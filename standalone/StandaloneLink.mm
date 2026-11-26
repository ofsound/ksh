#include "StandaloneLink.h"
#include "StandaloneMidiClock.h"
#include "../source/ui/KshFooterMenu.h"
#include <mach/mach_time.h>

#if JUCE_IOS
 #import <UIKit/UIKit.h>
 #include <ABLLink.h>
 #include <ABLLinkSettingsViewController.h>
#else
 #include <ableton/Link.hpp>
#endif

namespace ksh
{
namespace
{
    // KSH's metronome accents every four beats. Pattern length and rate can
    // change during a set without changing the shared bar or launch boundary.
    constexpr double quantum = 4.0;

   #if JUCE_IOS
    class Session
    {
    public:
        explicit Session (double bpm) : link (ABLLinkNew (bpm))
        {
            linkEnabled.store (ABLLinkIsEnabled (link), std::memory_order_relaxed);
            transportSynced.store (ABLLinkIsStartStopSyncEnabled (link), std::memory_order_relaxed);
            ABLLinkSetIsEnabledCallback (link, updateFlag, &linkEnabled);
            ABLLinkSetIsStartStopSyncEnabledCallback (link, updateFlag, &transportSynced);
        }
        ~Session() { ABLLinkDelete (link); }
        bool enabled() const { return linkEnabled.load (std::memory_order_relaxed); }
        bool syncsTransport() const { return transportSynced.load (std::memory_order_relaxed); }
        auto peers() const { return ABLLinkNumPeers (link); }
        void setActive (bool active) { ABLLinkSetActive (link, active); }
        auto capture() { return ABLLinkCaptureAudioSessionState (link); }
        void commit (ABLLinkSessionStateRef state) { ABLLinkCommitAudioSessionState (link, state); }
        auto appState() const { return ABLLinkCaptureAppSessionState (link); }
        static double tempo (ABLLinkSessionStateRef state) { return ABLLinkGetTempo (state); }
        static bool playing (ABLLinkSessionStateRef state) { return ABLLinkIsPlaying (state); }
        static void setTempo (ABLLinkSessionStateRef state, double bpm, uint64_t time) { ABLLinkSetTempo (state, bpm, time); }
        static void setPlaying (ABLLinkSessionStateRef state, bool shouldPlay, uint64_t time) { ABLLinkSetIsPlaying (state, shouldPlay, time); }
        static void requestLaunch (ABLLinkSessionStateRef state, uint64_t time) { ABLLinkRequestBeatAtTime (state, 0.0, time, quantum); }
        static void requestSharedLaunch (ABLLinkSessionStateRef state) { ABLLinkRequestBeatAtStartPlayingTime (state, 0.0, quantum); }
        static double beat (ABLLinkSessionStateRef state, uint64_t time) { return ABLLinkBeatAtTime (state, time, quantum); }

        ABLLinkRef link;

    private:
        static void updateFlag (bool value, void* context)
        {
            static_cast<std::atomic<bool>*> (context)->store (value, std::memory_order_relaxed);
        }
        std::atomic<bool> linkEnabled { false }, transportSynced { false };
    };
   #else
    class Session
    {
    public:
        explicit Session (double bpm) : link (bpm) {}
        bool enabled() const { return link.isEnabled(); }
        bool syncsTransport() const { return link.isStartStopSyncEnabled(); }
        auto peers() const { return link.numPeers(); }
        auto capture() { return link.captureAudioSessionState(); }
        void commit (ableton::Link::SessionState state) { link.commitAudioSessionState (state); }
        auto appState() const { return link.captureAppSessionState(); }
        static double tempo (const ableton::Link::SessionState& state) { return state.tempo(); }
        static bool playing (const ableton::Link::SessionState& state) { return state.isPlaying(); }
        static void setTempo (ableton::Link::SessionState& state, double bpm, std::chrono::microseconds time) { state.setTempo (bpm, time); }
        static void setPlaying (ableton::Link::SessionState& state, bool shouldPlay, std::chrono::microseconds time) { state.setIsPlaying (shouldPlay, time); }
        static void requestLaunch (ableton::Link::SessionState& state, std::chrono::microseconds time) { state.requestBeatAtTime (0.0, time, quantum); }
        static void requestSharedLaunch (ableton::Link::SessionState& state) { state.requestBeatAtStartPlayingTime (0.0, quantum); }
        static double beat (const ableton::Link::SessionState& state, std::chrono::microseconds time) { return state.beatAtTime (time, quantum); }

        ableton::Link link;
    };
   #endif

    class LinkClock final : public StandaloneLink
    {
    public:
        LinkClock (double bpm, [[maybe_unused]] juce::PropertySet* preferences)
            : session (bpm)
           #if !JUCE_IOS
            , settings (preferences)
           #endif
        {
            mach_timebase_info (&timebase);
           #if JUCE_IOS
            session.setActive (true);
           #else
            if (settings != nullptr)
            {
                session.link.enableStartStopSync (settings->getBoolValue ("linkStartStopSync"));
                session.link.enable (settings->getBoolValue ("linkEnabled"));
            }
           #endif
        }

