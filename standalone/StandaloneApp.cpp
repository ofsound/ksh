#include <juce_audio_plugin_client/detail/juce_CheckSettingMacros.h>
#include <juce_audio_plugin_client/detail/juce_IncludeSystemHeaders.h>
#include <juce_audio_plugin_client/detail/juce_IncludeModuleHeaders.h>
#include <juce_audio_plugin_client/detail/juce_PluginUtilities.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace juce
{
namespace
{
constexpr auto virtualMidiOutputName = "KSH Output";
}

class KshStandalonePluginHolder final : public StandalonePluginHolder,
                                        private ChangeListener
{
public:
    KshStandalonePluginHolder (PropertySet* settingsToUse,
                               bool takeOwnershipOfSettings,
                               const String& preferredDefaultDeviceName,
                               const AudioDeviceManager::AudioDeviceSetup* preferredSetupOptions,
                               const Array<PluginInOuts>& channels,
                               bool shouldAutoOpenMidiDevices)
        : StandalonePluginHolder (settingsToUse,
                                  takeOwnershipOfSettings,
                                  preferredDefaultDeviceName,
                                  preferredSetupOptions,
                                  channels,
                                  shouldAutoOpenMidiDevices)
    {
        createVirtualMidiOutput();
        useVirtualMidiOutputIfAvailable();
        deviceManager.addChangeListener (this);
    }

    ~KshStandalonePluginHolder() override
    {
        deviceManager.removeChangeListener (this);
        player.setMidiOutput (nullptr);

        if (virtualMidiOutput != nullptr)
            virtualMidiOutput->stopBackgroundThread();
    }

private:
    void changeListenerCallback (ChangeBroadcaster*) override
    {
        useVirtualMidiOutputIfAvailable();
    }

    void createVirtualMidiOutput()
    {
        virtualMidiOutput = MidiOutput::createNewDevice (virtualMidiOutputName);

        if (virtualMidiOutput != nullptr)
            virtualMidiOutput->startBackgroundThread();
    }

    void useVirtualMidiOutputIfAvailable()
    {
        if (virtualMidiOutput != nullptr)
            player.setMidiOutput (virtualMidiOutput.get());
    }

    std::unique_ptr<MidiOutput> virtualMidiOutput;
};

class KshStandaloneApp final : public JUCEApplication
{
public:
    KshStandaloneApp()
    {
        PropertiesFile::Options options;

        options.applicationName = CharPointer_UTF8 (JucePlugin_Name);
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
       #if JUCE_LINUX || JUCE_BSD
        options.folderName = "~/.config";
       #else
        options.folderName = "";
       #endif

        appProperties.setStorageParameters (options);
    }

    const String getApplicationName() override
    {
        return CharPointer_UTF8 (JucePlugin_Name);
    }

    const String getApplicationVersion() override
    {
        return JucePlugin_VersionString;
    }

    bool moreThanOneInstanceAllowed() override
    {
        return true;
    }

    void anotherInstanceStarted (const String&) override {}

    StandaloneFilterWindow* createWindow()
    {
        if (Desktop::getInstance().getDisplays().displays.isEmpty())
        {
            jassertfalse;
            return nullptr;
        }

        return new StandaloneFilterWindow (
            getApplicationName(),
            LookAndFeel::getDefaultLookAndFeel().findColour (ResizableWindow::backgroundColourId),
            createPluginHolder());
    }

    std::unique_ptr<StandalonePluginHolder> createPluginHolder()
    {
        constexpr auto autoOpenMidiDevices =
       #if (JUCE_ANDROID || JUCE_IOS) && ! JUCE_DONT_AUTO_OPEN_MIDI_DEVICES_ON_MOBILE
                true;
       #else
                false;
       #endif

       #ifdef JucePlugin_PreferredChannelConfigurations
        constexpr StandalonePluginHolder::PluginInOuts channels[] { JucePlugin_PreferredChannelConfigurations };
        const Array<StandalonePluginHolder::PluginInOuts> channelConfig (channels, numElementsInArray (channels));
       #else
        const Array<StandalonePluginHolder::PluginInOuts> channelConfig;
       #endif

        return std::make_unique<KshStandalonePluginHolder> (appProperties.getUserSettings(),
                                                            false,
                                                            String {},
                                                            nullptr,
                                                            channelConfig,
                                                            autoOpenMidiDevices);
    }

    void initialise (const String&) override
    {
        mainWindow = rawToUniquePtr (createWindow());

        if (mainWindow != nullptr)
        {
           #if JUCE_STANDALONE_FILTER_WINDOW_USE_KIOSK_MODE
            Desktop::getInstance().setKioskModeComponent (mainWindow.get(), false);
           #endif

            mainWindow->setVisible (true);
        }
        else
        {
            pluginHolder = createPluginHolder();
        }
    }

    void shutdown() override
    {
        pluginHolder = nullptr;
        mainWindow = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (pluginHolder != nullptr)
            pluginHolder->savePluginState();

        if (mainWindow != nullptr)
            mainWindow->pluginHolder->savePluginState();

        if (ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            Timer::callAfterDelay (100, [] {
                if (auto* app = JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
        }
        else
        {
            quit();
        }
    }

protected:
    ApplicationProperties appProperties;
    std::unique_ptr<StandaloneFilterWindow> mainWindow;

private:
    std::unique_ptr<StandalonePluginHolder> pluginHolder;
};
} // namespace juce

START_JUCE_APPLICATION (juce::KshStandaloneApp)
