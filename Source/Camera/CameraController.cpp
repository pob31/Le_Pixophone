#include "Camera/CameraController.h"

#include "Core/TxMapping.h"

namespace pixo
{
namespace
{

constexpr const char* deviceKey = "cameraDevice";
constexpr const char* formatKey = "cameraFormat";   // "width height fps"

} // namespace

CameraController::CameraController (CameraSource& cameraIn, ParameterStore& paramsIn, juce::PropertiesFile& settingsIn)
    : camera (cameraIn), params (paramsIn), settings (settingsIn)
{
    request.deviceId = settings.getValue (deviceKey).toStdString();

    const auto format = juce::StringArray::fromTokens (settings.getValue (formatKey), false);

    if (format.size() == 3)
    {
        request.width = format[0].getIntValue();
        request.height = format[1].getIntValue();
        request.fps = format[2].getDoubleValue();
    }

    // A remembered device that is gone falls back to the first one.
    if (! request.deviceId.empty())
    {
        bool present = false;

        for (const auto& device : camera.listDevices())
            present = present || device.id == request.deviceId;

        if (! present)
            request = CameraRequest {};
    }

    startTimerHz (30);
}

CameraController::~CameraController()
{
    stopTimer();
    camera.close();
}

std::string CameraController::getDeviceId() const
{
    const auto status = camera.getStatus();
    return status.open ? status.deviceId : request.deviceId;
}

void CameraController::selectDevice (const std::string& deviceId)
{
    request = CameraRequest {};
    request.deviceId = deviceId;
    saveRequest();
    reopen();
}

void CameraController::selectFormat (const CameraFormat& format)
{
    request.width = format.width;
    request.height = format.height;
    request.fps = format.fps;
    saveRequest();
    reopen();
}

void CameraController::saveRequest()
{
    settings.setValue (deviceKey, juce::String (request.deviceId));
    settings.setValue (formatKey, juce::String (request.width) + " " + juce::String (request.height) + " "
                                    + juce::String (request.fps));
    settings.saveIfNeeded();
}

void CameraController::reopen()
{
    if (wantOpen)
    {
        camera.open (request);
        wasOpen = false;    // controls are re-sent once the new stream is up
    }
}

void CameraController::timerCallback()
{
    const bool on = params.get (ParamId::cameraOn) > 0.5f;

    if (on != wantOpen)
    {
        wantOpen = on;

        if (on)
            camera.open (request);
        else
            camera.close();

        wasOpen = false;
    }

    const auto status = camera.getStatus();

    if (status.open && ! wasOpen)
    {
        // The device's choice becomes the remembered one (incl. default picks).
        if (request.width == 0)
        {
            request.width = status.format.width;
            request.height = status.format.height;
            request.fps = status.format.fps;
        }

        sendControls (true);
    }
    else if (status.open)
    {
        sendControls (false);
    }

    wasOpen = status.open;
}

void CameraController::sendControls (bool force)
{
    const auto status = camera.getStatus();

    const auto send = [&] (CameraControl control, ParamId valueParam, ParamId autoParam, float& sentValue, int& sentAuto)
    {
        const auto& range = status.ranges[static_cast<int> (control)];

        if (! range.supported)
            return;

        const int automatic = params.get (autoParam) > 0.5f ? 1 : 0;
        const float value = params.get (valueParam);

        if (automatic != 0)
        {
            if (force || automatic != sentAuto)
                camera.setAutomatic (control, true);
        }
        else if (force || automatic != sentAuto || value != sentValue)
        {
            camera.setControl (control, txmap::toDeviceRange (value, range.minValue, range.maxValue, range.step));
        }

        sentAuto = automatic;
        sentValue = value;
    };

    send (CameraControl::exposure, ParamId::exposure, ParamId::exposureAuto, sentExposure, sentExposureAuto);
    send (CameraControl::gain, ParamId::gain, ParamId::gainAuto, sentGain, sentGainAuto);

    // White balance has no value parameter: only its automatic is switched.
    const auto& whiteBalance = status.ranges[static_cast<int> (CameraControl::whiteBalance)];
    const int whiteBalanceAuto = params.get (ParamId::whiteBalanceAuto) > 0.5f ? 1 : 0;

    if (whiteBalance.supported && (force || whiteBalanceAuto != sentWhiteBalanceAuto))
    {
        if (whiteBalanceAuto != 0)
            camera.setAutomatic (CameraControl::whiteBalance, true);
        else
            camera.setControl (CameraControl::whiteBalance, whiteBalance.defaultValue);
    }

    sentWhiteBalanceAuto = whiteBalanceAuto;
}

} // namespace pixo
