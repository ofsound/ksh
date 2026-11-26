#include <juce_audio_plugin_client/detail/juce_CheckSettingMacros.h>
#include <juce_audio_plugin_client/detail/juce_IncludeSystemHeaders.h>
#include "StandalonePreferences.h"
#include "StandaloneMidiRouting.h"
#if JUCE_MAC
 #include "../source/PluginEditor.h"
#endif
#include "../source/ui/KshFooterMenu.h"
#if defined (KSH_STANDALONE_LINK)
 #include "StandaloneLink.h"
#endif
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
constexpr auto preferredMidiOutputSetting = "kshPreferredMidiOutput";
constexpr auto midiThruSetting = "kshMidiThru";
constexpr auto midiThruMenuId = 10002;
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
        observedMidiOutput = deviceManager.getDefaultMidiOutputIdentifier();
        preferredMidiOutput = observedMidiOutput;
        if (settingsToUse != nullptr)
        {
            if (preferredMidiOutput.isEmpty())
                if (const auto setup = settingsToUse->getXmlValue ("audioSetup"))
                    preferredMidiOutput = setup->getStringAttribute ("defaultMidiOutputDevice");
            preferredMidiOutput = settingsToUse->getValue (preferredMidiOutputSetting, preferredMidiOutput);
            settingsToUse->setValue (preferredMidiOutputSetting, preferredMidiOutput);
        }
       #if JUCE_IOS
        networkOutputSelected = settingsToUse != nullptr && settingsToUse->getBoolValue (networkMidiOutputSetting);
       #endif
        updateMidiOutput();
        if (auto* plugin = dynamic_cast<PluginProcessor*> (processor.get()))
        {
            plugin->setTimestampedStandaloneMidiInput (true);
            plugin->setStandaloneMidiThru (settings != nullptr && settings->getBoolValue (midiThruSetting));
        }
        refreshMidiInputs();
        deviceManager.addMidiInputDeviceCallback ({}, this);
        deviceManager.addChangeListener (this);
        midiDeviceChanges = MidiDeviceListConnection::make ([this]
        {
            refreshMidiInputs();
            updateMidiOutput();
        });
       #if defined (KSH_STANDALONE_LINK)
        if (ksh::StandaloneCoreMidiOutput::isAvailable())
            if (auto* plugin = dynamic_cast<PluginProcessor*> (processor.get()))
                link = ksh::StandaloneLink::create (plugin->getStandaloneTempoBpm(), settings);
        updateLinkOutputLatency (deviceManager.getCurrentAudioDevice());
       #endif
        installStandaloneServices();
    }

    ~KshStandalonePluginHolder() override
    {
        detachMidiOutput();
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

#if defined (KSH_STANDALONE_LINK)
    void setLinkForeground (bool foreground)
    {
        if (link != nullptr)
            link->setForeground (foreground);
    }
#endif

private:
   #if JUCE_IOS || defined (KSH_STANDALONE_LINK)
    void audioDeviceAboutToStart (AudioIODevice* device) override
    {
        emptyBuffer.setSize (device->getActiveInputChannels().countNumberOfSetBits(), device->getCurrentBufferSizeSamples());
        emptyBuffer.clear();
        player.audioDeviceAboutToStart (device);
        player.setMidiOutput (nullptr);
       #if defined (KSH_STANDALONE_LINK)
        updateLinkOutputLatency (device);
       #endif
    }
   #endif

   #if JUCE_IOS
    void audioDeviceError (const String& message) override
    {
        // JUCE stops the audio unit on interruption without calling audioDeviceStopped().
        if (message == "AVAudioSessionInterruptionTypeBegan" || message == "AVAudioSessionMediaServicesWereLostNotification")
            if (auto* plugin = dynamic_cast<PluginProcessor*> (processor.get()))
                plugin->flushStandaloneMidiOutput();
    }
   #endif

   #if defined (KSH_STANDALONE_LINK)
    void updateLinkOutputLatency (AudioIODevice* device)
    {
        if (link != nullptr && device != nullptr && device->getCurrentSampleRate() > 0.0)
            link->setOutputLatency (device->getOutputLatencyInSamples() / device->getCurrentSampleRate());
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
           #if defined (KSH_STANDALONE_LINK)
            if (link != nullptr)
            {
                plugin->setStandaloneSync (link.get());
                plugin->standaloneLinkStatus = [this] { return link->status(); };
                plugin->showStandaloneLinkSettings = [this] (Component& target) { link->showSettings (target); };
            }
           #endif
           #if JUCE_IOS || defined (KSH_STANDALONE_LINK)
            updateMidiOutput();
           #endif
            plugin->showStandaloneMidiOutputMenu = [this] (Component& target) { showMidiOutputMenu (target); };
            plugin->standaloneMidiOutputName = [this]
            {
               #if JUCE_IOS
                if (networkOutputSelected)
                    return String (networkMidiOutputName);
               #endif
                return activeMidiOutput.name.isNotEmpty() ? activeMidiOutput.name : String ("Unavailable");
            };
        }
    }

    void clearStandaloneServices()
    {
        if (auto* plugin = dynamic_cast<PluginProcessor*> (processor.get()))
        {
            plugin->setStandaloneMidiOutput (nullptr);
            plugin->setStandaloneSync (nullptr);
            plugin->showStandaloneLinkSettings = {};
            plugin->standaloneLinkStatus = {};
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
        const auto network = ksh::getStandaloneNetworkMidiDestination();
        destinations = ksh::withoutStandaloneNetworkDestination (std::move (destinations), network.identifier);
        localSelected = !networkOutputSelected;
       #endif
        PopupMenu menu;
        menu.setLookAndFeel (&target.getLookAndFeel());
        menu.addSectionHeader ("MIDI output destination");
        menu.addItem (1, virtualMidiOutputName, virtualMidiOutput != nullptr,
                      localSelected && activeMidiOutput == virtualOutputInfo);
        menu.addSeparator();
       #if JUCE_IOS
        menu.addItem (networkMidiOutputMenuId, networkMidiOutputName, virtualMidiOutput != nullptr, networkOutputSelected);
       #endif
        for (int i = 0; i < destinations.size(); ++i)
            menu.addItem (i + 2, destinations[i].name, true, destinations[i].identifier == activeMidiOutput.identifier);
        if (destinations.isEmpty())
            menu.addItem (destinations.size() + 2, "No connected MIDI destinations", false);
        if (auto* plugin = dynamic_cast<PluginProcessor*> (processor.get()))
        {
            menu.addSeparator();
            menu.addItem (midiThruMenuId, "MIDI Thru (echo input to output)", true, plugin->isStandaloneMidiThruEnabled());
        }
       #if JUCE_IOS
        menu.addSeparator();
        menu.addItem (bluetoothMidiMenuId, String::fromUTF8 ("Connect Bluetooth MIDI…"),
                      BluetoothMidiDevicePairingDialogue::isAvailable());
        const Component::SafePointer<Component> menuTarget (&target);
       #endif
        const WeakReference<KshStandalonePluginHolder> weak (this);
        ksh::ui::showAnchoredPopup (menu, ksh::ui::footerMenuOptions (target), [weak, destinations
                                     #if JUCE_IOS
                                      , menuTarget
                                     #endif
                                      ] (int result)
        {
            if (weak == nullptr || result == 0)
                return;
            if (result == midiThruMenuId)
            {
                if (auto* plugin = dynamic_cast<PluginProcessor*> (weak->processor.get()))
                {
                    const bool enabled = !plugin->isStandaloneMidiThruEnabled();
                    plugin->setStandaloneMidiThru (enabled);
                    if (weak->settings != nullptr)
                        weak->settings->setValue (midiThruSetting, enabled);
                }
                return;
            }
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
        detachMidiOutput();
       #if JUCE_IOS
        networkForwarder.stop();
        networkOutputSelected = network;
        if (settings != nullptr)
            settings->setValue (networkMidiOutputSetting, network);
       #endif
        rememberMidiOutput (identifier);
        updateMidiOutput();
       #if JUCE_IOS
        refreshMidiInputs();
       #endif
        saveAudioDeviceState();
        if (identifier.isNotEmpty() && activeMidiOutput.identifier != identifier)
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
       #if defined (KSH_STANDALONE_LINK)
        updateLinkOutputLatency (deviceManager.getCurrentAudioDevice());
       #endif
        refreshMidiInputs();
        updateMidiOutput();
    }

    void refreshMidiInputs()
    {
        auto available = MidiInput::getAvailableDevices();
       #if JUCE_IOS
        if (!networkOutputSelected)
        {
            const auto networkSource = ksh::getStandaloneNetworkMidiSourceIdentifier();
            if (networkSource.isNotEmpty())
            {
                if (deviceManager.isMidiInputDeviceEnabled (networkSource))
                    deviceManager.setMidiInputDeviceEnabled (networkSource, false);
                available.removeIf ([&] (const auto& input) { return input.identifier == networkSource; });
            }
        }
       #endif
        ksh::updateStandaloneMidiInputs (available, lastMidiDevices,
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

    void rememberMidiOutput (const String& identifier)
    {
        preferredMidiOutput = identifier;
        if (settings != nullptr)
            settings->setValue (preferredMidiOutputSetting, preferredMidiOutput);
    }

    void detachMidiOutput()
    {
        player.setMidiOutput (nullptr);
       #if JUCE_IOS || defined (KSH_STANDALONE_LINK)
        if (auto* plugin = dynamic_cast<PluginProcessor*> (processor.get()))
            plugin->setStandaloneMidiOutput (nullptr);
       #endif
        if (auto* output = std::exchange (bufferedMidiOutput, nullptr))
        {
            output->clearAllPendingMessages();
            for (int channel = 1; channel <= 16; ++channel)
            {
                output->sendMessageNow (MidiMessage::controllerEvent (channel, 64, 0));
                output->sendMessageNow (MidiMessage::allNotesOff (channel));
            }
        }
    }

    void updateMidiOutput()
    {
        if (updatingMidiOutput)
            return;
        const ScopedValueSetter<bool> updating (updatingMidiOutput, true);

        const auto managerSelection = deviceManager.getDefaultMidiOutputIdentifier();
        // The JUCE audio settings dialog can also change the selected device.
        if (managerSelection != observedMidiOutput)
        {
            rememberMidiOutput (managerSelection);
           #if JUCE_IOS
            if (managerSelection.isNotEmpty() && networkOutputSelected)
            {
                networkForwarder.stop();
                networkOutputSelected = false;
                if (settings != nullptr)
                    settings->setValue (networkMidiOutputSetting, false);
            }
           #endif
        }

        auto destinations = MidiOutput::getAvailableDevices();
       #if JUCE_IOS
        const auto network = ksh::getStandaloneNetworkMidiDestination();
        destinations = ksh::withoutStandaloneNetworkDestination (std::move (destinations), network.identifier);
        const auto availableId = networkOutputSelected ? String() : ksh::availableStandaloneMidiOutput (destinations, preferredMidiOutput);
       #else
        const auto availableId = ksh::availableStandaloneMidiOutput (destinations, preferredMidiOutput);
       #endif
        if (availableId != managerSelection)
        {
            detachMidiOutput();
            deviceManager.setDefaultMidiOutputDevice (availableId);
        }
        observedMidiOutput = deviceManager.getDefaultMidiOutputIdentifier();
        auto* selected = deviceManager.getDefaultMidiOutput();
        auto* output = selected != nullptr ? selected : virtualMidiOutput.get();
        activeMidiOutput = {};

       #if JUCE_IOS || defined (KSH_STANDALONE_LINK)
        if (auto* plugin = dynamic_cast<PluginProcessor*> (processor.get()))
        {
            if (ksh::StandaloneCoreMidiOutput::isAvailable())
            {
                const auto attach = [this, plugin] (MidiOutput* candidate, bool virtualSource)
                {
                    if (candidate == nullptr)
                        return false;
                    const auto info = candidate->getDeviceInfo();
                    if (!nativeMidiOutput.matches (info, virtualSource))
                    {
                        detachMidiOutput();
                        if (nativeMidiOutput.open (info, virtualSource).isNotEmpty())
                            return false;
                    }
                    plugin->setStandaloneMidiOutput (&nativeMidiOutput);
                    activeMidiOutput = info;
                    return true;
                };
                if (attach (output, selected == nullptr) || (selected != nullptr && attach (virtualMidiOutput.get(), true)))
                {
                    updateNetworkForwarding();
                    return;
                }
                output = virtualMidiOutput.get();
            }
        }
       #endif
        if (output != bufferedMidiOutput)
            detachMidiOutput();
        if (output != nullptr && !output->isBackgroundThreadRunning())
            output->startBackgroundThread();
        bufferedMidiOutput = output;
        player.setMidiOutput (output);
        if (output != nullptr)
            activeMidiOutput = output->getDeviceInfo();
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
    MidiDeviceInfo activeMidiOutput;
    String preferredMidiOutput, observedMidiOutput;
    MidiOutput* bufferedMidiOutput = nullptr;
    bool updatingMidiOutput = false;
    MidiDeviceListConnection midiDeviceChanges;
    const bool autoOpenExternalMidiDevices;
   #if defined (KSH_STANDALONE_LINK)
    std::unique_ptr<ksh::StandaloneLink> link;
   #endif
   #if JUCE_IOS || defined (KSH_STANDALONE_LINK)
    ksh::StandaloneCoreMidiOutput nativeMidiOutput;
   #endif
   #if JUCE_IOS
    ksh::StandaloneNetworkMidiForwarder networkForwarder;
    bool networkOutputSelected = false;
    String networkForwardingError;
   #endif
    JUCE_DECLARE_WEAK_REFERENCEABLE (KshStandalonePluginHolder)
};

class KshStandaloneApp final : public JUCEApplication
#if JUCE_MAC
                             , private MenuBarModel
#endif
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

    void anotherInstanceStarted ([[maybe_unused]] const String& commandLine) override
    {
       #if JUCE_MAC
        const auto paths = StringArray::fromTokens (commandLine, true);
        if (paths.size() != 1)
            return;
        const auto path = paths[0].unquoted();
        if (!File::isAbsolutePath (path))
            return;
        const File file (path);
        if (file.hasFileExtension ("kshproject"))
        {
            pendingProject = file;
            openPendingProject();
        }
       #endif
    }

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

    void initialise ([[maybe_unused]] const String& commandLine) override
    {
       #if JUCE_IOS
        if (SystemStats::getDeviceDescription().startsWithIgnoreCase ("iPhone"))
            Desktop::getInstance().setOrientationsEnabled (Desktop::rotatedClockwise | Desktop::rotatedAntiClockwise);
       #endif
        mainWindow = rawToUniquePtr (createWindow());

        if (mainWindow != nullptr)
        {
           #if JUCE_MAC
            menuCommands.setFirstCommandTarget (this);
            menuCommands.registerAllCommandsForTarget (this);
            PopupMenu applicationMenu;
            applicationMenu.addCommandItem (&menuCommands, audioSettingsCommand);
            MenuBarModel::setMacMainMenu (this, &applicationMenu);
           #endif
           #if JUCE_STANDALONE_FILTER_WINDOW_USE_KIOSK_MODE
            Desktop::getInstance().setKioskModeComponent (mainWindow.get(), false);
           #endif

            mainWindow->setVisible (true);
           #if JUCE_MAC
            if (pendingProject == File())
                anotherInstanceStarted (commandLine);
            else
                openPendingProject();
           #endif
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
       #if JUCE_MAC
        MenuBarModel::setMacMainMenu (nullptr);
       #endif
        saveCurrentPluginState();
        pluginHolder = nullptr;
        mainWindow = nullptr;
        appProperties.saveIfNeeded();
    }

   #if JUCE_IOS
    void resumed() override
    {
       #if defined (KSH_STANDALONE_LINK)
        setLinkForeground (true);
       #endif
        updateFullscreenSystemGestures();
    }

    void suspended() override
    {
       #if defined (KSH_STANDALONE_LINK)
        setLinkForeground (false);
       #endif
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
   #if JUCE_MAC
    static constexpr CommandID audioSettingsCommand = 0x2000;
    static constexpr CommandID saveProjectCommand = 0x2001;
    static constexpr CommandID saveProjectAsCommand = 0x2002;
    ApplicationCommandManager menuCommands;
    File pendingProject;

    void openPendingProject()
    {
        if (pendingProject == File() || mainWindow == nullptr)
            return;
        auto* holder = mainWindow->getPluginHolder();
        if (auto* editor = dynamic_cast<PluginEditor*> (holder->processor->getActiveEditor()))
        {
            const auto file = std::exchange (pendingProject, File());
            editor->filesDropped ({ file.getFullPathName() }, 0, 0);
        }
    }

    StringArray getMenuBarNames() override { return { "File" }; }
    PopupMenu getMenuForIndex (int index, const String&) override
    {
        PopupMenu menu;
        if (index == 0)
        {
            menu.addCommandItem (&menuCommands, saveProjectCommand);
            menu.addCommandItem (&menuCommands, saveProjectAsCommand);
        }
        return menu;
    }
    void menuItemSelected (int, int) override {}

    void getAllCommands (Array<CommandID>& commands) override
    {
        JUCEApplication::getAllCommands (commands);
        commands.add (audioSettingsCommand);
        commands.add (saveProjectCommand);
        commands.add (saveProjectAsCommand);
    }

    void getCommandInfo (CommandID command, ApplicationCommandInfo& info) override
    {
        if (command == audioSettingsCommand)
        {
            info.setInfo ("Audio/MIDI Settings", "Open Audio/MIDI Settings", "Application", 0);
            info.addDefaultKeypress (',', ModifierKeys::commandModifier);
        }
        else if (command == saveProjectCommand)
        {
            info.setInfo ("Save", "Save the current project", "File", 0);
            info.addDefaultKeypress ('s', ModifierKeys::commandModifier);
        }
        else if (command == saveProjectAsCommand)
        {
            info.setInfo ("Save As...", "Save a project under a new name or location", "File", 0);
            info.addDefaultKeypress ('s', ModifierKeys::commandModifier | ModifierKeys::shiftModifier);
        }
        else
            JUCEApplication::getCommandInfo (command, info);
    }

    bool perform (const InvocationInfo& info) override
    {
        if (info.commandID != audioSettingsCommand && info.commandID != saveProjectCommand && info.commandID != saveProjectAsCommand)
            return JUCEApplication::perform (info);

        auto* holder = mainWindow != nullptr ? mainWindow->getPluginHolder() : nullptr;
        if (holder == nullptr)
            return false;
        if (info.commandID == audioSettingsCommand)
        {
            holder->showAudioSettingsDialog();
            return true;
        }
        if (auto* editor = dynamic_cast<PluginEditor*> (holder->processor->getActiveEditor()))
        {
            editor->saveCurrentProject (info.commandID == saveProjectAsCommand);
            return true;
        }
        return false;
    }
   #endif

   #if JUCE_IOS
   #if defined (KSH_STANDALONE_LINK)
    void setLinkForeground (bool foreground)
    {
        auto* holder = mainWindow != nullptr ? mainWindow->getPluginHolder() : pluginHolder.get();
        if (auto* standalone = dynamic_cast<KshStandalonePluginHolder*> (holder))
            standalone->setLinkForeground (foreground);
    }
   #endif

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