        ~LinkClock() override
        {
           #if JUCE_IOS
            if (settingsView != nil)
                [settingsView dismissViewControllerAnimated:NO completion:nil];
            settingsView = nil;
            session.setActive (false);
           #endif
        }

        void setForeground ([[maybe_unused]] bool inForeground) override
        {
           #if JUCE_IOS
            session.setActive (inForeground || transportActive.load (std::memory_order_relaxed) || session.syncsTransport());
           #endif
        }

        void setOutputLatency (double seconds) override
        {
            outputLatencyNs.store (static_cast<juce::uint64> (std::llround (std::max (0.0, seconds) * 1.0e9)),
                                   std::memory_order_relaxed);
        }

        bool requestPlaying (bool shouldPlay, bool shareWithPeers) override
        {
            if (!session.enabled())
                return false;
            playRequest.store ((shouldPlay ? 1 : -1) * (shareWithPeers ? 2 : 1), std::memory_order_relaxed);
            return true;
        }

        bool requestTempo (double bpm) override
        {
            if (!session.enabled())
                return false;
            tempoRequest.store (bpm, std::memory_order_relaxed);
            return true;
        }

        double sessionTempo() const override
        {
            return session.enabled() ? Session::tempo (session.appState()) : 0.0;
        }

        StandaloneLinkStatus status() const override
        {
            return { session.enabled(), static_cast<int> (session.peers()) };
        }

        StandaloneSyncBlock process (juce::Optional<juce::uint64> hostTimeNs, double sampleRate,
                                     int numSamples, double localTempo, bool localPlaying) override
        {
            StandaloneSyncBlock result;
            const bool enabled = session.enabled();
            const auto nowNs = StandaloneMidiClock::nanosecondsToHostTicks (mach_absolute_time(), timebase.denom, timebase.numer);
            const auto blockNs = hostTimeNs.orFallback (0);
            auto startNs = blockNs != 0 ? blockNs : nextBlockNs;
            if (startNs == 0 || (blockNs == 0 && (startNs > nowNs + 100000000 || nowNs > startNs + 100000000)))
                startNs = nowNs;
            nextBlockNs = startNs + static_cast<juce::uint64> (std::llround (numSamples * 1.0e9 / sampleRate));
            const auto outputLatency = outputLatencyNs.load (std::memory_order_relaxed);
            const auto presentationNs = startNs + outputLatency;
           #if JUCE_IOS
            const auto time = StandaloneMidiClock::nanosecondsToHostTicks (presentationNs, timebase.numer, timebase.denom);
           #else
            const auto time = std::chrono::duration_cast<std::chrono::microseconds> (std::chrono::nanoseconds (presentationNs));
           #endif

            auto state = session.capture();
            if (!enabled)
            {
                // Seed the solo Link timeline before discovery can find a session.
                if (wasEnabled || localTempo != lastLocalTempo)
                {
                    Session::setTempo (state, localTempo, time);
                    session.commit (state);
                }
                lastLocalTempo = localTempo;
                if (wasEnabled)
                {
                    playRequest.store (0, std::memory_order_relaxed);
                    tempoRequest.store (0.0, std::memory_order_relaxed);
                }
                wasEnabled = false;
                transportActive.store (localPlaying, std::memory_order_relaxed);
                return result;
            }

            const bool joining = !wasEnabled;
            wasEnabled = true;
            const bool syncTransport = session.syncsTransport();
            const bool sharedPlaying = Session::playing (state);
            // Joining the session or opting into Start/Stop must never replay
            // an old peer transport command.
            const bool remoteChange = !joining && syncTransport && wasTransportSynced && sharedPlaying != lastSharedPlaying;
            wasTransportSynced = syncTransport;
            if (joining)
            {
                wantsPlayback = localPlaying;
                launched = false;
                if (wantsPlayback)
                    Session::requestLaunch (state, time);
            }

            const auto requestedTempo = tempoRequest.exchange (0.0, std::memory_order_relaxed);
            if (requestedTempo > 0.0)
                Session::setTempo (state, requestedTempo, time);

            const auto requestedPlay = playRequest.exchange (0, std::memory_order_relaxed);
            if (requestedPlay != 0)
            {
                const bool shouldPlay = requestedPlay > 0;
                if (shouldPlay != wantsPlayback)
                {
                    if (std::abs (requestedPlay) == 2)
                        Session::setPlaying (state, shouldPlay, time);
                    if (shouldPlay)
                        Session::requestLaunch (state, time);
                    wantsPlayback = shouldPlay;
                    launched = false;
                }
            }
            else if (remoteChange && sharedPlaying != wantsPlayback)
            {
                wantsPlayback = sharedPlaying;
                launched = false;
                if (wantsPlayback)
                    Session::requestSharedLaunch (state);
            }
            lastSharedPlaying = Session::playing (state);
            if (joining || requestedTempo > 0.0 || requestedPlay != 0 || (remoteChange && wantsPlayback))
                session.commit (state);

            result.presentationTimeNs = presentationNs;
           #if JUCE_IOS
            const auto endTime = StandaloneMidiClock::nanosecondsToHostTicks (
                nextBlockNs + outputLatency, timebase.numer, timebase.denom);
           #else
            const auto endTime = std::chrono::duration_cast<std::chrono::microseconds> (
                std::chrono::nanoseconds (nextBlockNs + outputLatency));
           #endif
            result.ppq = Session::beat (state, time);
            const auto beatSpan = Session::beat (state, endTime) - result.ppq;
            const auto nominalBpm = Session::tempo (state);
            result.bpm = numSamples >= 64 && beatSpan > 0.0 ? beatSpan * sampleRate * 60.0 / numSamples : nominalBpm;
            result.reset = joining;
            if (wantsPlayback && !launched)
            {
                // Negative PPQ is the count-in. Start at the nearest sample
                // when beat zero first falls inside this block.
                const auto samplesUntilLaunch = -result.ppq * sampleRate * 60.0 / result.bpm;
                if (samplesUntilLaunch < numSamples - 0.5)
                {
                    result.firstPlaybackSample = static_cast<int> (std::llround (std::max (0.0, samplesUntilLaunch)));
                    if (samplesUntilLaunch >= -0.5)
                        result.ppq = -result.firstPlaybackSample * result.bpm / (60.0 * sampleRate);
                    result.reset = true;
                    launched = true;
                }
            }
            result.waiting = wantsPlayback && !launched;
            result.playing = wantsPlayback && launched;
            transportActive.store (wantsPlayback, std::memory_order_relaxed);
            return result;
        }

