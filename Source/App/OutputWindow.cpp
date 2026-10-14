#include "App/OutputWindow.h"

namespace pixo
{
namespace
{

/** A projector is usually a secondary display: prefer the first non-main one. */
int defaultOutputDisplay()
{
    const auto& displays = juce::Desktop::getInstance().getDisplays().displays;

    for (int i = 0; i < displays.size(); ++i)
        if (! displays.getReference (i).isMain)
            return i;

    return 0;
}

} // namespace

OutputWindow::OutputWindow (RenderEngine& engine)
    : surface (engine, SurfaceRole::output),
      displayIndex (defaultOutputDisplay())
{
    setName ("Le Pixophone - Output");
    setOpaque (true);
    setWantsKeyboardFocus (true);
    addAndMakeVisible (surface);

    addToDesktop (juce::ComponentPeer::windowAppearsOnTaskbar);
    applyPlacement();
    setVisible (true);
}

OutputWindow::~OutputWindow() = default;

void OutputWindow::setDisplay (int newDisplayIndex)
{
    displayIndex = newDisplayIndex;
    applyPlacement();
}

void OutputWindow::setFullscreen (bool shouldBeFullscreen)
{
    fullscreen = shouldBeFullscreen;
    applyPlacement();
}

void OutputWindow::applyPlacement()
{
    const auto& displays = juce::Desktop::getInstance().getDisplays().displays;

    if (displays.isEmpty())
        return;

    displayIndex = juce::jlimit (0, displays.size() - 1, displayIndex);
    const auto& display = displays.getReference (displayIndex);

    if (fullscreen)
    {
        setAlwaysOnTop (true);
        setBounds (display.logicalBounds.getSmallestIntegerContainer());
        setMouseCursor (juce::MouseCursor::NoCursor);
    }
    else
    {
        setAlwaysOnTop (false);
        setBounds (juce::Rectangle<int> (800, 600).withCentre (display.userBounds.getCentre().roundToInt()));
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }
}

void OutputWindow::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void OutputWindow::resized()
{
    surface.setBounds (getLocalBounds());
}

void OutputWindow::mouseDown (const juce::MouseEvent& event)
{
    if (! fullscreen)
        dragger.startDraggingComponent (this, event);
}

void OutputWindow::mouseDrag (const juce::MouseEvent& event)
{
    if (! fullscreen)
        dragger.dragComponent (this, event, nullptr);
}

bool OutputWindow::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && fullscreen)
    {
        setFullscreen (false);

        if (onPlacementChanged)
            onPlacementChanged();

        return true;
    }

    return false;
}

} // namespace pixo
