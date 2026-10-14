#pragma once

#include <memory>
#include <string>

#include "sokol_gfx.h"

namespace pixo
{

/** Outcome of presenting a swapchain. */
enum class PresentResult
{
    presented,
    occluded,     // window hidden or covered: nothing shown, no vsync wait happened
    deviceLost
};

/** One native window's swapchain. Created, used and destroyed on the render
    thread only. */
class GpuSwapchain
{
public:
    virtual ~GpuSwapchain() = default;

    /** Matches the back buffers to the window's current client size.
        Returns false when there is nothing to draw into (e.g. zero size). */
    virtual bool prepare() = 0;

    /** The sokol description of the current back buffer. Valid after
        prepare() returned true. */
    virtual sg_swapchain describe() const = 0;

    virtual PresentResult present (bool waitForVSync) = 0;
};

/** The platform GPU device under sokol_gfx (D4). Render thread only. */
class GpuBackend
{
public:
    virtual ~GpuBackend() = default;

    virtual const char* getName() const = 0;

    /** The GPU adapter in use (which one matters on hybrid laptops). */
    virtual std::string getAdapterName() const = 0;

    /** The device objects and defaults sokol_gfx needs for sg_setup(). */
    virtual sg_environment describeEnvironment() const = 0;

    /** Returns nullptr and fills `error` on failure. */
    virtual std::unique_ptr<GpuSwapchain> createSwapchain (void* nativeWindow, std::string& error) = 0;

    /** Completes the destruction of released swapchains now (D3D11 defers
        it to the next flush), so their windows can be destroyed after. */
    virtual void flushReleases() = 0;

    /** Releases the device after sg_shutdown(). Returns a diagnostic line,
        e.g. the references still held on the device (should be none). */
    virtual std::string releaseDevice() = 0;
};

/** Creates this platform's backend, or returns nullptr and fills `error`. */
std::unique_ptr<GpuBackend> createPlatformGpuBackend (std::string& error);

} // namespace pixo
