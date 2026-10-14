#pragma once

namespace pixo
{

/** Native windows that GPU surfaces present into (D4). Message thread only.

    Windows: plain Win32 windows kept free of JUCE headers. A surface window
    starts as a hidden top-level popup, is embedded as a child of a JUCE
    window's peer while it is shown, and is destroyed only after the render
    thread has released its swapchain (see RenderEngine::detachSurface). */

/** Returns nullptr on failure. */
void* createSurfaceWindow();

/** Makes the window a child of `parentWindow` (a JUCE peer's native handle). */
void embedSurfaceWindow (void* window, void* parentWindow);

/** Hides the window and turns it back into an unparented popup, so it can
    outlive its former parent until its swapchain is gone. */
void unembedSurfaceWindow (void* window);

/** Position and size in physical pixels, relative to the parent's client area. */
void setSurfaceWindowBounds (void* window, int x, int y, int width, int height);

void setSurfaceWindowVisible (void* window, bool visible);

void destroySurfaceWindow (void* window);

} // namespace pixo
