// Native windows for GPU surfaces (Windows). Free of JUCE headers, like
// D3D11Backend.cpp.

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include "Render/NativeSurfaceWindow.h"

namespace pixo
{
namespace
{

constexpr const wchar_t* surfaceWindowClass = L"LePixophoneGpuSurface";

HWND toHwnd (void* window)
{
    return static_cast<HWND> (window);
}

LRESULT CALLBACK surfaceWindowProc (HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_ERASEBKGND:
            return 1;                           // the swapchain covers the whole client area

        case WM_PAINT:
            ValidateRect (window, nullptr);     // presented by the render thread, never painted
            return 0;

        case WM_NCHITTEST:
            return HTTRANSPARENT;               // mouse input goes to the JUCE window underneath

        default:
            return DefWindowProcW (window, message, wParam, lParam);
    }
}

bool registerSurfaceWindowClass()
{
    static const bool registered = []
    {
        WNDCLASSEXW windowClass {};
        windowClass.cbSize = sizeof (windowClass);
        windowClass.lpfnWndProc = surfaceWindowProc;
        windowClass.hInstance = GetModuleHandleW (nullptr);
        windowClass.lpszClassName = surfaceWindowClass;

        return RegisterClassExW (&windowClass) != 0
            || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }();

    return registered;
}

void setChildStyle (HWND window, bool child)
{
    auto style = GetWindowLongPtrW (window, GWL_STYLE);
    using Style = decltype (style);

    style &= ~static_cast<Style> (child ? WS_POPUP : WS_CHILD);
    style |= static_cast<Style> (child ? WS_CHILD : WS_POPUP);
    SetWindowLongPtrW (window, GWL_STYLE, style);
}

} // namespace

void* createSurfaceWindow()
{
    if (! registerSurfaceWindowClass())
        return nullptr;

    return CreateWindowExW (0, surfaceWindowClass, L"", WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                            0, 0, 16, 16, nullptr, nullptr, GetModuleHandleW (nullptr), nullptr);
}

void embedSurfaceWindow (void* window, void* parentWindow)
{
    setChildStyle (toHwnd (window), true);
    SetParent (toHwnd (window), toHwnd (parentWindow));
}

void unembedSurfaceWindow (void* window)
{
    ShowWindow (toHwnd (window), SW_HIDE);
    SetParent (toHwnd (window), nullptr);
    setChildStyle (toHwnd (window), false);
}

void setSurfaceWindowBounds (void* window, int x, int y, int width, int height)
{
    SetWindowPos (toHwnd (window), nullptr, x, y, width, height,
                  SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER);
}

void setSurfaceWindowVisible (void* window, bool visible)
{
    ShowWindow (toHwnd (window), visible ? SW_SHOWNA : SW_HIDE);
}

void destroySurfaceWindow (void* window)
{
    DestroyWindow (toHwnd (window));
}

} // namespace pixo
