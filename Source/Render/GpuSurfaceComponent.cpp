#include "Render/GpuSurfaceComponent.h"

#include "Render/NativeSurfaceWindow.h"

namespace pixo
{

/** Keeps the native window parented to the component's peer and aligned with
    the component's bounds (the job juce::HWNDComponent does, minus destroying
    the window, which must wait for the render thread). */
class GpuSurfaceComponent::Host final : public juce::ComponentMovementWatcher
{
public:
    Host (GpuSurfaceComponent& ownerIn, void* windowIn)
        : juce::ComponentMovementWatcher (&ownerIn), owner (ownerIn), window (windowIn)
    {
        if (owner.isShowing())
            componentPeerChanged();
    }

    ~Host() override
    {
        unembedSurfaceWindow (window);
    }

    void* getWindow() const noexcept { return window; }

    void componentMovedOrResized (bool /*wasMoved*/, bool /*wasResized*/) override
    {
        if (currentPeer == nullptr)
            return;

        const auto area = (currentPeer->getAreaCoveredBy (owner).toFloat()
                           * currentPeer->getPlatformScaleFactor()).getSmallestIntegerContainer();

        setSurfaceWindowBounds (window, area.getX(), area.getY(), area.getWidth(), area.getHeight());
    }

    void componentPeerChanged() override
    {
        auto* peer = owner.getPeer();

        if (peer != currentPeer)
        {
            if (currentPeer != nullptr)
                unembedSurfaceWindow (window);

            currentPeer = peer;

            if (currentPeer != nullptr)
            {
                embedSurfaceWindow (window, currentPeer->getNativeHandle());
                componentMovedOrResized (true, true);
            }
        }

        const bool showing = currentPeer != nullptr && owner.isShowing();
        setSurfaceWindowVisible (window, showing);

        if (showing)
            owner.windowEmbedded (window);
    }

    void componentVisibilityChanged() override
    {
        componentPeerChanged();
    }

    using juce::ComponentMovementWatcher::componentMovedOrResized;
    using juce::ComponentMovementWatcher::componentVisibilityChanged;

private:
    GpuSurfaceComponent& owner;
    void* window;
    juce::ComponentPeer* currentPeer = nullptr;
};

//==============================================================================
GpuSurfaceComponent::GpuSurfaceComponent (RenderEngine& engineIn, SurfaceRole roleIn)
    : engine (engineIn), role (roleIn)
{
    setOpaque (true);
    setInterceptsMouseClicks (false, false);

    if (auto* window = createSurfaceWindow())
        host = std::make_unique<Host> (*this, window);
    else
        juce::Logger::writeToLog ("GpuSurfaceComponent: could not create a native surface window");
}

GpuSurfaceComponent::~GpuSurfaceComponent()
{
    if (host == nullptr)
        return;

    auto* window = host->getWindow();
    host.reset();   // stop tracking; the window is hidden and unparented

    if (surfaceId != 0)
        engine.detachSurface (surfaceId, [window] { destroySurfaceWindow (window); });
    else
        destroySurfaceWindow (window);
}

void GpuSurfaceComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);   // only visible before the first frame
}

void GpuSurfaceComponent::windowEmbedded (void* nativeWindow)
{
    if (surfaceId == 0)
        surfaceId = engine.attachSurface (nativeWindow, role);
}

} // namespace pixo
