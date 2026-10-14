#include "UI/CameraPanel.h"

#include <algorithm>

#include "Camera/CameraController.h"

namespace pixo
{
namespace
{

juce::String describeRange (const char* name, const CameraControlRange& range)
{
    if (! range.supported)
        return juce::String (name) + " n/a";

    return juce::String (name) + " " + juce::String (range.minValue) + ".." + juce::String (range.maxValue)
         + (range.step > 1 ? " /" + juce::String (range.step) : juce::String());
}

} // namespace

CameraPanel::CameraPanel (CameraController& controllerIn)
    : controller (controllerIn)
{
    heading.setText ("Camera", juce::dontSendNotification);
    heading.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    addAndMakeVisible (heading);

    deviceBox.setTextWhenNothingSelected ("(no camera)");
    deviceBox.onChange = [this]
    {
        const auto index = deviceBox.getSelectedItemIndex();

        if (index >= 0 && index < static_cast<int> (devices.size()))
            controller.selectDevice (devices[static_cast<std::size_t> (index)].id);
    };
    addAndMakeVisible (deviceBox);

    formatBox.setTextWhenNothingSelected ("(mode)");
    formatBox.onChange = [this]
    {
        const auto index = formatBox.getSelectedItemIndex();

        if (index >= 0 && index < static_cast<int> (formats.size()))
            controller.selectFormat (formats[static_cast<std::size_t> (index)]);
    };
    addAndMakeVisible (formatBox);

    statusLabel.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
    statusLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (statusLabel);

    refreshDevices();
    startTimerHz (4);
}

void CameraPanel::refreshDevices()
{
    devices = controller.listDevices();
    deviceBox.clear (juce::dontSendNotification);

    for (std::size_t i = 0; i < devices.size(); ++i)
        deviceBox.addItem (devices[i].name, static_cast<int> (i) + 1);

    const auto current = controller.getDeviceId();
    shownDevice = current;

    for (std::size_t i = 0; i < devices.size(); ++i)
        if (devices[i].id == current)
            deviceBox.setSelectedItemIndex (static_cast<int> (i), juce::dontSendNotification);
}

void CameraPanel::refreshFormats (const CameraStatus& status)
{
    // Sorted, largest first, without duplicates.
    auto sorted = status.formats;
    std::sort (sorted.begin(), sorted.end(), [] (const CameraFormat& a, const CameraFormat& b)
    {
        if (a.width != b.width) return a.width > b.width;
        if (a.height != b.height) return a.height > b.height;
        if (a.fps != b.fps) return a.fps > b.fps;
        return a.encoding < b.encoding;
    });

    sorted.erase (std::unique (sorted.begin(), sorted.end(), [] (const CameraFormat& a, const CameraFormat& b)
    {
        return a.width == b.width && a.height == b.height && a.fps == b.fps && a.encoding == b.encoding;
    }), sorted.end());

    formats = sorted;
    formatBox.clear (juce::dontSendNotification);

    for (std::size_t i = 0; i < formats.size(); ++i)
    {
        formatBox.addItem (formats[i].describe(), static_cast<int> (i) + 1);

        if (formats[i].describe() == status.format.describe())
            formatBox.setSelectedItemIndex (static_cast<int> (i), juce::dontSendNotification);
    }

    shownFormat = status.format.describe();
}

void CameraPanel::timerCallback()
{
    const auto status = controller.getStatus();

    if (status.open && status.deviceId != shownDevice)
        refreshDevices();

    if (status.open && status.format.describe() != shownFormat)
        refreshFormats (status);

    juce::String text;

    if (status.open)
    {
        text << status.format.describe() << "   " << juce::String (status.measuredFps, 1) << " fps captured\n"
             << describeRange ("exposure", status.ranges[static_cast<int> (CameraControl::exposure)]) << "   "
             << describeRange ("gain", status.ranges[static_cast<int> (CameraControl::gain)]);
    }
    else
    {
        text << (status.error.empty() ? juce::String ("closed") : juce::String (status.error));
    }

    statusLabel.setText (text, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId,
                           status.error.empty() ? juce::Colours::lightgrey : juce::Colours::orange);
}

void CameraPanel::resized()
{
    auto area = getLocalBounds();
    heading.setBounds (area.removeFromTop (26));
    deviceBox.setBounds (area.removeFromTop (26).reduced (0, 1));
    area.removeFromTop (4);
    formatBox.setBounds (area.removeFromTop (26).reduced (0, 1));
    area.removeFromTop (4);
    statusLabel.setBounds (area);
}

} // namespace pixo
