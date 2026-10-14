// Windows GPU backend: one D3D11 device, plus one flip-model DXGI swapchain
// per native surface window. Deliberately free of JUCE headers, so that
// <windows.h> and the D3D headers don't collide with JUCE's own Windows
// includes.

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdio>
#include <iterator>
#include <utility>

#include "Render/GpuBackend.h"

namespace pixo
{
namespace
{

using Microsoft::WRL::ComPtr;

// Gamma-encoded 8-bit BGRA, no sRGB conversion: GEM blended in this space,
// and the faithful port (D14) blends the same way.
constexpr DXGI_FORMAT backBufferFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
constexpr sg_pixel_format sokolBackBufferFormat = SG_PIXELFORMAT_BGRA8;

std::string describeFailure (const char* what, HRESULT hr)
{
    char text[128];
    std::snprintf (text, sizeof (text), "%s failed (HRESULT 0x%08lX)", what, static_cast<unsigned long> (hr));
    return text;
}

class D3D11Swapchain final : public GpuSwapchain
{
public:
    D3D11Swapchain (ID3D11Device* deviceIn, ID3D11DeviceContext* contextIn,
                    HWND windowIn, ComPtr<IDXGISwapChain1> swapchainIn)
        : device (deviceIn), context (contextIn), window (windowIn), swapchain (std::move (swapchainIn))
    {
    }

    bool prepare() override
    {
        RECT client {};
        if (! GetClientRect (window, &client))
            return false;

        const auto newWidth  = static_cast<int> (client.right - client.left);
        const auto newHeight = static_cast<int> (client.bottom - client.top);

        if (newWidth <= 0 || newHeight <= 0)
            return false;

        if (renderView != nullptr && newWidth == width && newHeight == height)
            return true;

        // ResizeBuffers fails while any reference to a back buffer remains,
        // including the context's output-merger binding from the last pass.
        renderView.Reset();
        context->OMSetRenderTargets (0, nullptr, nullptr);

        if (FAILED (swapchain->ResizeBuffers (0, static_cast<UINT> (newWidth), static_cast<UINT> (newHeight),
                                              DXGI_FORMAT_UNKNOWN, 0)))
            return false;

        ComPtr<ID3D11Texture2D> backBuffer;

        if (FAILED (swapchain->GetBuffer (0, IID_PPV_ARGS (&backBuffer)))
            || FAILED (device->CreateRenderTargetView (backBuffer.Get(), nullptr, &renderView)))
        {
            renderView.Reset();
            return false;
        }

        width = newWidth;
        height = newHeight;
        return true;
    }

    sg_swapchain describe() const override
    {
        sg_swapchain result {};
        result.width = width;
        result.height = height;
        result.sample_count = 1;
        result.color_format = sokolBackBufferFormat;
        result.depth_format = SG_PIXELFORMAT_NONE;
        result.d3d11.render_view = renderView.Get();
        return result;
    }

    PresentResult present (bool waitForVSync) override
    {
        const auto hr = swapchain->Present (waitForVSync ? 1u : 0u, 0);

        if (hr == DXGI_STATUS_OCCLUDED)
            return PresentResult::occluded;

        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
            return PresentResult::deviceLost;

        return PresentResult::presented;
    }

private:
    ID3D11Device* device;
    ID3D11DeviceContext* context;
    HWND window;
    ComPtr<IDXGISwapChain1> swapchain;
    ComPtr<ID3D11RenderTargetView> renderView;
    int width = 0;
    int height = 0;
};

/** The adapter Windows ranks first for performance, or nullptr when the
    query isn't available (pre-1803 Windows) or fails. */
ComPtr<IDXGIAdapter1> findHighPerformanceAdapter()
{
    ComPtr<IDXGIFactory6> factory6;

    if (FAILED (CreateDXGIFactory2 (0, IID_PPV_ARGS (&factory6))))
        return nullptr;

    ComPtr<IDXGIAdapter1> adapter;

    if (FAILED (factory6->EnumAdapterByGpuPreference (0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS (&adapter))))
        return nullptr;

    return adapter;
}

class D3D11Backend final : public GpuBackend
{
public:
    bool initialise (std::string& error)
    {
        const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };

        // D22: the high-performance GPU (the discrete one on hybrid laptops),
        // falling back to the system default adapter.
        const auto preferredAdapter = findHighPerformanceAdapter();

