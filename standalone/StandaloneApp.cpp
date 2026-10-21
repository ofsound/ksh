#include <juce_audio_plugin_client/detail/juce_CheckSettingMacros.h>
#include <juce_audio_plugin_client/detail/juce_IncludeSystemHeaders.h>
#include "StandalonePreferences.h"
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
                                        private ChangeListener,
                                        private MidiInputCallback
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
        if (settingsToUse != nullptr)
            if (auto* plugin = dynamic_cast<PluginProcessor*> (processor.get()))
            {
                ksh::restoreStandaloneUiScale (*plugin, *settingsToUse);
                ksh::restoreStandaloneProjectFile (*plugin, *settingsToUse);
            }

        createVirtualMidiOutput();
        useVirtualMidiOutputIfAvailable();
        if (auto* plugin = dynamic_cast<PluginProcessor*> (processor.get()))
            plugin->setTimestampedStandaloneMidiInput (true);
        deviceManager.removeMidiInputDeviceCallback ({}, &player);
        deviceManager.addMidiInputDeviceCallback ({}, this);
        deviceManager.addChangeListener (this);
    }

    ~KshStandalonePluginHolder() override
    {
        deviceManager.removeChangeListener (this);
        deviceManager.removeMidiInputDeviceCallback ({}, this);
        player.setMidiOutput (nullptr);

        if (virtualMidiOutput != nullptr)
            virtualMidiOutput->stopBackgroundThread();
    }

private:
    void handleIncomingMidiMessage (MidiInput* input, const MidiMessage& message) override
    {
        player.handleIncomingMidiMessage (input, message);
        if (auto* plugin = dynamic_cast<PluginProcessor*> (processor.get()))
            plugin->captureStandaloneMidiInput (message);
    }

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
       #if JUCE_IOS
        if (SystemStats::getDeviceDescription().startsWithIgnoreCase ("iPhone"))
            Desktop::getInstance().setOrientationsEnabled (Desktop::rotatedClockwise | Desktop::rotatedAntiClockwise);
       #endif
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
        saveCurrentPluginState();
        pluginHolder = nullptr;
        mainWindow = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        saveCurrentPluginState();

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
    void saveCurrentPluginState()
    {
        const auto saveHolder = [] (StandalonePluginHolder& holder)
        {
            if (holder.settings != nullptr)
                if (auto* plugin = dynamic_cast<PluginProcessor*> (holder.processor.get()))
                {
                    ksh::saveStandaloneUiScale (*plugin, *holder.settings);
                    ksh::saveStandaloneProjectFile (*plugin, *holder.settings);
                }
            holder.savePluginState();
        };
        if (pluginHolder != nullptr)
            saveHolder (*pluginHolder);

        if (mainWindow != nullptr && mainWindow->pluginHolder != nullptr)
            saveHolder (*mainWindow->pluginHolder);

        appProperties.saveIfNeeded();
    }

    std::unique_ptr<StandalonePluginHolder> pluginHolder;
};
} // namespace juce

START_JUCE_APPLICATION (juce::KshStandaloneApp)
