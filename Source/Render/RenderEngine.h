#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace pixo
{

class GpuBackend;

/** What a surface is for. The output paces the render loop (vsync); previews
    present without waiting so they never hold the projector back. */
enum class SurfaceRole
{
    output,
    preview
};

using SurfaceId = int;

/** Owns the render thread, the GPU device and sokol_gfx (D4, D9).

    Surfaces are native windows hosted by GpuSurfaceComponent. They are
    attached and detached from the message thread; their swapchains are
    created, used and released on the render thread only.

    Threading rule: the message thread never blocks on the render thread.
    DXGI sends messages to a swapchain window's thread when swapchains are
    released, so a message thread waiting on the render thread can deadlock
    it. Detaching and stopping are therefore asynchronous: completion is
    reported back on the message thread. */
class RenderEngine final : private juce::Thread
{
public:
    RenderEngine();

    /** Call stopAsync() and wait for its callback first; destroying a
        running engine blocks the message thread (last-resort path). */
    ~RenderEngine() override;

    /** Starts the render thread, which creates the GPU device. */
    void start();

    /** Message thread. Stops the render loop and releases every swapchain
        and the GPU device while the message loop keeps running, then calls
        `whenStopped` on the message thread. Call before quitting. */
    void stopAsync (std::function<void()> whenStopped);

    /** Message thread. The window must stay alive until the matching
        detachSurface() reports back. */
    SurfaceId attachSurface (void* nativeWindow, SurfaceRole role, juce::Colour tint);

    /** Message thread, non-blocking. `afterRelease` is called on the message
        thread once the render thread no longer uses the surface's window,
        which may then be destroyed. Called immediately if the engine has
        stopped. */
    void detachSurface (SurfaceId id, std::function<void()> afterRelease);

    struct Status
    {
        juce::String backendName;
        juce::String error;             // empty while healthy
        double framesPerSecond = 0.0;
        int surfaceCount = 0;
    };

    /** Any thread. */
    Status getStatus() const;

private:
    struct Surface;     // render-thread state of one attached window
    struct Request;     // an attach or detach waiting for the render thread
    struct GpuObjects;  // sokol pipelines and other GPU resources

    enum class LoopState
    {
        notStarted,
        running,
        stopped
    };

    void run() override;
    bool initialiseGpu();
    void shutdownGpu();
    void applyRequests();
    bool renderFrame (double timeSeconds);
    void countFrame();
    void finishLoop();
    void setError (const juce::String& message);

    // Render thread only.
    std::unique_ptr<GpuBackend> backend;
    std::unique_ptr<GpuObjects> gpu;
    std::vector<std::unique_ptr<Surface>> surfaces;
    double statsStartMs = 0.0;
    int statsFrames = 0;

    // Shared with the message thread, guarded by requestLock.
    std::mutex requestLock;
    std::vector<std::shared_ptr<Request>> requests;
    LoopState loopState = LoopState::notStarted;
    std::function<void()> onStopped;

    // Message thread only.
    SurfaceId nextSurfaceId = 1;

    mutable std::mutex statusLock;
    Status status;      // guarded by statusLock

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RenderEngine)
};

} // namespace pixo
