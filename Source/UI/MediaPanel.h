#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace pixo
{

class MediaLibrary;

/** Media folder, mix preset loading and the state of the four MIX slots.
    (Pad and strip banks join with their layers.) */
class MediaPanel final : public juce::Component
{
public:
    explicit MediaPanel (MediaLibrary& library);
    ~MediaPanel() override;

    void resized() override;

    static constexpr int preferredHeight = 214;

private:
    void refresh();
    void chooseMediaRoot();

    MediaLibrary& library;

    juce::Label heading;
    juce::Label rootLabel;
    juce::TextButton chooseButton { "Media folder..." };
    juce::Label presetLabel;
    juce::Slider presetSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::TextButton loadButton { "Load" };
    std::array<juce::Label, 4> slotLabels;
    juce::Label statusLabel;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MediaPanel)
};

} // namespace pixo
