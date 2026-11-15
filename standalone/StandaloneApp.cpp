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

#if JUCE_IOS
void kshDeferIPhoneEdgeGestures (void* nativeView);
#endif
#if defined (KSH_IPAD_SYSTEM_OVERLAYS)
extern "C" void kshHideIPadSystemOverlays (const void* nativeView);
#endif

namespace juce
{
namespace
{
constexpr auto virtualMidiOutputName = "KSH Output";
constexpr auto networkMidiOutputName = "Network Session";
constexpr auto networkMidiOutputSetting = "kshNetworkMidiOutput";
constexpr auto networkMidiOutputMenuId = 10000;
#if JUCE_IOS
constexpr auto bluetoothMidiMenuId = 10001;
#endif

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
       #if JUCE_IOS
        networkOutputSelected = settingsToUse != nullptr && settingsToUse->getBoolValue (networkMidiOutputSetting);
       #endif
        updateMidiOutput();
        if (auto* plugin = dynamic_cast<PluginProcessor*> (processor.get()))
            plugin->setTimestampedStandaloneMidiInput (true);
        deviceManager.addMidiInputDeviceCallback ({}, this);
        deviceManager.addChangeListener (this);
        midiDeviceChanges = MidiDeviceListConnection::make ([this]
        {
            refreshMidiInputs();
            updateMidiOutput();
        });
        refreshMidiInputs();
        installStandaloneServices();
    }

    ~KshStandalonePluginHolder() override
    {
        clearStandaloneServices();
        masterReference.clear();
        midiDeviceChanges.reset();
        deviceManager.removeChangeListener (this);
        deviceManager.removeMidiInputDeviceCallback ({}, this);
        player.setMidiOutput (nullptr);
       #if JUCE_IOS
        networkForwarder.stop();
       #endif

        if (virtualMidiOutput != nullptr)
            virtualMidiOutput->stopBackgroundThread();
    }

    void createPlugin() override
    {
        StandalonePluginHolder::createPlugin();
        installStandaloneServices();
    }

    void deletePlugin() override
    {
        clearStandaloneServices();
        StandalonePluginHolder::deletePlugin();
    }

private:
   #if JUCE_IOS
    void audioDeviceAboutToStart (AudioIODevice* device) override
    {
        emptyBuffer.setSize (device->getActiveInputChannels().countNumberOfSetBits(), device->getCurrentBufferSizeSamples());
        emptyBuffer.clear();
        player.audioDeviceAboutToStart (device);
        player.setMidiOutput (nullptr);
    }
   #endif

    void installStandaloneServices()
    {
        if (auto* plugin = dynamic_cast<PluginProcessor*> (processor.get()))
        {
           #if JUCE_MAC
            plugin->showStandaloneAudioSettings = [this] { showAudioSettingsDialog(); };
           #endif
            plugin->setTimestampedStandaloneMidiInput (true);
           #if JUCE_IOS
            updateMidiOutput();
           #endif
            plugin->showStandaloneMidiOutputMenu = [this] (Component& target) { showMidiOutputMenu (target); };
            plugin->standaloneMidiOutputName = [this]
            {
               #if JUCE_IOS
                if (networkOutputSelected)
                    return String (networkMidiOutputName);
               #endif
                if (auto* selected = deviceManager.getDefaultMidiOutput())
                    return selected->getName();
                return virtualMidiOutput != nullptr ? String (virtualMidiOutputName) : String ("Unavailable");
            };
        }
    }

    void clearStandaloneServices()
    {
        if (auto* plugin = dynamic_cast<PluginProcessor*> (processor.get()))
        {
            plugin->setStandaloneMidiOutput (nullptr);
            plugin->showStandaloneMidiOutputMenu = {};
            plugin->showStandaloneAudioSettings = {};
            plugin->standaloneMidiOutputName = {};
        }
    }

