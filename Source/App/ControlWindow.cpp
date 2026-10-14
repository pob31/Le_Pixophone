#include "App/ControlWindow.h"

#include "App/OutputWindow.h"
#include "Render/GpuSurfaceComponent.h"
#include "Render/RenderEngine.h"

namespace pixo
{
namespace
{

const juce::Colour backgroundColour { 0xff151515 };

/** Teal, as in the reference frames, to tell the preview from the output. */
const juce::Colour previewTint { 0xff3fa7a3 };

juce::String describeDisplay (const juce::Displays::Display& display, int index)
{
    const auto pixels = display.physicalBounds;     // a projector is described by its pixel resolution
    const auto scale = juce::roundToInt (display.scale * 100.0);

    return juce::String (index + 1) + ": " + juce::String (pixels.getWidth()) + " x " + juce::String (pixels.getHeight())
         + (scale != 100 ? " @ " + juce::String (scale) + "%" : juce::String())
         + (display.isMain ? " (main)" : "");
}

class ControlPanel final : public juce::Component,
                           private juce::Timer
{
public:
    ControlPanel (RenderEngine& engineIn, OutputWindow& outputIn)
        : engine (engineIn),
          output (outputIn),
          preview (engineIn, SurfaceRole::preview, previewTint)
    {
        title.setText ("Le Pixophone", juce::dontSendNotification);
        title.setFont (juce::FontOptions (22.0f, juce::Font::bold));
        addAndMakeVisible (title);

        subtitle.setText ("P0 scaffold - render engine test pattern", juce::dontSendNotification);
        subtitle.setColour (juce::Label::textColourId, juce::Colours::grey);
        addAndMakeVisible (subtitle);

        addAndMakeVisible (preview);

        displayLabel.setText ("Output display", juce::dontSendNotification);
        addAndMakeVisible (displayLabel);

        displayBox.onChange = [this]
        {
            if (displayBox.getSelectedItemIndex() >= 0)
                output.setDisplay (displayBox.getSelectedItemIndex());
        };
        addAndMakeVisible (displayBox);

        fullscreenToggle.setButtonText ("Fullscreen (Esc on the output leaves it)");
        fullscreenToggle.onClick = [this] { output.setFullscreen (fullscreenToggle.getToggleState()); };
        addAndMakeVisible (fullscreenToggle);

        statusLabel.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
        statusLabel.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (statusLabel);

        output.onPlacementChanged = [this] { syncWithOutput(); };

        refreshDisplays();
        syncWithOutput();
        startTimerHz (4);
    }

    ~ControlPanel() override
    {
        output.onPlacementChanged = nullptr;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (backgroundColour);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (16);

        title.setBounds (area.removeFromTop (30));
        subtitle.setBounds (area.removeFromTop (22));
        area.removeFromTop (12);

        auto statusArea = area.removeFromBottom (70);
        area.removeFromBottom (8);
        auto controls = area.removeFromBottom (28);
        area.removeFromBottom (12);

        // 16:9 preview, as large as the remaining space allows.
        const auto previewWidth = juce::jmin (area.getWidth(), area.getHeight() * 16 / 9);
        preview.setBounds (area.withSizeKeepingCentre (previewWidth, previewWidth * 9 / 16));

        displayLabel.setBounds (controls.removeFromLeft (110));
        displayBox.setBounds (controls.removeFromLeft (260));
        controls.removeFromLeft (16);
        fullscreenToggle.setBounds (controls);

        statusLabel.setBounds (statusArea);
    }

private:
    void timerCallback() override
    {
        if (juce::Desktop::getInstance().getDisplays().displays.size() != displayBox.getNumItems())
            refreshDisplays();

        const auto status = engine.getStatus();

        juce::String text;
        text << "GPU: " << (status.backendName.isNotEmpty() ? status.backendName : juce::String ("starting...")) << "\n"
             << "Frame rate: " << juce::String (status.framesPerSecond, 1) << " fps"
             << "   Surfaces: " << status.surfaceCount << "\n";

        if (status.error.isNotEmpty())
            text << "Error: " << status.error;

        statusLabel.setText (text, juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId,
                               status.error.isNotEmpty() ? juce::Colours::orangered : juce::Colours::lightgrey);
    }

    void refreshDisplays()
    {
        const auto& displays = juce::Desktop::getInstance().getDisplays().displays;

        displayBox.clear (juce::dontSendNotification);

        for (int i = 0; i < displays.size(); ++i)
            displayBox.addItem (describeDisplay (displays.getReference (i), i), i + 1);

        displayBox.setSelectedItemIndex (output.getDisplay(), juce::dontSendNotification);
    }

    void syncWithOutput()
    {
        displayBox.setSelectedItemIndex (output.getDisplay(), juce::dontSendNotification);
        fullscreenToggle.setToggleState (output.isFullscreen(), juce::dontSendNotification);
    }

    RenderEngine& engine;
    OutputWindow& output;

    juce::Label title;
    juce::Label subtitle;
    GpuSurfaceComponent preview;
    juce::Label displayLabel;
    juce::ComboBox displayBox;
    juce::ToggleButton fullscreenToggle;
    juce::Label statusLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControlPanel)
};

} // namespace

ControlWindow::ControlWindow (RenderEngine& engine, OutputWindow& output)
    : juce::DocumentWindow ("Le Pixophone", backgroundColour,
                            juce::DocumentWindow::minimiseButton | juce::DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new ControlPanel (engine, output), false);
    setResizable (true, false);
    setResizeLimits (640, 480, 4096, 4096);
    centreWithSize (960, 720);
    setVisible (true);
}

void ControlWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

} // namespace pixo
