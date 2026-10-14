#include <juce_gui_basics/juce_gui_basics.h>

#include "App/ControlWindow.h"
#include "App/OutputWindow.h"
#include "Render/RenderEngine.h"

namespace pixo
{

class PixophoneApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        // %APPDATA%\Le Pixophone\pixophone.log on Windows. Every juce::Logger
        // message lands there, including sokol_gfx's and the render thread's.
        logger.reset (juce::FileLogger::createDefaultAppLogger ("Le Pixophone", "pixophone.log",
                                                               getApplicationName() + " " + getApplicationVersion()));
        juce::Logger::setCurrentLogger (logger.get());

        renderEngine = std::make_unique<RenderEngine>();
        renderEngine->start();

        outputWindow = std::make_unique<OutputWindow> (*renderEngine);
        controlWindow = std::make_unique<ControlWindow> (*renderEngine, *outputWindow);
    }

    void systemRequestedQuit() override
    {
        if (quitRequested)
            return;

        quitRequested = true;

        // Quit in two steps: the render thread releases every swapchain and
        // the GPU device while this thread still pumps messages (DXGI needs
        // it, see RenderEngine), then the message loop ends.
        renderEngine->stopAsync ([] { juce::JUCEApplicationBase::quit(); });

        // Safety net, should the render thread be stuck.
        juce::Timer::callAfterDelay (3000, [] { juce::JUCEApplicationBase::quit(); });
    }

    void shutdown() override
    {
        // The engine has stopped: closing the windows destroys their surface
        // windows directly.
        controlWindow.reset();
        outputWindow.reset();
        renderEngine.reset();

        juce::Logger::writeToLog ("Shutdown complete");
        juce::Logger::setCurrentLogger (nullptr);
        logger.reset();
    }

    void anotherInstanceStarted (const juce::String&) override {}

private:
    std::unique_ptr<juce::FileLogger> logger;
    std::unique_ptr<RenderEngine> renderEngine;
    std::unique_ptr<OutputWindow> outputWindow;
    std::unique_ptr<ControlWindow> controlWindow;
    bool quitRequested = false;
};

} // namespace pixo

START_JUCE_APPLICATION (pixo::PixophoneApplication)
