#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace pixo
{

class MediaLibrary;
class OutputWindow;
class ParameterStore;
class RenderEngine;

/** The performer's window: output preview, output placement, engine status,
    media and the parameter test bench. The real control surface
    (PD114-ANALYSIS §3.5) comes in P4. */
class ControlWindow final : public juce::DocumentWindow
{
public:
    ControlWindow (RenderEngine& engine, OutputWindow& output, ParameterStore& params, MediaLibrary& media);

    void closeButtonPressed() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControlWindow)
};

} // namespace pixo
