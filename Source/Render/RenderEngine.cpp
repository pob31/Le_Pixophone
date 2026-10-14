#include "Render/RenderEngine.h"

#include <juce_events/juce_events.h>

#include <algorithm>
#include <array>
#include <string>

#include "Render/GpuBackend.h"
#include "Render/shaders/testpattern.glsl.h"

namespace pixo
{
namespace
{

void logSokolMessage (const char* tag, uint32_t level, uint32_t itemId, const char* message,
                      uint32_t line, const char* /*file*/, void* /*userData*/)
{
    static constexpr const char* levelNames[] = { "panic", "error", "warning", "info" };
    const auto* levelName = level < 4 ? levelNames[level] : "?";

    juce::Logger::writeToLog (juce::String ("[") + tag + " " + levelName + " #" + juce::String (itemId) + "] "
                              + (message != nullptr ? message : "") + " (sokol_gfx.h:" + juce::String (line) + ")");
}

void callOnMessageThread (std::function<void()> callback)
{
    if (callback != nullptr)
        juce::MessageManager::callAsync (std::move (callback));
}

} // namespace

struct RenderEngine::Surface
{
    SurfaceId id = 0;
    SurfaceRole role = SurfaceRole::preview;
    std::array<float, 4> tint {};
    std::unique_ptr<GpuSwapchain> swapchain;
};

struct RenderEngine::Request
{
    enum class Kind
    {
        attach,
        detach
    };

