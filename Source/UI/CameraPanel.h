#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

#include "Camera/CameraSource.h"

namespace pixo
{

class CameraController;

/** Camera device and mode choice, with the live status (mode, measured frame
    rate, control ranges, errors). */
class CameraPanel final : public juce::Component,
                          private juce::Timer
{
public:
    explicit CameraPanel (CameraController& controller);

    void resized() override;

    static constexpr int preferredHeight = 128;

private:
    void timerCallback() override;
    void refreshDevices();
    void refreshFormats (const CameraStatus& status);

    CameraController& controller;

    juce::Label heading;
    juce::ComboBox deviceBox;
    juce::ComboBox formatBox;
    juce::Label statusLabel;

    std::vector<CameraDeviceInfo> devices;
    std::vector<CameraFormat> formats;
    std::string shownFormat;
    std::string shownDevice;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CameraPanel)
};

} // namespace pixo
