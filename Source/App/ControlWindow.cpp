#include "App/ControlWindow.h"

#include "App/OutputWindow.h"
#include "Media/MediaLibrary.h"
#include "Render/GpuSurfaceComponent.h"
#include "Render/RenderEngine.h"
#include "UI/CameraPanel.h"
#include "UI/MediaPanel.h"
#include "UI/ParameterPanel.h"

namespace pixo
{
namespace
{

const juce::Colour backgroundColour { 0xff151515 };
constexpr int sidePanelWidth = 420;

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
    ControlPanel (RenderEngine& engineIn, OutputWindow& outputIn, ParameterStore& params, MediaLibrary& media,
                  CameraController& camera)
        : engine (engineIn),
          output (outputIn),
          preview (engineIn, SurfaceRole::preview),
          mediaPanel (media),
          cameraPanel (camera),
          parameterPanel (params)
    {
        title.setText ("Le Pixophone", juce::dontSendNotification);
        title.setFont (juce::FontOptions (22.0f, juce::Font::bold));
        addAndMakeVisible (title);

        subtitle.setText ("P2 - camera (parameter test bench)", juce::dontSendNotification);
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

        addAndMakeVisible (mediaPanel);
        addAndMakeVisible (cameraPanel);

        parameterView.setViewedComponent (&parameterPanel, false);
        parameterView.setScrollBarsShown (true, false);
        addAndMakeVisible (parameterView);

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

        auto side = area.removeFromRight (sidePanelWidth);
        area.removeFromRight (16);
        mediaPanel.setBounds (side.removeFromTop (MediaPanel::preferredHeight));
        side.removeFromTop (8);
        cameraPanel.setBounds (side.removeFromTop (CameraPanel::preferredHeight));
        side.removeFromTop (8);
        parameterView.setBounds (side);
        parameterPanel.setSize (side.getWidth() - parameterView.getScrollBarThickness() - 4,
                                parameterPanel.getContentHeight());

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
             << "   Surfaces: " << status.surfaceCount << "\n"
             << "Auto-invert: " << (status.autoInverted ? "inverted" : "normal")
             << "   next flip " << juce::roundToInt (status.autoInvertProgress * 100.0f) << "%"
             << "   motion " << juce::String (status.motion, 3) << "\n";

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
    MediaPanel mediaPanel;
    CameraPanel cameraPanel;
    ParameterPanel parameterPanel;
    juce::Viewport parameterView;
    juce::Label displayLabel;
    juce::ComboBox displayBox;
    juce::ToggleButton fullscreenToggle;
    juce::Label statusLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControlPanel)
};

} // namespace

ControlWindow::ControlWindow (RenderEngine& engine, OutputWindow& output, ParameterStore& params, MediaLibrary& media,
                              CameraController& camera)
    : juce::DocumentWindow ("Le Pixophone", backgroundColour,
                            juce::DocumentWindow::minimiseButton | juce::DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new ControlPanel (engine, output, params, media, camera), false);
    setResizable (true, false);
    setResizeLimits (960, 600, 4096, 4096);
    centreWithSize (1360, 880);
    setVisible (true);
}

void ControlWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

} // namespace pixo
