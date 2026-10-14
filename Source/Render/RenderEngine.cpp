#include "Render/RenderEngine.h"

#include <juce_events/juce_events.h>

#include <algorithm>
#include <array>
#include <string>

#include "Render/Compositor.h"
#include "Render/GpuBackend.h"

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
    std::unique_ptr<GpuSwapchain> swapchain;
};

struct RenderEngine::Request
{
    enum class Kind
    {
        attach,
        detach,
        image
    };

    Kind kind = Kind::attach;
    SurfaceId id = 0;
    SurfaceRole role = SurfaceRole::preview;
    void* nativeWindow = nullptr;
    std::function<void()> afterRelease;             // detach only
    ImageBank bank = ImageBank::mix;                // image only
    int slot = 0;                                   // image only
    std::shared_ptr<const DecodedImage> image;      // image only
};

//==============================================================================
RenderEngine::RenderEngine (ParameterStore& paramsIn)
    : juce::Thread ("Pixophone render"),
      params (paramsIn)
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

SurfaceId RenderEngine::attachSurface (void* nativeWindow, SurfaceRole role)
{
    auto request = std::make_shared<Request>();
    request->kind = Request::Kind::attach;
    request->id = nextSurfaceId++;
    request->role = role;
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

void RenderEngine::setImage (ImageBank bank, int slot, std::shared_ptr<const DecodedImage> image)
{
    auto request = std::make_shared<Request>();
    request->kind = Request::Kind::image;
    request->bank = bank;
    request->slot = slot;
    request->image = std::move (image);

    const std::lock_guard<std::mutex> lock (requestLock);

    if (loopState != LoopState::stopped)
        requests.push_back (request);
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

    compositor = std::make_unique<Compositor>();

    if (! compositor->initialise (error))
    {
        setError ("compositor: " + juce::String (error));
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
    compositor.reset();     // its sokol resources die with sg_shutdown()

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
        if (request->kind == Request::Kind::image)
        {
            compositor->setImage (request->bank, request->slot, request->image.get());
        }
        else if (request->kind == Request::Kind::attach)
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
    // The picture is rendered once, at the output window's size (its aspect
    // is GEM's), then shown in every window. Without an output window it is
    // rendered at a nominal 1280×720 for the previews.
    std::vector<Surface*> drawn;
    int outputWidth = 0;
    int outputHeight = 0;

    for (auto& surface : surfaces)
    {
        if (! surface->swapchain->prepare())
            continue;

        drawn.push_back (surface.get());

        if (surface->role == SurfaceRole::output && outputWidth == 0)
        {
            const auto description = surface->swapchain->describe();
            outputWidth = description.width;
            outputHeight = description.height;
        }
    }

    if (outputWidth == 0)
    {
        outputWidth = 1280;
        outputHeight = 720;
    }

    compositor->renderFrame (params.snapshot(), params.compat, timeSeconds * 1000.0, outputWidth, outputHeight);

    for (auto* surface : drawn)
    {
        const auto description = surface->swapchain->describe();

        sg_pass pass {};
        pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
        pass.action.colors[0].clear_value = { 0.0f, 0.0f, 0.0f, 1.0f };
        pass.swapchain = description;
        pass.label = surface->role == SurfaceRole::output ? "output" : "preview";

        sg_begin_pass (&pass);
        compositor->drawToWindow (description.width, description.height);
        sg_end_pass();
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
