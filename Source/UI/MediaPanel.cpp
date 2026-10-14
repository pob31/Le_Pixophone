#include "UI/MediaPanel.h"

#include "Media/MediaLibrary.h"

namespace pixo
{
namespace
{

juce::String describe (MediaLibrary::SlotStatus status)
{
    switch (status)
    {
        case MediaLibrary::SlotStatus::empty:   return "empty";
        case MediaLibrary::SlotStatus::loading: return "loading";
        case MediaLibrary::SlotStatus::loaded:  return "ok";
        case MediaLibrary::SlotStatus::missing: return "missing, kept previous";
        case MediaLibrary::SlotStatus::failed:  return "unreadable, kept previous";
    }

    return {};
}

} // namespace

MediaPanel::MediaPanel (MediaLibrary& libraryIn)
    : library (libraryIn)
{
    heading.setText ("Media", juce::dontSendNotification);
    heading.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    addAndMakeVisible (heading);

    rootLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    rootLabel.setMinimumHorizontalScale (0.5f);
    addAndMakeVisible (rootLabel);

    chooseButton.onClick = [this] { chooseMediaRoot(); };
    addAndMakeVisible (chooseButton);

    presetLabel.setText ("Mix preset", juce::dontSendNotification);
    addAndMakeVisible (presetLabel);

    presetSlider.setRange (0.0, 127.0, 1.0);
    presetSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 44, 22);
    addAndMakeVisible (presetSlider);

    loadButton.onClick = [this] { library.loadPreset (ImageBank::mix, static_cast<int> (presetSlider.getValue())); };
    addAndMakeVisible (loadButton);

    stripLabel.setText ("Strip preset", juce::dontSendNotification);
    addAndMakeVisible (stripLabel);

    stripSlider.setRange (0.0, 127.0, 1.0);
    stripSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 44, 22);
    addAndMakeVisible (stripSlider);

    stripLoadButton.onClick = [this] { library.loadPreset (ImageBank::strip, static_cast<int> (stripSlider.getValue())); };
    addAndMakeVisible (stripLoadButton);

    stripStateLabel.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.5f, juce::Font::plain));
    addAndMakeVisible (stripStateLabel);

    for (auto& label : slotLabels)
    {
        label.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.5f, juce::Font::plain));
        addAndMakeVisible (label);
    }

    statusLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible (statusLabel);

    library.onChange = [this] { refresh(); };
    refresh();
}

MediaPanel::~MediaPanel()
{
    library.onChange = nullptr;
}

void MediaPanel::refresh()
{
    const auto root = library.getMediaRoot();
    rootLabel.setText (root == juce::File() ? juce::String ("(no media folder)") : root.getFullPathName(),
                       juce::dontSendNotification);

    for (int slot = 0; slot < static_cast<int> (slotLabels.size()); ++slot)
    {
        const auto state = library.getSlot (ImageBank::mix, slot);
        auto& label = slotLabels[static_cast<std::size_t> (slot)];

        label.setText ("mix" + juce::String (slot) + "  "
                           + (state.fileName.isNotEmpty() ? state.fileName : juce::String ("-"))
                           + "  [" + describe (state.status) + "]",
                       juce::dontSendNotification);

        const bool problem = state.status == MediaLibrary::SlotStatus::missing
                          || state.status == MediaLibrary::SlotStatus::failed;
        label.setColour (juce::Label::textColourId, problem ? juce::Colours::orange : juce::Colours::lightgrey);
    }

    juce::StringArray parts;
    bool stripProblem = false;

    for (int slot = 0; slot < bankSlotCount (ImageBank::strip); ++slot)
    {
        const auto state = library.getSlot (ImageBank::strip, slot);

        if (state.fileName.isNotEmpty())
            parts.add (state.fileName.upToLastOccurrenceOf (".", false, false) + " [" + describe (state.status) + "]");

        stripProblem = stripProblem || state.status == MediaLibrary::SlotStatus::missing
                                    || state.status == MediaLibrary::SlotStatus::failed;
    }

    stripStateLabel.setText ("strip  " + (parts.isEmpty() ? juce::String ("-") : parts.joinIntoString (", ")),
                             juce::dontSendNotification);
    stripStateLabel.setColour (juce::Label::textColourId, stripProblem ? juce::Colours::orange : juce::Colours::lightgrey);

    statusLabel.setText (library.getStatusMessage(), juce::dontSendNotification);
}

void MediaPanel::chooseMediaRoot()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose the Le_Pixophone media folder (with presets/ and images1024/)",
                                                   library.getMediaRoot());

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto folder = fc.getResult();

                              if (folder.isDirectory())
                              {
                                  library.setMediaRoot (folder);
                                  library.loadPreset (ImageBank::mix, static_cast<int> (presetSlider.getValue()));
                              }
                          });
}

void MediaPanel::resized()
{
    auto area = getLocalBounds();

    heading.setBounds (area.removeFromTop (26));

    auto rootRow = area.removeFromTop (26);
    chooseButton.setBounds (rootRow.removeFromRight (120).reduced (0, 1));
    rootLabel.setBounds (rootRow);
    area.removeFromTop (4);

    auto presetRow = area.removeFromTop (26);
    presetLabel.setBounds (presetRow.removeFromLeft (110));
    loadButton.setBounds (presetRow.removeFromRight (70).reduced (0, 1));
    presetSlider.setBounds (presetRow.reduced (4, 0));
    area.removeFromTop (6);

    for (auto& label : slotLabels)
        label.setBounds (area.removeFromTop (20));

    area.removeFromTop (6);
    auto stripRow = area.removeFromTop (26);
    stripLabel.setBounds (stripRow.removeFromLeft (110));
    stripLoadButton.setBounds (stripRow.removeFromRight (70).reduced (0, 1));
    stripSlider.setBounds (stripRow.reduced (4, 0));
    stripStateLabel.setBounds (area.removeFromTop (20));

    area.removeFromTop (4);
    statusLabel.setBounds (area.removeFromTop (22));
}

} // namespace pixo
