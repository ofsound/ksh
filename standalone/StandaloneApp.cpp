#include <juce_audio_plugin_client/detail/juce_CheckSettingMacros.h>
#include <juce_audio_plugin_client/detail/juce_IncludeSystemHeaders.h>
#include "StandalonePreferences.h"
#include "StandaloneMidiRouting.h"
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
                                  false),
          autoOpenExternalMidiDevices (shouldAutoOpenMidiDevices)
    {
        if (settingsToUse != nullptr)
            if (auto* plugin = dynamic_cast<PluginProcessor*> (processor.get()))
            {
                ksh::restoreStandaloneUiScale (*plugin, *settingsToUse);
                ksh::restoreStandaloneProjectFile (*plugin, *settingsToUse);
            }

        // Replace JUCE's auto-open-all timer: it would open our own virtual output
        // as an input and echo every emitted note back into the processor.
        deviceManager.removeMidiInputDeviceCallback ({}, &player);
        createVirtualMidiOutput();
        updateMidiOutput();
        if (auto* plugin = dynamic_cast<PluginProcessor*> (processor.get()))
            plugin->setTimestampedStandaloneMidiInput (true);
        deviceManager.addMidiInputDeviceCallback ({}, this);
        deviceManager.addChangeListener (this);
        midiDeviceChanges = MidiDeviceListConnection::make ([this] { refreshMidiInputs(); });
        refreshMidiInputs();
        installMidiOutputServices();
    }

    ~KshStandalonePluginHolder() override
    {
        clearMidiOutputServices();
        masterReference.clear();
        midiDeviceChanges.reset();
        deviceManager.removeChangeListener (this);
        deviceManager.removeMidiInputDeviceCallback ({}, this);
        player.setMidiOutput (nullptr);

        if (virtualMidiOutput != nullptr)
            virtualMidiOutput->stopBackgroundThread();
    }

    void createPlugin() override
    {
        StandalonePluginHolder::createPlugin();
        installMidiOutputServices();
    }

    void deletePlugin() override
    {
        clearMidiOutputServices();
        StandalonePluginHolder::deletePlugin();
    }

private:
    void installMidiOutputServices()
    {
        if (auto* plugin = dynamic_cast<PluginProcessor*> (processor.get()))
        {
            plugin->setTimestampedStandaloneMidiInput (true);
            plugin->showStandaloneMidiOutputMenu = [this] (Component& target) { showMidiOutputMenu (target); };
            plugin->standaloneMidiOutputName = [this]
            {
                if (auto* selected = deviceManager.getDefaultMidiOutput())
                    return selected->getName();
                return virtualMidiOutput != nullptr ? String (virtualMidiOutputName) : String ("Unavailable");
            };
        }
    }

    void clearMidiOutputServices()
    {
        if (auto* plugin = dynamic_cast<PluginProcessor*> (processor.get()))
        {
            plugin->showStandaloneMidiOutputMenu = {};
            plugin->standaloneMidiOutputName = {};
        }
    }

    void showMidiOutputMenu (Component& target)
    {
       #if JUCE_IOS
        ksh::enableStandaloneNetworkMidi();
       #endif
        const auto destinations = MidiOutput::getAvailableDevices();
        const auto selected = deviceManager.getDefaultMidiOutputIdentifier();
        PopupMenu menu;
        menu.setLookAndFeel (&target.getLookAndFeel());
        menu.addSectionHeader ("MIDI output destination");
        menu.addItem (1, virtualMidiOutputName, virtualMidiOutput != nullptr, selected.isEmpty());
        menu.addSeparator();
        for (int i = 0; i < destinations.size(); ++i)
            menu.addItem (i + 2, destinations[i].name, true, destinations[i].identifier == selected);
        if (destinations.isEmpty())
            menu.addItem (destinations.size() + 2, "No connected MIDI destinations", false);
        const WeakReference<KshStandalonePluginHolder> weak (this);
        menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&target)
                               .withParentComponent (target.getTopLevelComponent())
                               .withStandardItemHeight (44)
                               .withMinimumWidth (260)
                               .withMaximumNumColumns (1), [weak, destinations] (int result)
        {
            if (weak == nullptr || result == 0)
                return;
            const auto identifier = result == 1 ? String() : destinations[result - 2].identifier;
            // Detach and flush the old route before the manager closes it.
            weak->player.setMidiOutput (nullptr);
            auto* oldOutput = weak->deviceManager.getDefaultMidiOutput();
            if (oldOutput == nullptr)
                oldOutput = weak->virtualMidiOutput.get();
            if (oldOutput != nullptr)
            {
                oldOutput->clearAllPendingMessages();
                for (int channel = 1; channel <= 16; ++channel)
                    oldOutput->sendMessageNow (MidiMessage::allNotesOff (channel));
            }
            weak->deviceManager.setDefaultMidiOutputDevice (identifier);
            weak->updateMidiOutput();
            weak->saveAudioDeviceState();
            if (result > 1 && weak->deviceManager.getDefaultMidiOutput() == nullptr)
                AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "MIDI output",
                                                  "Could not open that MIDI destination. Using KSH Output instead.");
        });
    }

    void handleIncomingMidiMessage (MidiInput* input, const MidiMessage& message) override
    {
        // A settings change can enable a source before its message-thread device
        // notification arrives. Block self-feedback here as well as in discovery.
        if (input == nullptr || ksh::isStandaloneMidiOutputSource (input->getDeviceInfo(), virtualOutputInfo))
            return;
        player.handleIncomingMidiMessage (input, message);
        if (auto* plugin = dynamic_cast<PluginProcessor*> (processor.get()))
            plugin->captureStandaloneMidiInput (message);
    }

    void changeListenerCallback (ChangeBroadcaster*) override
    {
        refreshMidiInputs();
        updateMidiOutput();
    }

    void refreshMidiInputs()
    {
        ksh::updateStandaloneMidiInputs (MidiInput::getAvailableDevices(), lastMidiDevices,
                                         virtualOutputInfo, autoOpenExternalMidiDevices,
                                         [this] (const String& identifier, bool enabled)
                                         {
                                             if (deviceManager.isMidiInputDeviceEnabled (identifier) != enabled)
                                                 deviceManager.setMidiInputDeviceEnabled (identifier, enabled);
                                         });
    }

    void createVirtualMidiOutput()
    {
        virtualMidiOutput = MidiOutput::createNewDevice (virtualMidiOutputName);

        if (virtualMidiOutput != nullptr)
        {
            virtualOutputInfo = virtualMidiOutput->getDeviceInfo();
            virtualMidiOutput->startBackgroundThread();
        }
    }

    void updateMidiOutput()
    {
        // An explicit destination (including the iOS network session) takes
        // priority over the local virtual source. JUCE persists this selection.
        auto* selected = deviceManager.getDefaultMidiOutput();
        auto* output = selected != nullptr ? selected : virtualMidiOutput.get();
        if (output != nullptr && !output->isBackgroundThreadRunning())
            output->startBackgroundThread();
        player.setMidiOutput (output);
    }

    std::unique_ptr<MidiOutput> virtualMidiOutput;
    MidiDeviceInfo virtualOutputInfo { virtualMidiOutputName, {} };
    MidiDeviceListConnection midiDeviceChanges;
    const bool autoOpenExternalMidiDevices;
    JUCE_DECLARE_WEAK_REFERENCEABLE (KshStandalonePluginHolder)
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

   #if JUCE_IOS
    void suspended() override
    {
        // iOS may terminate a backgrounded app without calling shutdown().
        saveCurrentPluginState();
        appProperties.saveIfNeeded();
    }
   #endif

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