        void showSettings (juce::Component& target) override
        {
           #if JUCE_IOS
            auto* peer = target.getPeer();
            auto* view = peer != nullptr ? (__bridge UIView*) peer->getNativeHandle() : nil;
            auto* presenter = view.window.rootViewController;
            if (presenter == nil || presenter.presentedViewController != nil)
                return;
            auto* pane = [ABLLinkSettingsViewController instance:session.link];
            auto* navigation = [[UINavigationController alloc] initWithRootViewController:pane];
            __weak auto weakNavigation = navigation;
            pane.navigationItem.title = @"Ableton Link";
            pane.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc]
                initWithBarButtonSystemItem:UIBarButtonSystemItemDone
                primaryAction:[UIAction actionWithHandler:^(UIAction*)
                {
                    [weakNavigation dismissViewControllerAnimated:YES completion:nil];
                }]];
            navigation.modalPresentationStyle = UIModalPresentationFormSheet;
            settingsView = navigation;
            [presenter presentViewController:navigation animated:YES completion:nil];
           #else
            juce::PopupMenu menu;
            menu.setLookAndFeel (&target.getLookAndFeel());
            const auto [enabled, peers] = status();
            const auto heading = juce::String::fromUTF8 ("Ableton Link · ");
            menu.addSectionHeader (heading + (enabled ? juce::String (peers) + (peers == 1 ? " peer" : " peers") : "Disabled"));
            menu.addItem (1, "Enable Ableton Link", true, enabled);
            menu.addItem (2, "Start/Stop Sync", enabled, session.syncsTransport());
            const juce::WeakReference<LinkClock> weak (this);
            ui::showAnchoredPopup (menu, ui::footerMenuOptions (target), [weak] (int item)
            {
                if (auto* clock = weak.get())
                    clock->selectSetting (item);
            });
           #endif
        }

    private:
       #if !JUCE_IOS
        void selectSetting (int item)
        {
            if (item == 1)
            {
                const bool enabled = !session.enabled();
                session.link.enable (enabled);
                if (settings != nullptr)
                    settings->setValue ("linkEnabled", enabled);
            }
            if (item == 2)
            {
                const bool sync = !session.syncsTransport();
                session.link.enableStartStopSync (sync);
                if (settings != nullptr)
                    settings->setValue ("linkStartStopSync", sync);
            }
        }
        juce::PropertySet* settings;
        JUCE_DECLARE_WEAK_REFERENCEABLE (LinkClock)
       #else
        __weak UINavigationController* settingsView = nil;
       #endif
        Session session;
        mach_timebase_info_data_t timebase { 1, 1 };
        std::atomic<juce::uint64> outputLatencyNs { 0 };
        std::atomic<int> playRequest { 0 };
        std::atomic<double> tempoRequest { 0.0 };
        std::atomic<bool> transportActive { false };

        // Audio thread state.
        juce::uint64 nextBlockNs = 0;
        double lastLocalTempo = 0.0;
        bool wasEnabled = false;
        bool wasTransportSynced = false;
        bool lastSharedPlaying = false;
        bool wantsPlayback = false;
        bool launched = false;
    };
}

    std::unique_ptr<StandaloneLink> StandaloneLink::create (double initialTempo, juce::PropertySet* preferences)
    {
        return std::make_unique<LinkClock> (initialTempo, preferences);
    }
}
