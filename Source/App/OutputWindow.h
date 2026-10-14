#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Render/GpuSurfaceComponent.h"

namespace pixo
{

/** The projector output: a borderless top-level window on a chosen display.

    Fullscreen covers the whole display, stays on top and hides the cursor,
    like the original's external-display presets (PD114-ANALYSIS §2.1).
    Windowed is an 800×600 window centred on the display and draggable with
    the mouse, for working without a projector. Escape leaves fullscreen. */
class OutputWindow final : public juce::Component
{
public:
    explicit OutputWindow (RenderEngine& engine);
    ~OutputWindow() override;

    /** Index into juce::Desktop's display list; clamped when applied. */
    void setDisplay (int displayIndex);
    int getDisplay() const noexcept { return displayIndex; }

    void setFullscreen (bool shouldBeFullscreen);
    bool isFullscreen() const noexcept { return fullscreen; }

    /** Called when the window changes its own placement (e.g. Escape). */
    std::function<void()> onPlacementChanged;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void applyPlacement();

    GpuSurfaceComponent surface;
    juce::ComponentDragger dragger;
    int displayIndex = 0;
    bool fullscreen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutputWindow)
};

} // namespace pixo
