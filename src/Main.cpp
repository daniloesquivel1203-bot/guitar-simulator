#include "MainComponent.h"
#include "ui/Theme.h"

class GuitarSimulatorApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        juce::PropertiesFile::Options options;
        options.applicationName = getApplicationName();
        options.filenameSuffix = ".settings";
        options.folderName = getApplicationName();
        options.osxLibrarySubFolder = "Application Support";
        options.millisecondsBeforeSaving = 1000;
        settings.setStorageParameters (options);

        juce::LookAndFeel::setDefaultLookAndFeel (&theme);

        engine = std::make_unique<AudioEngine> (*settings.getUserSettings());
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *engine, *settings.getUserSettings());
    }

    void shutdown() override
    {
        mainWindow.reset();
        engine.reset();
        settings.closeFiles();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override   { quit(); }

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, AudioEngine& engineToUse, juce::PropertiesFile& settingsToUse)
            : DocumentWindow (name, Theme::background, DocumentWindow::allButtons),
              windowSettings (settingsToUse)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent (engineToUse), true);
            setResizable (true, true);
            setResizeLimits (1100, 700, 2800, 1800);

            // Reopen where the window was last time, if that is still on a screen.
            // The pedal layout needs more room than the old one, so a window saved
            // smaller than that starts fresh at the new size.
            if (! restoreWindowStateFromString (windowSettings.getValue (windowStateKey))
                || getWidth() < 1100 || getHeight() < 700)
                centreWithSize (1280, 800);

            setVisible (true);
        }

        ~MainWindow() override
        {
            windowSettings.setValue (windowStateKey, getWindowStateAsString());
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        static constexpr const char* windowStateKey = "window.state";

        juce::PropertiesFile& windowSettings;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    Theme theme;
    juce::ApplicationProperties settings;
    juce::TooltipWindow tooltips;
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (GuitarSimulatorApplication)
