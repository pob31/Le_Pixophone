#include "UI/ParameterPanel.h"

#include <cstring>

namespace pixo
{
namespace
{

constexpr int headingHeight = 26;
constexpr int rowHeight = 24;
constexpr int labelWidth = 110;

} // namespace

ParameterPanel::ParameterPanel (ParameterStore& storeIn)
    : store (storeIn)
{
    const char* currentGroup = nullptr;

    for (std::size_t i = 0; i < numParams; ++i)
    {
        const auto id = static_cast<ParamId> (i);
        const auto& def = paramDef (id);

        Row row;
        row.id = id;

        if (currentGroup == nullptr || std::strcmp (currentGroup, def.group) != 0)
        {
            currentGroup = def.group;
            row.heading = std::make_unique<juce::Label> (juce::String(), def.group);
            row.heading->setFont (juce::FontOptions (15.0f, juce::Font::bold));
            addAndMakeVisible (*row.heading);
        }

        row.label = std::make_unique<juce::Label> (juce::String(), def.label);
        row.label->setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible (*row.label);

        row.slider = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
        row.slider->setRange (def.minValue, def.maxValue, def.integer ? 1.0 : 0.0);
        row.slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, rowHeight - 4);
        row.slider->setValue (store.get (id), juce::dontSendNotification);
        row.slider->onValueChange = [this, id, slider = row.slider.get()]
        {
            store.set (id, static_cast<float> (slider->getValue()));
        };
        addAndMakeVisible (*row.slider);

        rows.push_back (std::move (row));
    }

    startTimerHz (10);
}

int ParameterPanel::getContentHeight() const
{
    int height = 0;

    for (const auto& row : rows)
        height += rowHeight + (row.heading != nullptr ? headingHeight : 0);

    return height + 8;
}

void ParameterPanel::resized()
{
    auto area = getLocalBounds();

    for (auto& row : rows)
    {
        if (row.heading != nullptr)
            row.heading->setBounds (area.removeFromTop (headingHeight));

        auto line = area.removeFromTop (rowHeight);
        row.label->setBounds (line.removeFromLeft (labelWidth));
        row.slider->setBounds (line);
    }
}

void ParameterPanel::timerCallback()
{
    for (auto& row : rows)
        if (! row.slider->isMouseButtonDown())
            row.slider->setValue (store.get (row.id), juce::dontSendNotification);
}

} // namespace pixo
