#include <juce_gui_basics/juce_gui_basics.h>

#include "App/ControlWindow.h"
#include "App/OutputWindow.h"
#include "Core/Params.h"
#include "Media/MediaLibrary.h"
#include "Render/RenderEngine.h"

namespace pixo
{
namespace
{

/** `--set key=value` (repeatable): sets a parameter by its ParamDef key at
    start-up. For scripted checks; unknown keys are logged and ignored. */
void applyCommandLine (const juce::String& commandLine, ParameterStore& params)
{
    const auto tokens = juce::StringArray::fromTokens (commandLine, true);

    for (int i = 0; i + 1 < tokens.size(); ++i)
    {
        if (tokens[i] != "--set")
            continue;

        const auto assignment = tokens[++i].unquoted();
        const auto key = assignment.upToFirstOccurrenceOf ("=", false, false);
        const auto value = assignment.fromFirstOccurrenceOf ("=", false, false);
        bool found = false;

        for (std::size_t p = 0; p < numParams; ++p)
        {
            const auto id = static_cast<ParamId> (p);

            if (key == paramDef (id).key)
            {
                params.set (id, value.getFloatValue());
                found = true;
            }
        }

        juce::Logger::writeToLog ((found ? "Command line: " : "Command line: unknown parameter ") + assignment);
    }
}

} // namespace

class PixophoneApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String& commandLine) override
    {
        // %APPDATA%\Le Pixophone\pixophone.log on Windows. Every juce::Logger
        // message lands there, including sokol_gfx's and the render thread's.
        logger.reset (juce::FileLogger::createDefaultAppLogger ("Le Pixophone", "pixophone.log",
                                                               getApplicationName() + " " + getApplicationVersion()));
        juce::Logger::setCurrentLogger (logger.get());

        juce::PropertiesFile::Options options;
        options.applicationName = "Le Pixophone";
        options.folderName = "Le Pixophone";
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        settings.setStorageParameters (options);

        params = std::make_unique<ParameterStore>();
        applyCommandLine (commandLine, *params);

        renderEngine = std::make_unique<RenderEngine> (*params);
        renderEngine->start();

        media = std::make_unique<MediaLibrary> (*renderEngine, *settings.getUserSettings());

        outputWindow = std::make_unique<OutputWindow> (*renderEngine);
        controlWindow = std::make_unique<ControlWindow> (*renderEngine, *outputWindow, *params, *media);

        // The original loads preset 0 at start-up (loadbang, §3.3).
        media->loadPreset (ImageBank::mix, 0);
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
        media.reset();
        renderEngine.reset();
        params.reset();
        settings.saveIfNeeded();

        juce::Logger::writeToLog ("Shutdown complete");
        juce::Logger::setCurrentLogger (nullptr);
        logger.reset();
    }

    void anotherInstanceStarted (const juce::String&) override {}

private:
    std::unique_ptr<juce::FileLogger> logger;
    juce::ApplicationProperties settings;
    std::unique_ptr<ParameterStore> params;
    std::unique_ptr<RenderEngine> renderEngine;
    std::unique_ptr<MediaLibrary> media;
    std::unique_ptr<OutputWindow> outputWindow;
    std::unique_ptr<ControlWindow> controlWindow;
    bool quitRequested = false;
};

} // namespace pixo

START_JUCE_APPLICATION (pixo::PixophoneApplication)
