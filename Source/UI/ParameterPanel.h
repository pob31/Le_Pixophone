#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

#include "Core/Params.h"

namespace pixo
{

/** One slider per parameter, grouped, generated from the ParamDef table:
    the test bench until the real control surface (P4). Sliders write to the
    store; a timer follows changes made elsewhere (command line, later MIDI). */
class ParameterPanel final : public juce::Component,
                             private juce::Timer
{
public:
    explicit ParameterPanel (ParameterStore& store);

    void resized() override;

    /** Height needed to show every row, for a surrounding Viewport. */
    int getContentHeight() const;

private:
    void timerCallback() override;

    struct Row
    {
        ParamId id;
        std::unique_ptr<juce::Label> heading;   // set on the first row of a group
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::Slider> slider;
    };

    ParameterStore& store;
    std::vector<Row> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParameterPanel)
};

} // namespace pixo
