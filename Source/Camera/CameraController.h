#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

#include "Camera/CameraSource.h"
#include "Core/Params.h"

namespace pixo
{

/** Drives the CameraSource from the parameters (message thread).

    - `cameraOn` opens and closes the camera.
    - Exposure and gain (0–127) are mapped onto the device's control ranges
      and sent only when they change, so live sweeps from a controller cost
      little (D17). The `*Auto` parameters switch the device automatics.
    - The chosen device and mode are remembered in the settings. */
class CameraController final : private juce::Timer
{
public:
    CameraController (CameraSource& camera, ParameterStore& params, juce::PropertiesFile& settings);
    ~CameraController() override;

    std::vector<CameraDeviceInfo> listDevices() { return camera.listDevices(); }
    CameraStatus getStatus() const { return camera.getStatus(); }
    /** The open device, else the requested one (empty: default). */
    std::string getDeviceId() const;

    /** Reopens with another device (default mode) or mode. */
    void selectDevice (const std::string& deviceId);
    void selectFormat (const CameraFormat& format);

private:
    void timerCallback() override;
    void reopen();
    void saveRequest();
    void sendControls (bool force);

    CameraSource& camera;
    ParameterStore& params;
    juce::PropertiesFile& settings;
    CameraRequest request;

    bool wantOpen = false;
    bool wasOpen = false;
    float sentExposure = -1.0f;
    float sentGain = -1.0f;
    int sentExposureAuto = -1;
    int sentGainAuto = -1;
    int sentWhiteBalanceAuto = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CameraController)
};

} // namespace pixo
