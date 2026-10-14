#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pixo
{

/** One capture mode a device offers. */
struct CameraFormat
{
    int width = 0;
    int height = 0;
    double fps = 0.0;
    std::string encoding;   // "MJPG", "YUY2", "NV12", ...

    std::string describe() const;
};

struct CameraDeviceInfo
{
    std::string id;         // stable per device (Windows: the symbolic link)
    std::string name;
};

/** Camera controls, in the device's own units (D17). Exposure and gain are
    played live; the rest are there to switch off the automatics. */
enum class CameraControl
{
    exposure,
    gain,
    whiteBalance,
    count
};

struct CameraControlRange
{
    bool supported = false;
    long minValue = 0;
    long maxValue = 0;
    long step = 1;
    long defaultValue = 0;
    bool canBeAutomatic = false;
};

/** A captured frame: 8-bit BGRA (alpha undefined), top row first. */
struct CameraFrame
{
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> bgra;
    std::uint64_t sequence = 0;     // increases by one per captured frame
};

/** What to open. Zero width/height/fps picks the default (VGA at 30 fps or
    the closest mode, like the PS3 Eye in the original). */
struct CameraRequest
{
    std::string deviceId;           // empty: the first device
    int width = 0;
    int height = 0;
    double fps = 0.0;
};

struct CameraStatus
{
    bool open = false;
    std::string deviceId;               // the device actually opened (also when chosen by default)
    std::string deviceName;
    CameraFormat format;
    std::vector<CameraFormat> formats;  // modes of the open device
    CameraControlRange ranges[static_cast<int> (CameraControl::count)];
    double measuredFps = 0.0;
    std::string error;                  // empty while healthy
};

/** A live camera (D7). Capture runs on its own thread; opening is
    asynchronous (watch getStatus()). Control changes may come from any thread
    and are applied by the capture thread between frames, latest value wins
    (D17). */
class CameraSource
{
public:
    virtual ~CameraSource() = default;

    virtual std::vector<CameraDeviceInfo> listDevices() = 0;

    /** Starts (or restarts) capture. Returns immediately. */
    virtual void open (const CameraRequest& request) = 0;
    virtual void close() = 0;

    virtual CameraStatus getStatus() const = 0;

    virtual void setControl (CameraControl control, long value) = 0;
    virtual void setAutomatic (CameraControl control, bool automatic) = 0;

    /** Render thread: swaps in the newest frame if it is newer than
        `frame.sequence`. Returns false when there is nothing new. The
        caller's buffer is recycled, so steady state allocates nothing. */
    virtual bool takeLatestFrame (CameraFrame& frame) = 0;
};

/** This platform's camera backend (Windows: Media Foundation). */
std::unique_ptr<CameraSource> createPlatformCameraSource();

} // namespace pixo
