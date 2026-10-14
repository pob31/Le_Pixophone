#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Render/RenderEngine.h"

namespace pixo
{

/** A rectangle of a JUCE window that the render thread draws into (D4).

    Owns a native child window positioned over the component, and registers
    it with the RenderEngine once it is embedded in a showing window. On
    destruction the window is unparented and handed to the engine, which
    destroys it only after the render thread has released its swapchain
    (RenderEngine's threading rule).

    A surface is assumed to stay in the top-level window it was first shown
    in. Mouse input passes through to the parent component. */
class GpuSurfaceComponent final : public juce::Component
{
public:
    GpuSurfaceComponent (RenderEngine& engine, SurfaceRole role);
    ~GpuSurfaceComponent() override;

    void paint (juce::Graphics&) override;

private:
    class Host;

    void windowEmbedded (void* nativeWindow);

    RenderEngine& engine;
    const SurfaceRole role;
    SurfaceId surfaceId = 0;
    std::unique_ptr<Host> host;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GpuSurfaceComponent)
};

} // namespace pixo
