// Camera capture on Windows through Media Foundation (D7, D17). Free of JUCE
// headers, like the other Win/ files.
//
// The source reader runs in asynchronous mode: frames arrive by callback on
// Media Foundation's worker threads, and nothing ever blocks waiting for a
// frame. A session thread opens the device, applies control changes and
// closes. Closing flushes the reader with a deadline, so a camera that sends
// nothing (e.g. an idle virtual camera) cannot hang the app.

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <strmif.h>         // IAMCameraControl, IAMVideoProcAmp (UVC controls)
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

#include "Camera/CameraSource.h"

namespace pixo
{
namespace
{

using Microsoft::WRL::ComPtr;

constexpr DWORD videoStream = static_cast<DWORD> (MF_SOURCE_READER_FIRST_VIDEO_STREAM);
constexpr auto flushDeadline = std::chrono::milliseconds (1000);

std::string toUtf8 (const wchar_t* text)
{
    if (text == nullptr)
        return {};

    const int size = WideCharToMultiByte (CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string result (static_cast<std::size_t> (std::max (size - 1, 0)), '\0');
    WideCharToMultiByte (CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring toWide (const std::string& text)
{
    const int size = MultiByteToWideChar (CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring result (static_cast<std::size_t> (std::max (size - 1, 0)), L'\0');
    MultiByteToWideChar (CP_UTF8, 0, text.c_str(), -1, result.data(), size);
    return result;
}

std::string lowercase (std::string text)
{
    for (auto& c : text)
        c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));

    return text;
}

std::string describeSubtype (const GUID& subtype)
{
    if (subtype == MFVideoFormat_RGB32) return "RGB32";
    if (subtype == MFVideoFormat_RGB24) return "RGB24";

    // Video subtypes are FOURCCs in Data1.
    char fourcc[5] = {};
    std::memcpy (fourcc, &subtype.Data1, 4);

    for (auto& c : fourcc)
        if (c != 0 && (c < 32 || c > 126))
            c = '?';

    return fourcc;
}

std::string describeFailure (const char* what, HRESULT hr)
{
    char text[160];
    std::snprintf (text, sizeof (text), "%s failed (HRESULT 0x%08lX)", what, static_cast<unsigned long> (hr));
    return text;
}

/** COM for the session thread (Media Foundation wants an MTA). */
struct ComScope
{
    ComScope() : result (CoInitializeEx (nullptr, COINIT_MULTITHREADED)) {}
    ~ComScope() { if (SUCCEEDED (result)) CoUninitialize(); }
    HRESULT result;
};

ComPtr<IMFAttributes> videoCaptureAttributes (UINT32 extraSlots = 0)
{
    ComPtr<IMFAttributes> attributes;

    if (SUCCEEDED (MFCreateAttributes (&attributes, 1 + extraSlots)))
        attributes->SetGUID (MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

    return attributes;
}

/** Default device: a real camera rather than a virtual one (NDI, OBS, ...),
    which may deliver nothing, or the infrared Windows Hello camera. */
const CameraDeviceInfo* chooseDefaultDevice (const std::vector<CameraDeviceInfo>& devices)
{
    const CameraDeviceInfo* best = nullptr;
    int bestScore = 0;

    for (const auto& device : devices)
    {
        const auto link = lowercase (device.id);
        const auto name = lowercase (device.name);
        int score = 0;

        if (link.find ("usb#") != std::string::npos || link.find ("vid_") != std::string::npos)
            score += 2;

        if (name.find ("ir camera") != std::string::npos)
            score -= 3;

        for (const char* virtualHint : { "virtual", "ndi", "obs" })
            if (name.find (virtualHint) != std::string::npos)
                score -= 2;

        if (best == nullptr || score > bestScore)
        {
            best = &device;
            bestScore = score;
        }
    }

    return best;
}

struct NativeMode
{
    CameraFormat format;
    ComPtr<IMFMediaType> type;
};

/** Picks the requested mode, or by default the one closest to the PS3 Eye's
    VGA at 30 fps (the original's M_Reset setting, §3.3). */
const NativeMode* chooseMode (const std::vector<NativeMode>& modes, const CameraRequest& request)
{
    const int wantWidth = request.width > 0 ? request.width : 640;
    const int wantHeight = request.height > 0 ? request.height : 480;
    const double wantFps = request.fps > 0.0 ? request.fps : 30.0;

    const NativeMode* best = nullptr;
    double bestScore = 0.0;

    for (const auto& mode : modes)
    {
        const double score = std::abs (mode.format.width - wantWidth) + std::abs (mode.format.height - wantHeight)
                           + 20.0 * std::abs (mode.format.fps - wantFps)
                           + (mode.format.encoding == "MJPG" ? 0.0 : 0.5);   // tie-break: MJPG takes less USB bandwidth

        if (best == nullptr || score < bestScore)
        {
            best = &mode;
            bestScore = score;
        }
    }

    return best;
}

constexpr long cameraControlProperty (CameraControl control)
{
    return control == CameraControl::exposure ? CameraControl_Exposure : -1;
}

constexpr long procAmpProperty (CameraControl control)
{
    return control == CameraControl::gain         ? VideoProcAmp_Gain
         : control == CameraControl::whiteBalance ? VideoProcAmp_WhiteBalance
                                                  : -1;
}

class MediaFoundationCamera;

/** Receives the asynchronous reader's samples on Media Foundation's worker
    threads and forwards them to the camera until detached. */
class ReaderCallback final : public IMFSourceReaderCallback
{
public:
    explicit ReaderCallback (MediaFoundationCamera& ownerIn) : owner (&ownerIn) {}

    STDMETHODIMP QueryInterface (REFIID iid, void** object) override
    {
        if (object == nullptr)
            return E_POINTER;

        if (iid == __uuidof (IUnknown) || iid == __uuidof (IMFSourceReaderCallback))
        {
            *object = static_cast<IMFSourceReaderCallback*> (this);
            AddRef();
            return S_OK;
        }

        *object = nullptr;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override  { return ++references; }

    STDMETHODIMP_(ULONG) Release() override
    {
        const auto remaining = --references;

        if (remaining == 0)
            delete this;

        return remaining;
    }

    STDMETHODIMP OnReadSample (HRESULT status, DWORD streamIndex, DWORD flags, LONGLONG timestamp, IMFSample* sample) override;

    STDMETHODIMP OnFlush (DWORD) override
    {
        {
            const std::lock_guard<std::mutex> lock (flushLock);
            flushed = true;
        }

        flushDone.notify_all();
        return S_OK;
    }

    STDMETHODIMP OnEvent (DWORD, IMFMediaEvent*) override { return S_OK; }

    bool waitForFlush()
    {
        std::unique_lock<std::mutex> lock (flushLock);
        return flushDone.wait_for (lock, flushDeadline, [this] { return flushed; });
    }

    /** No more forwarding: called before the owner goes away. */
    void detach()
    {
        const std::lock_guard<std::mutex> lock (ownerLock);
        owner = nullptr;
    }

private:
    ~ReaderCallback() = default;

    std::atomic<ULONG> references { 1 };

    std::mutex ownerLock;
    MediaFoundationCamera* owner;

    std::mutex flushLock;
    std::condition_variable flushDone;
    bool flushed = false;
};

class MediaFoundationCamera final : public CameraSource
{
public:
    MediaFoundationCamera()
    {
        startupResult = MFStartup (MF_VERSION, MFSTARTUP_LITE);
    }

    ~MediaFoundationCamera() override
    {
        close();

        if (SUCCEEDED (startupResult))
            MFShutdown();
    }

    std::vector<CameraDeviceInfo> listDevices() override
    {
        std::vector<CameraDeviceInfo> devices;
        auto attributes = videoCaptureAttributes();
        IMFActivate** activates = nullptr;
        UINT32 count = 0;

        if (attributes == nullptr || FAILED (MFEnumDeviceSources (attributes.Get(), &activates, &count)))
            return devices;

        for (UINT32 i = 0; i < count; ++i)
        {
            WCHAR* name = nullptr;
            WCHAR* link = nullptr;
            UINT32 length = 0;

            if (SUCCEEDED (activates[i]->GetAllocatedString (MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &length))
                && SUCCEEDED (activates[i]->GetAllocatedString (MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &link, &length)))
            {
                devices.push_back ({ toUtf8 (link), toUtf8 (name) });
            }

            CoTaskMemFree (name);
            CoTaskMemFree (link);
            activates[i]->Release();
        }

        CoTaskMemFree (activates);
        return devices;
    }

    void open (const CameraRequest& requestIn) override
    {
        close();

        auto request = requestIn;
        const auto devices = listDevices();

        if (request.deviceId.empty())
        {
            const auto* device = chooseDefaultDevice (devices);

            if (device == nullptr)
            {
                setError ("No camera found");
                return;
            }

            request.deviceId = device->id;
        }

        for (const auto& device : devices)
            if (device.id == request.deviceId)
                requestedName = device.name;

        {
            const std::lock_guard<std::mutex> lock (sessionLock);
            stopRequested = false;
        }

        sessionThread = std::thread ([this, request] { runSession (request); });
    }

    void close() override
    {
        {
            const std::lock_guard<std::mutex> lock (sessionLock);
            stopRequested = true;
        }

        sessionWake.notify_all();

        if (sessionThread.joinable())
            sessionThread.join();

        const std::lock_guard<std::mutex> lock (statusLock);
        status.open = false;
        status.measuredFps = 0.0;
    }

    CameraStatus getStatus() const override
    {
        const std::lock_guard<std::mutex> lock (statusLock);
        return status;
    }

    void setControl (CameraControl control, long value) override
    {
        {
            const std::lock_guard<std::mutex> lock (sessionLock);
            auto& pending = pendingControls[static_cast<int> (control)];
            pending.value = value;
            pending.automatic = false;
            pending.dirty = true;
        }

        sessionWake.notify_all();
    }

    void setAutomatic (CameraControl control, bool automatic) override
    {
        {
            const std::lock_guard<std::mutex> lock (sessionLock);
            auto& pending = pendingControls[static_cast<int> (control)];
            pending.automatic = automatic;
            pending.dirty = true;
        }

        sessionWake.notify_all();
    }

    bool takeLatestFrame (CameraFrame& frame) override
    {
        const std::lock_guard<std::mutex> lock (frameLock);

        if (latest.sequence <= frame.sequence || latest.bgra.empty())
            return false;

        std::swap (latest, frame);
        return true;
    }

    /** From ReaderCallback, on a Media Foundation thread. Returns true to
        request the next sample. */
    bool onSample (HRESULT readStatus, DWORD flags, IMFSample* sample)
    {
        if (FAILED (readStatus) || (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_ENDOFSTREAM)) != 0)
        {
            if (readStatus != MF_E_NOTACCEPTING && readStatus != MF_E_SHUTDOWN)
                setError (FAILED (readStatus) ? describeFailure ("Reading a frame", readStatus)
                                              : "Camera stream ended (unplugged?)");
            return false;
        }

        if (sample != nullptr && copySample (sample, work))
        {
            work.sequence = ++frameSequence;

            {
                const std::lock_guard<std::mutex> lock (frameLock);
                std::swap (latest, work);   // `work` gets back an old buffer to refill
            }

            countFrame();
        }

        return true;
    }

    /** From ReaderCallback: request the next sample. */
    void requestNextSample()
    {
        if (reader != nullptr)
            reader->ReadSample (videoStream, 0, nullptr, nullptr, nullptr, nullptr);
    }

private:
    struct PendingControl
    {
        long value = 0;
        bool automatic = false;
        bool dirty = false;
    };

    void setError (const std::string& message)
    {
        const std::lock_guard<std::mutex> lock (statusLock);
        status.error = message;
        status.open = false;
    }

    void runSession (const CameraRequest& request)
    {
        ComScope com;

        auto* callback = new ReaderCallback (*this);
        ComPtr<IMFMediaSource> source;
        std::string error;

        if (! openDevice (request, callback, source, error))
        {
            callback->detach();
            callback->Release();
            reader.Reset();

            if (source != nullptr)
                source->Shutdown();

            setError (error);
            return;
        }

        ComPtr<IAMCameraControl> cameraControl;
        ComPtr<IAMVideoProcAmp> procAmp;
        source.As (&cameraControl);
        source.As (&procAmp);
        readControlRanges (cameraControl.Get(), procAmp.Get());

        // The first request; the callback keeps asking from then on.
        reader->ReadSample (videoStream, 0, nullptr, nullptr, nullptr, nullptr);

        for (;;)
        {
            PendingControl toApply[static_cast<int> (CameraControl::count)];

            {
                std::unique_lock<std::mutex> lock (sessionLock);
                sessionWake.wait (lock, [this]
                {
                    if (stopRequested)
                        return true;

                    for (const auto& pending : pendingControls)
                        if (pending.dirty)
                            return true;

                    return false;
                });

                if (stopRequested)
                    break;

                for (int i = 0; i < static_cast<int> (CameraControl::count); ++i)
                {
                    toApply[i] = pendingControls[i];
                    pendingControls[i].dirty = false;
                }
            }

            applyControls (toApply, cameraControl.Get(), procAmp.Get());
        }

        // Stop: cancel pending reads, wait (bounded) for the flush, release.
        reader->Flush (videoStream);
        callback->waitForFlush();
        callback->detach();
        callback->Release();

        reader.Reset();
        source->Shutdown();
    }

    bool openDevice (const CameraRequest& request, ReaderCallback* callback,
                     ComPtr<IMFMediaSource>& source, std::string& error)
    {
        auto attributes = videoCaptureAttributes (1);
        const auto link = toWide (request.deviceId);
        attributes->SetString (MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, link.c_str());

        auto hr = MFCreateDeviceSource (attributes.Get(), &source);

        if (FAILED (hr))
        {
            error = describeFailure ("Opening the camera", hr);
            return false;
        }

        // The device's native modes, through its stream's media type handler.
        ComPtr<IMFPresentationDescriptor> presentation;
        ComPtr<IMFStreamDescriptor> stream;
        ComPtr<IMFMediaTypeHandler> handler;
        BOOL selected = FALSE;

        if (FAILED (hr = source->CreatePresentationDescriptor (&presentation))
            || FAILED (hr = presentation->GetStreamDescriptorByIndex (0, &selected, &stream))
            || FAILED (hr = stream->GetMediaTypeHandler (&handler)))
        {
            error = describeFailure ("Reading the camera modes", hr);
            return false;
        }

        std::vector<NativeMode> modes;
        DWORD typeCount = 0;
        handler->GetMediaTypeCount (&typeCount);

        for (DWORD i = 0; i < typeCount; ++i)
        {
            NativeMode mode;

            if (FAILED (handler->GetMediaTypeByIndex (i, &mode.type)))
                continue;

            UINT32 width = 0, height = 0, numerator = 0, denominator = 1;
            GUID subtype {};
            MFGetAttributeSize (mode.type.Get(), MF_MT_FRAME_SIZE, &width, &height);
            MFGetAttributeRatio (mode.type.Get(), MF_MT_FRAME_RATE, &numerator, &denominator);
            mode.type->GetGUID (MF_MT_SUBTYPE, &subtype);

            mode.format.width = static_cast<int> (width);
            mode.format.height = static_cast<int> (height);
            mode.format.fps = denominator != 0 ? static_cast<double> (numerator) / denominator : 0.0;
            mode.format.encoding = describeSubtype (subtype);
            modes.push_back (std::move (mode));
        }

        const auto* chosen = chooseMode (modes, request);

        if (chosen == nullptr)
        {
            error = "The camera offers no video modes";
            return false;
        }

        if (FAILED (hr = handler->SetCurrentMediaType (chosen->type.Get())))
        {
            error = describeFailure ("Selecting the camera mode", hr);
            return false;
        }

        // An asynchronous reader that decodes/converts to RGB32.
        ComPtr<IMFAttributes> readerAttributes;
        MFCreateAttributes (&readerAttributes, 2);
        readerAttributes->SetUINT32 (MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        readerAttributes->SetUnknown (MF_SOURCE_READER_ASYNC_CALLBACK, callback);

        ComPtr<IMFMediaType> output;
        MFCreateMediaType (&output);
        output->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Video);
        output->SetGUID (MF_MT_SUBTYPE, MFVideoFormat_RGB32);

        if (FAILED (hr = MFCreateSourceReaderFromMediaSource (source.Get(), readerAttributes.Get(), &reader))
            || FAILED (hr = reader->SetCurrentMediaType (videoStream, nullptr, output.Get())))
        {
            error = describeFailure ("Setting up frame conversion", hr);
            return false;
        }

        ComPtr<IMFMediaType> current;
        reader->GetCurrentMediaType (videoStream, &current);
        UINT32 width = 0, height = 0;
        MFGetAttributeSize (current.Get(), MF_MT_FRAME_SIZE, &width, &height);
        frameWidth = static_cast<int> (width);
        frameHeight = static_cast<int> (height);

        // RGB32 is bottom-up unless the type says otherwise.
        UINT32 stride = 0;
        defaultStride = SUCCEEDED (current->GetUINT32 (MF_MT_DEFAULT_STRIDE, &stride))
                          ? static_cast<LONG> (stride)
                          : -static_cast<LONG> (width * 4);

        {
            const std::lock_guard<std::mutex> lock (frameLock);
            frameSequence = latest.sequence;
        }

        fpsWindowStart = std::chrono::steady_clock::now();
        fpsFrames = 0;

        {
            const std::lock_guard<std::mutex> lock (statusLock);
            status = CameraStatus {};
            status.open = true;
            status.format = chosen->format;
            status.deviceId = request.deviceId;
            status.deviceName = requestedName;

            for (const auto& mode : modes)
                status.formats.push_back (mode.format);
        }

        // Whatever the controls were set to before this (re)open applies again.
        {
            const std::lock_guard<std::mutex> lock (sessionLock);

            for (auto& pending : pendingControls)
                pending.dirty = true;
        }

        return true;
    }

    bool copySample (IMFSample* sample, CameraFrame& frame)
    {
        ComPtr<IMFMediaBuffer> buffer;

        if (FAILED (sample->ConvertToContiguousBuffer (&buffer)) || frameWidth <= 0 || frameHeight <= 0)
            return false;

        frame.width = frameWidth;
        frame.height = frameHeight;
        frame.bgra.resize (static_cast<std::size_t> (frameWidth) * static_cast<std::size_t> (frameHeight) * 4);

        const auto rowBytes = static_cast<std::size_t> (frameWidth) * 4;
        ComPtr<IMF2DBuffer> buffer2d;

        if (SUCCEEDED (buffer.As (&buffer2d)))
        {
            BYTE* topRow = nullptr;
            LONG pitch = 0;

            if (FAILED (buffer2d->Lock2D (&topRow, &pitch)))
                return false;

            for (int y = 0; y < frameHeight; ++y)
                std::memcpy (frame.bgra.data() + static_cast<std::size_t> (y) * rowBytes, topRow + static_cast<LONG_PTR> (y) * pitch, rowBytes);

            buffer2d->Unlock2D();
            return true;
        }

        BYTE* data = nullptr;
        DWORD maxLength = 0, length = 0;

        if (FAILED (buffer->Lock (&data, &maxLength, &length)))
            return false;

        const auto absStride = static_cast<std::size_t> (std::abs (defaultStride));
        const bool complete = length >= absStride * static_cast<std::size_t> (frameHeight);

        if (complete)
        {
            const BYTE* topRow = defaultStride < 0 ? data + absStride * static_cast<std::size_t> (frameHeight - 1) : data;

            for (int y = 0; y < frameHeight; ++y)
                std::memcpy (frame.bgra.data() + static_cast<std::size_t> (y) * rowBytes,
                             topRow + static_cast<LONG_PTR> (y) * defaultStride, rowBytes);
        }

        buffer->Unlock();
        return complete;
    }

    void countFrame()
    {
        ++fpsFrames;
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration<double> (now - fpsWindowStart).count();

        if (elapsed < 1.0)
            return;

        const std::lock_guard<std::mutex> lock (statusLock);
        status.measuredFps = fpsFrames / elapsed;
        fpsFrames = 0;
        fpsWindowStart = now;
    }

    void readControlRanges (IAMCameraControl* cameraControl, IAMVideoProcAmp* procAmp)
    {
        const std::lock_guard<std::mutex> lock (statusLock);

        for (int i = 0; i < static_cast<int> (CameraControl::count); ++i)
        {
            const auto control = static_cast<CameraControl> (i);
            auto& range = status.ranges[i];
            long minValue = 0, maxValue = 0, step = 1, defaultValue = 0, caps = 0;
            HRESULT hr = E_NOTIMPL;

            if (cameraControlProperty (control) >= 0 && cameraControl != nullptr)
            {
                hr = cameraControl->GetRange (cameraControlProperty (control), &minValue, &maxValue, &step, &defaultValue, &caps);
                range.canBeAutomatic = (caps & CameraControl_Flags_Auto) != 0;
            }
            else if (procAmpProperty (control) >= 0 && procAmp != nullptr)
            {
                hr = procAmp->GetRange (procAmpProperty (control), &minValue, &maxValue, &step, &defaultValue, &caps);
                range.canBeAutomatic = (caps & VideoProcAmp_Flags_Auto) != 0;
            }

            range.supported = SUCCEEDED (hr) && maxValue > minValue;
            range.minValue = minValue;
            range.maxValue = maxValue;
            range.step = std::max (step, 1L);
            range.defaultValue = defaultValue;
        }
    }

    static void applyControls (const PendingControl* toApply, IAMCameraControl* cameraControl, IAMVideoProcAmp* procAmp)
    {
        for (int i = 0; i < static_cast<int> (CameraControl::count); ++i)
        {
            if (! toApply[i].dirty)
                continue;

            const auto control = static_cast<CameraControl> (i);

            if (cameraControlProperty (control) >= 0 && cameraControl != nullptr)
                cameraControl->Set (cameraControlProperty (control), toApply[i].value,
                                    toApply[i].automatic ? CameraControl_Flags_Auto : CameraControl_Flags_Manual);
            else if (procAmpProperty (control) >= 0 && procAmp != nullptr)
                procAmp->Set (procAmpProperty (control), toApply[i].value,
                              toApply[i].automatic ? VideoProcAmp_Flags_Auto : VideoProcAmp_Flags_Manual);
        }
    }

    HRESULT startupResult = E_FAIL;
    std::string requestedName;

    std::thread sessionThread;
    std::mutex sessionLock;                 // guards stopRequested and pendingControls
    std::condition_variable sessionWake;
    bool stopRequested = false;
    PendingControl pendingControls[static_cast<int> (CameraControl::count)];

    // Set by the session thread before the first read, then used by callbacks.
    ComPtr<IMFSourceReader> reader;
    int frameWidth = 0;
    int frameHeight = 0;
    LONG defaultStride = 0;

    // Callback threads only (Media Foundation serialises a reader's callbacks).
    CameraFrame work;
    std::uint64_t frameSequence = 0;
    std::chrono::steady_clock::time_point fpsWindowStart;
    int fpsFrames = 0;

    std::mutex frameLock;
    CameraFrame latest;

    mutable std::mutex statusLock;
    CameraStatus status;
};

STDMETHODIMP ReaderCallback::OnReadSample (HRESULT status, DWORD, DWORD flags, LONGLONG, IMFSample* sample)
{
    const std::lock_guard<std::mutex> lock (ownerLock);

    if (owner != nullptr && owner->onSample (status, flags, sample))
        owner->requestNextSample();

    return S_OK;
}

} // namespace

std::unique_ptr<CameraSource> createPlatformCameraSource()
{
    return std::make_unique<MediaFoundationCamera>();
}

} // namespace pixo