    void showMidiOutputMenu (Component& target)
    {
        auto destinations = MidiOutput::getAvailableDevices();
        auto localSelected = true;
       #if JUCE_IOS
        const auto network = ksh::enableStandaloneNetworkMidi();
        destinations = ksh::withoutStandaloneNetworkDestination (std::move (destinations), network.identifier);
        localSelected = !networkOutputSelected;
       #endif
        const auto selected = deviceManager.getDefaultMidiOutputIdentifier();
        PopupMenu menu;
        menu.setLookAndFeel (&target.getLookAndFeel());
        menu.addSectionHeader ("MIDI output destination");
        menu.addItem (1, virtualMidiOutputName, virtualMidiOutput != nullptr, localSelected && selected.isEmpty());
        menu.addSeparator();
       #if JUCE_IOS
        menu.addItem (networkMidiOutputMenuId, network.identifier.isNotEmpty() ? networkMidiOutputName : "Network Session unavailable",
                      network.identifier.isNotEmpty() && virtualMidiOutput != nullptr, networkOutputSelected);
       #endif
        for (int i = 0; i < destinations.size(); ++i)
            menu.addItem (i + 2, destinations[i].name, true, destinations[i].identifier == selected);
        if (destinations.isEmpty())
            menu.addItem (destinations.size() + 2, "No connected MIDI destinations", false);
       #if JUCE_IOS
        menu.addSeparator();
        menu.addItem (bluetoothMidiMenuId, String::fromUTF8 ("Connect Bluetooth MIDI…"),
                      BluetoothMidiDevicePairingDialogue::isAvailable());
        const Component::SafePointer<Component> menuTarget (&target);
       #endif
        const WeakReference<KshStandalonePluginHolder> weak (this);
        menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&target)
                               .withParentComponent (target.getTopLevelComponent())
                               .withStandardItemHeight (44)
                               .withMinimumWidth (260)
                               .withMaximumNumColumns (1), [weak, destinations
                              #if JUCE_IOS
                               , menuTarget
                              #endif
                               ] (int result)
        {
            if (weak == nullptr || result == 0)
                return;
           #if JUCE_IOS
            if (result == bluetoothMidiMenuId)
            {
                const bool opened = BluetoothMidiDevicePairingDialogue::open (ModalCallbackFunction::create ([weak, menuTarget] (int)
                {
                    if (weak != nullptr && menuTarget != nullptr && menuTarget->isShowing())
                        weak->showMidiOutputMenu (*menuTarget);
                }));
                if (!opened)
                    AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Bluetooth MIDI",
                                                      "Could not open the Bluetooth MIDI device picker.");
                return;
            }
           #endif
            const auto useNetwork = result == networkMidiOutputMenuId;
            if (result != 1 && !useNetwork && !isPositiveAndBelow (result - 2, destinations.size()))
                return;
            const auto identifier = result == 1 || useNetwork ? String() : destinations[result - 2].identifier;
            weak->selectMidiOutput (identifier, useNetwork);
        });
    }

    void selectMidiOutput (const String& identifier, [[maybe_unused]] bool network)
    {
        // Detach and flush the old route before the manager closes it.
        player.setMidiOutput (nullptr);
       #if JUCE_IOS
        if (auto* plugin = dynamic_cast<PluginProcessor*> (processor.get()))
            plugin->setStandaloneMidiOutput (nullptr);
       #endif
        auto* oldOutput = deviceManager.getDefaultMidiOutput();
        if (oldOutput == nullptr)
            oldOutput = virtualMidiOutput.get();
        if (oldOutput != nullptr)
        {
            oldOutput->clearAllPendingMessages();
            for (int channel = 1; channel <= 16; ++channel)
                oldOutput->sendMessageNow (MidiMessage::allNotesOff (channel));
        }
       #if JUCE_IOS
        networkForwarder.stop();
        networkOutputSelected = network;
        if (settings != nullptr)
            settings->setValue (networkMidiOutputSetting, network);
       #endif
        deviceManager.setDefaultMidiOutputDevice (identifier);
        updateMidiOutput();
        saveAudioDeviceState();
        if (identifier.isNotEmpty() && deviceManager.getDefaultMidiOutput() == nullptr)
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "MIDI output",
                                              "Could not open that MIDI destination. Using KSH Output instead.");
       #if JUCE_IOS
        if (network && !networkForwarder.isActive())
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "MIDI output",
                                              "Could not connect to the Network Session. KSH will keep trying while it is selected, and KSH Output stays available.\n\n"
                                                  + networkForwardingError);
       #endif
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
           #if ! JUCE_IOS
            virtualMidiOutput->startBackgroundThread();
           #endif
        }
    }

    void updateMidiOutput()
    {
        // An explicit destination (including the iOS network session) takes
        // priority over the local virtual source. JUCE persists this selection.
        auto* selected = deviceManager.getDefaultMidiOutput();
        auto* output = selected != nullptr ? selected : virtualMidiOutput.get();
       #if JUCE_IOS
        // CoreMIDI receives audio-clock timestamps directly. Never let the
        // AudioProcessorPlayer also send the same block via its timer queue.
        player.setMidiOutput (nullptr);
        if (auto* plugin = dynamic_cast<PluginProcessor*> (processor.get()))
        {
            const auto info = output != nullptr ? output->getDeviceInfo() : MidiDeviceInfo();
            if (output != nullptr && !nativeMidiOutput.matches (info, selected == nullptr))
            {
                plugin->setStandaloneMidiOutput (nullptr);
                const auto error = nativeMidiOutput.open (info, selected == nullptr);
                if (error.isNotEmpty())
                {
                    AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "MIDI output", error);
                    return;
                }
            }
            plugin->setStandaloneMidiOutput (output != nullptr ? &nativeMidiOutput : nullptr);
        }
       #else
        if (output != nullptr && !output->isBackgroundThreadRunning())
            output->startBackgroundThread();
        player.setMidiOutput (output);
       #endif
        updateNetworkForwarding();
    }

    void updateNetworkForwarding()
    {
       #if JUCE_IOS
        // The network route plays into KSH Output, which the forwarder relays.
        if (!networkOutputSelected || networkForwarder.isActive())
            return;
        if (virtualMidiOutput == nullptr)
            networkForwardingError = "KSH Output is unavailable";
        else if (deviceManager.getDefaultMidiOutput() != nullptr)
            networkForwardingError = "Another MIDI output is open";
        else
            networkForwardingError = networkForwarder.start (virtualOutputInfo);
       #endif
    }

    std::unique_ptr<MidiOutput> virtualMidiOutput;
    MidiDeviceInfo virtualOutputInfo { virtualMidiOutputName, {} };
    MidiDeviceListConnection midiDeviceChanges;
    const bool autoOpenExternalMidiDevices;
   #if JUCE_IOS
    ksh::StandaloneIOSMidiOutput nativeMidiOutput;
    ksh::StandaloneNetworkMidiForwarder networkForwarder;
    bool networkOutputSelected = false;
    String networkForwardingError;
   #endif
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

        auto* window = new StandaloneFilterWindow (
            getApplicationName(),
            LookAndFeel::getDefaultLookAndFeel().findColour (ResizableWindow::backgroundColourId),
            createPluginHolder());
       #if JUCE_MAC
        // JUCE keeps Options as a child even with native window chrome.
        for (auto* child : window->getChildren())
            if (auto* button = dynamic_cast<TextButton*> (child); button != nullptr && button->getButtonText() == "Options")
                button->setVisible (false);
        const auto contentSize = window->getContentComponent()->getLocalBounds();
        window->setUsingNativeTitleBar (true);
        window->setContentComponentSize (contentSize.getWidth(), contentSize.getHeight());
       #endif
        return window;
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
        // Start Network MIDI before the holder initializes JUCE's cached endpoints.
        (void) ksh::enableStandaloneNetworkMidi();
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
           #if JUCE_IOS
            updateFullscreenSystemGestures();
           #endif
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
    void resumed() override
    {
        updateFullscreenSystemGestures();
    }

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
   #if JUCE_IOS
    void updateFullscreenSystemGestures()
    {
        auto* peer = mainWindow != nullptr ? mainWindow->getPeer() : nullptr;
        auto* nativeView = peer != nullptr ? peer->getNativeHandle() : nullptr;
        if (nativeView == nullptr)
            return;

        if (mainWindow->isFullScreen())
            kshDeferIPhoneEdgeGestures (nativeView);
       #if defined (KSH_IPAD_SYSTEM_OVERLAYS)
        kshHideIPadSystemOverlays (nativeView);
       #endif
    }
   #endif

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