    Kind kind = Kind::attach;
    SurfaceId id = 0;
    SurfaceRole role = SurfaceRole::preview;
    std::array<float, 4> tint {};
    void* nativeWindow = nullptr;
    std::function<void()> afterRelease;     // detach only
};

struct RenderEngine::GpuObjects
{
    sg_pipeline testPattern {};
};

//==============================================================================
RenderEngine::RenderEngine()
    : juce::Thread ("Pixophone render")
{
}

RenderEngine::~RenderEngine()
{
    stopThread (4000);
}

void RenderEngine::start()
{
    startThread (juce::Thread::Priority::highest);
}

void RenderEngine::stopAsync (std::function<void()> whenStopped)
{
    {
        const std::lock_guard<std::mutex> lock (requestLock);

        if (loopState != LoopState::running)
        {
            callOnMessageThread (std::move (whenStopped));
            return;
        }

        onStopped = std::move (whenStopped);
    }

    signalThreadShouldExit();
    notify();   // wake the loop if it is pacing itself in wait()
}

SurfaceId RenderEngine::attachSurface (void* nativeWindow, SurfaceRole role, juce::Colour tint)
{
    auto request = std::make_shared<Request>();
    request->kind = Request::Kind::attach;
    request->id = nextSurfaceId++;
    request->role = role;
    request->tint = { tint.getFloatRed(), tint.getFloatGreen(), tint.getFloatBlue(), tint.getFloatAlpha() };
    request->nativeWindow = nativeWindow;

    const std::lock_guard<std::mutex> lock (requestLock);

    if (loopState != LoopState::stopped)
        requests.push_back (request);

    return request->id;
}

void RenderEngine::detachSurface (SurfaceId id, std::function<void()> afterRelease)
{
    {
        const std::lock_guard<std::mutex> lock (requestLock);

        if (loopState == LoopState::running)
        {
            auto request = std::make_shared<Request>();
            request->kind = Request::Kind::detach;
            request->id = id;
            request->afterRelease = std::move (afterRelease);
            requests.push_back (request);
            return;
        }

        // Not started: no swapchain exists yet, dropping the pending attach is
        // enough. Stopped: every swapchain is already released.
        requests.erase (std::remove_if (requests.begin(), requests.end(),
                                        [id] (const auto& r) { return r->id == id; }),
                        requests.end());
    }

    if (afterRelease != nullptr)
        afterRelease();
}

RenderEngine::Status RenderEngine::getStatus() const
{
    const std::lock_guard<std::mutex> lock (statusLock);
    return status;
}

void RenderEngine::setError (const juce::String& message)
{
    juce::Logger::writeToLog ("RenderEngine: " + message);

    const std::lock_guard<std::mutex> lock (statusLock);
    status.error = message;
}

//==============================================================================
void RenderEngine::run()
{
    {
        const std::lock_guard<std::mutex> lock (requestLock);
        loopState = LoopState::running;
    }

    const auto initStartMs = juce::Time::getMillisecondCounterHiRes();

    if (initialiseGpu())
    {
        const auto startMs = juce::Time::getMillisecondCounterHiRes();
        statsStartMs = startMs;

        juce::Logger::writeToLog ("RenderEngine: " + juce::String (backend->getName()) + " ready in "
                                  + juce::String (startMs - initStartMs, 1) + " ms");

        while (! threadShouldExit())
        {
            applyRequests();

            const auto timeSeconds = (juce::Time::getMillisecondCounterHiRes() - startMs) * 0.001;

            // Without a visible output surface nothing waits for vsync:
            // pace the loop by hand rather than spin.
            if (! renderFrame (timeSeconds))
                wait (16);
        }
    }

    finishLoop();
}

void RenderEngine::finishLoop()
{
    juce::Logger::writeToLog ("RenderEngine: render loop finished, releasing "
                              + juce::String (static_cast<int> (surfaces.size())) + " surface(s)");

    // Swapchains and device go first, while (after stopAsync) the message
    // thread still pumps messages for DXGI. Only then report back.
    const auto startMs = juce::Time::getMillisecondCounterHiRes();
    surfaces.clear();
    shutdownGpu();
    const auto releasedMs = juce::Time::getMillisecondCounterHiRes() - startMs;

    std::vector<std::function<void()>> callbacks;

    {
        const std::lock_guard<std::mutex> lock (requestLock);
        loopState = LoopState::stopped;

        for (auto& request : requests)
            if (request->afterRelease != nullptr)
                callbacks.push_back (std::move (request->afterRelease));

        requests.clear();

        if (onStopped != nullptr)
            callbacks.push_back (std::move (onStopped));
    }

    for (auto& callback : callbacks)
        callOnMessageThread (std::move (callback));

    juce::Logger::writeToLog ("RenderEngine: stopped, GPU released in " + juce::String (releasedMs, 1) + " ms");
}

bool RenderEngine::initialiseGpu()
{
    std::string error;
    backend = createPlatformGpuBackend (error);

    if (backend == nullptr)
    {
        setError ("GPU initialisation failed: " + juce::String (error));
        return false;
    }

    sg_desc desc {};
    desc.environment = backend->describeEnvironment();
    desc.logger.func = logSokolMessage;
    sg_setup (&desc);

    if (! sg_isvalid())
    {
        setError ("sokol_gfx setup failed (see log)");
        backend.reset();
        return false;
    }

    gpu = std::make_unique<GpuObjects>();

    sg_pipeline_desc pipelineDesc {};
    pipelineDesc.shader = sg_make_shader (testpattern_shader_desc (sg_query_backend()));
    pipelineDesc.label = "testpattern";
    gpu->testPattern = sg_make_pipeline (&pipelineDesc);

    if (sg_query_pipeline_state (gpu->testPattern) != SG_RESOURCESTATE_VALID)
    {
        setError ("test-pattern pipeline failed (see log)");
        shutdownGpu();
        return false;
    }

    juce::Logger::writeToLog ("RenderEngine: adapter " + juce::String (backend->getAdapterName()));

    const std::lock_guard<std::mutex> lock (statusLock);
    status.backendName = juce::String (backend->getName()) + " on " + juce::String (backend->getAdapterName());
    return true;
}

void RenderEngine::shutdownGpu()
{
    gpu.reset();        // sokol resources die with sg_shutdown()

    if (sg_isvalid())
        sg_shutdown();

    if (backend != nullptr)
        juce::Logger::writeToLog ("RenderEngine: " + juce::String (backend->releaseDevice()));

    backend.reset();
}

void RenderEngine::applyRequests()
{
    std::vector<std::shared_ptr<Request>> pending;

    {
        const std::lock_guard<std::mutex> lock (requestLock);
        pending.swap (requests);
    }

    for (auto& request : pending)
    {
        if (request->kind == Request::Kind::attach)
        {
            std::string error;
            auto swapchain = backend->createSwapchain (request->nativeWindow, error);

            if (swapchain == nullptr)
            {
                setError ("surface " + juce::String (request->id) + ": " + juce::String (error));
                continue;
            }

            auto surface = std::make_unique<Surface>();
            surface->id = request->id;
            surface->role = request->role;
            surface->tint = request->tint;
            surface->swapchain = std::move (swapchain);
            surfaces.push_back (std::move (surface));

            juce::Logger::writeToLog ("RenderEngine: surface " + juce::String (request->id) + " attached ("
                                      + (request->role == SurfaceRole::output ? "output" : "preview") + ")");
        }
        else
        {
            surfaces.erase (std::remove_if (surfaces.begin(), surfaces.end(),
                                            [&] (const auto& s) { return s->id == request->id; }),
                            surfaces.end());
            backend->flushReleases();

            juce::Logger::writeToLog ("RenderEngine: surface " + juce::String (request->id) + " detached");
            callOnMessageThread (std::move (request->afterRelease));
        }
    }
}

bool RenderEngine::renderFrame (double timeSeconds)
{
    std::vector<Surface*> drawn;

    for (auto& surface : surfaces)
    {
        if (! surface->swapchain->prepare())
            continue;

        sg_pass pass {};
        pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
        pass.action.colors[0].clear_value = { 0.0f, 0.0f, 0.0f, 1.0f };
        pass.swapchain = surface->swapchain->describe();
        pass.label = surface->role == SurfaceRole::output ? "output" : "preview";

        sg_begin_pass (&pass);
        sg_apply_pipeline (gpu->testPattern);

        fs_params_t params {};
        params.params[0] = static_cast<float> (timeSeconds);
        params.params[1] = 15.0f;   // ≈ the reference frames' band count (PD114-ANALYSIS §1)
        std::copy (surface->tint.begin(), surface->tint.end(), params.tint);
        sg_apply_uniforms (UB_fs_params, SG_RANGE (params));

        sg_draw (0, 3, 1);
        sg_end_pass();

        drawn.push_back (surface.get());
    }

    sg_commit();

    bool paced = false;

    for (auto* surface : drawn)
    {
        const bool isOutput = surface->role == SurfaceRole::output;
        const auto result = surface->swapchain->present (isOutput && ! paced);

        if (result == PresentResult::deviceLost)
        {
            // Device recovery is out of scope for P0: stop cleanly instead.
            setError ("GPU device lost - restart the application");
            signalThreadShouldExit();
            return true;
        }

        if (isOutput && result == PresentResult::presented)
            paced = true;
    }

    countFrame();
    return paced;
}

void RenderEngine::countFrame()
{
    ++statsFrames;

    const auto nowMs = juce::Time::getMillisecondCounterHiRes();
    const auto elapsedMs = nowMs - statsStartMs;

    if (elapsedMs < 500.0)
        return;

    const std::lock_guard<std::mutex> lock (statusLock);
    status.framesPerSecond = statsFrames * 1000.0 / elapsedMs;
    status.surfaceCount = static_cast<int> (surfaces.size());

    statsFrames = 0;
    statsStartMs = nowMs;
}

} // namespace pixo