        const auto createDevice = [&] (UINT flags)
        {
            if (preferredAdapter != nullptr)
            {
                const auto hr = D3D11CreateDevice (preferredAdapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags,
                                                   levels, static_cast<UINT> (std::size (levels)), D3D11_SDK_VERSION,
                                                   &device, nullptr, &context);
                if (SUCCEEDED (hr))
                    return hr;
            }

            return D3D11CreateDevice (nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                      levels, static_cast<UINT> (std::size (levels)), D3D11_SDK_VERSION,
                                      &device, nullptr, &context);
        };

        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

       #if defined (_DEBUG)
        auto hr = createDevice (flags | D3D11_CREATE_DEVICE_DEBUG);

        if (FAILED (hr))    // the debug layer is an optional Windows feature
            hr = createDevice (flags);
       #else
        auto hr = createDevice (flags);
       #endif

        if (FAILED (hr))
        {
            error = describeFailure ("D3D11CreateDevice", hr);
            return false;
        }

        ComPtr<IDXGIDevice> dxgiDevice;
        ComPtr<IDXGIAdapter> adapter;

        if (FAILED (hr = device.As (&dxgiDevice))
            || FAILED (hr = dxgiDevice->GetAdapter (&adapter))
            || FAILED (hr = adapter->GetParent (IID_PPV_ARGS (&factory))))
        {
            error = describeFailure ("DXGI factory lookup", hr);
            return false;
        }

        DXGI_ADAPTER_DESC adapterDesc {};

        if (SUCCEEDED (adapter->GetDesc (&adapterDesc)))
        {
            char utf8[256] {};
            WideCharToMultiByte (CP_UTF8, 0, adapterDesc.Description, -1, utf8, sizeof (utf8) - 1, nullptr, nullptr);
            adapterName = utf8;
        }

        return true;
    }

    const char* getName() const override { return "Direct3D 11"; }

    std::string getAdapterName() const override { return adapterName; }

    std::string releaseDevice() override
    {
        if (device == nullptr)
            return {};

        context->ClearState();
        context->Flush();

        factory.Reset();
        const auto contextRefs = context.Reset();
        const auto deviceRefs = device.Reset();

        char text[128];
        std::snprintf (text, sizeof (text), "D3D11 device released (references left: context %lu, device %lu)",
                       static_cast<unsigned long> (contextRefs), static_cast<unsigned long> (deviceRefs));
        return text;
    }

    sg_environment describeEnvironment() const override
    {
        sg_environment environment {};
        environment.defaults.color_format = sokolBackBufferFormat;
        environment.defaults.depth_format = SG_PIXELFORMAT_NONE;    // 2D compositing, no depth buffer
        environment.defaults.sample_count = 1;
        environment.d3d11.device = device.Get();
        environment.d3d11.device_context = context.Get();
        return environment;
    }

    std::unique_ptr<GpuSwapchain> createSwapchain (void* nativeWindow, std::string& error) override
    {
        auto* window = static_cast<HWND> (nativeWindow);

        DXGI_SWAP_CHAIN_DESC1 desc {};
        desc.Format = backBufferFormat;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

        ComPtr<IDXGISwapChain1> swapchain;
        auto hr = factory->CreateSwapChainForHwnd (device.Get(), window, &desc, nullptr, nullptr, &swapchain);

        if (FAILED (hr))
        {
            // Flip model needs Windows 10; fall back to the blt model.
            desc.BufferCount = 1;
            desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
            hr = factory->CreateSwapChainForHwnd (device.Get(), window, &desc, nullptr, nullptr, &swapchain);
        }

        if (FAILED (hr))
        {
            error = describeFailure ("CreateSwapChainForHwnd", hr);
            return nullptr;
        }

        // Fullscreen is a borderless window (OutputWindow), never DXGI's
        // exclusive mode: keep Alt+Enter from switching modes behind our back.
        factory->MakeWindowAssociation (window, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);

        return std::make_unique<D3D11Swapchain> (device.Get(), context.Get(), window, std::move (swapchain));
    }

    void flushReleases() override
    {
        context->OMSetRenderTargets (0, nullptr, nullptr);
        context->Flush();
    }

private:
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIFactory2> factory;
    std::string adapterName;
};

} // namespace

std::unique_ptr<GpuBackend> createPlatformGpuBackend (std::string& error)
{
    auto backend = std::make_unique<D3D11Backend>();

    if (! backend->initialise (error))
        return nullptr;

    return backend;
}

} // namespace pixo
