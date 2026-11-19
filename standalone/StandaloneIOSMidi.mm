#import <CoreMIDI/CoreMIDI.h>
#include "StandaloneMidiRouting.h"
#include "StandaloneMidiClock.h"
#include <mach/mach_time.h>

namespace ksh
{
    namespace
    {
        juce::MidiDeviceInfo getObjectInfo (MIDIObjectRef object)
        {
            juce::MidiDeviceInfo result;
            CFStringRef name = nullptr;
            if (MIDIObjectGetStringProperty (object, kMIDIPropertyName, &name) == noErr && name != nullptr)
            {
                result.name = juce::String::fromCFString (name);
                CFRelease (name);
            }
            SInt32 identifier = 0;
            if (MIDIObjectGetIntegerProperty (object, kMIDIPropertyUniqueID, &identifier) == noErr)
                result.identifier = juce::String (identifier);
            return result;
        }

        // Must match JUCE's CoreMIDI identifier for an unconnected, non-external
        // endpoint so a JUCE-listed duplicate of the session can be hidden.
        juce::MidiDeviceInfo getJuceEndpointInfo (MIDIEndpointRef endpoint, bool external = false)
        {
            MIDIEntityRef entity = 0;
            MIDIEndpointGetEntity (endpoint, &entity);
            auto result = getObjectInfo (endpoint);
            if (entity == 0)
                return result;
            if (result == juce::MidiDeviceInfo())
                result = getObjectInfo (entity);
            MIDIDeviceRef device = 0;
            MIDIEntityGetDevice (entity, &device);
            if (device == 0)
                return result;
            const auto info = getObjectInfo (device);
            if (external && device != 0 && MIDIDeviceGetNumberOfEntities (device) < 2)
            {
                result.name = info.name;
                return result;
            }
            if (info == juce::MidiDeviceInfo() || result.name.startsWithIgnoreCase (info.name))
                return result;
            return { (info.name + " " + result.name).trimEnd(), info.identifier + " " + result.identifier };
        }

        juce::MidiDeviceInfo getConnectedEndpointInfo (MIDIEndpointRef endpoint)
        {
            juce::MidiDeviceInfo result;
            CFDataRef connections = nullptr;
            MIDIObjectGetDataProperty (endpoint, kMIDIPropertyConnectionUniqueID, &connections);
            if (connections != nullptr)
            {
                const auto count = CFDataGetLength (connections) / static_cast<CFIndex> (sizeof (MIDIUniqueID));
                const auto* ids = reinterpret_cast<const UInt32*> (CFDataGetBytePtr (connections));
                for (CFIndex i = 0; i < count; ++i)
                {
                    MIDIObjectRef object = 0;
                    MIDIObjectType type {};
                    if (MIDIObjectFindByUniqueID (static_cast<MIDIUniqueID> (CFSwapInt32BigToHost (ids[i])), &object, &type) != noErr)
                        continue;
                    const auto info = type == kMIDIObjectType_ExternalSource || type == kMIDIObjectType_ExternalDestination
                                          ? getJuceEndpointInfo (object, true) : getObjectInfo (object);
                    if (info == juce::MidiDeviceInfo())
                        continue;
                    result.name += (result.name.isEmpty() ? "" : ", ") + info.name;
                    result.identifier += (result.identifier.isEmpty() ? "" : ", ") + info.identifier;
                }
                CFRelease (connections);
            }
            return result == juce::MidiDeviceInfo() ? getJuceEndpointInfo (endpoint) : result;
        }

        MIDIEndpointRef findDestination (const juce::MidiDeviceInfo& info)
        {
            if (info.identifier.isEmpty())
                return 0;
            // Match JUCE's full identifier, including external-device connections;
            // never guess from a display name (two keyboards can share a name).
            for (ItemCount i = 0; i < MIDIGetNumberOfDestinations(); ++i)
                if (const auto endpoint = MIDIGetDestination (i); getConnectedEndpointInfo (endpoint).identifier == info.identifier)
                    return endpoint;
            return 0;
        }

       #if JUCE_IOS
        MIDINetworkSession* enableSession()
        {
            // CoreMIDI owns this shared session. Keep it enabled for other MIDI apps after KSH closes.
            MIDINetworkSession* session = [MIDINetworkSession defaultSession];
            session.connectionPolicy = MIDINetworkConnectionPolicy_Anyone;
            session.enabled = YES;
            return session;
        }

        // The session can report no endpoint while its driver port already exists.
        MIDIEndpointRef findNetworkDestination (MIDINetworkSession* session)
        {
            if (const auto endpoint = [session destinationEndpoint]; endpoint != 0)
                return endpoint;
            for (ItemCount i = 0; i < MIDIGetNumberOfDestinations(); ++i)
            {
                const auto endpoint = MIDIGetDestination (i);
                MIDIEntityRef entity = 0;
                MIDIDeviceRef device = 0;
                if (MIDIEndpointGetEntity (endpoint, &entity) == noErr && MIDIEntityGetDevice (entity, &device) == noErr
                    && getObjectInfo (device).name == "Network")
                    return endpoint;
            }
            return 0;
        }

       #endif

        MIDIEndpointRef findSource (const juce::MidiDeviceInfo& source)
        {
            MIDIObjectRef object = 0;
            MIDIObjectType type {};
            if (source.identifier.isNotEmpty()
                && MIDIObjectFindByUniqueID ((MIDIUniqueID) source.identifier.getIntValue(), &object, &type) == noErr
                && type == kMIDIObjectType_Source)
                return (MIDIEndpointRef) object;
            for (ItemCount i = 0; i < MIDIGetNumberOfSources(); ++i)
                if (const auto endpoint = MIDIGetSource (i); getObjectInfo (endpoint).name == source.name)
                    return endpoint;
            return 0;
        }
    }

   #if JUCE_IOS
    juce::MidiDeviceInfo enableStandaloneNetworkMidi()
    {
        const auto endpoint = findNetworkDestination (enableSession());
        if (endpoint == 0)
            return {};
        auto info = getJuceEndpointInfo (endpoint);
        if (info.identifier.isEmpty())
            return {};
        info.name = "Network Session";
        return info;
    }

   #endif

    struct StandaloneCoreMidiOutput::Impl
    {
        juce::MidiDeviceInfo info;
        bool virtualSource = false;
        MIDIClientRef client = 0;
        MIDIPortRef port = 0;
        MIDIEndpointRef endpoint = 0;
        uint8_t group = 0;
        mach_timebase_info_data_t timebase {};
        StandaloneMidiClock clock;
        MIDITimeStamp lastScheduledTime = 0;

        ~Impl()
        {
            if (port != 0)
                MIDIPortDispose (port);
            if (client != 0)
                MIDIClientDispose (client);
        }

        API_AVAILABLE (macos (11.0))
        void submit (const MIDIEventList* events) const
        {
            if (virtualSource)
                MIDIReceivedEventList (endpoint, events);
            else
                MIDISendEventList (port, endpoint, events);
        }

        API_AVAILABLE (macos (11.0))
        void silence (MIDITimeStamp time) const
        {
            alignas (MIDIEventList) std::byte storage[1024] {};
            auto* events = reinterpret_cast<MIDIEventList*> (storage);
            auto* packet = MIDIEventListInit (events, kMIDIProtocol_1_0);
            for (UInt32 channel = 0; channel < 16; ++channel)
            {
                // Sustain off before all-notes-off, including MIDI thru notes.
                const UInt32 words[] { 0x20b04000u | (UInt32 (group) << 24) | (channel << 16),
                                       0x20b07b00u | (UInt32 (group) << 24) | (channel << 16) };
                packet = MIDIEventListAdd (events, sizeof (storage), packet, time, 2, words);
            }
            submit (events);
        }
    };

    bool StandaloneCoreMidiOutput::isAvailable()
    {
        if (@available (macOS 11.0, *))
            return true;
        return false;
    }

    StandaloneCoreMidiOutput::StandaloneCoreMidiOutput() = default;
    StandaloneCoreMidiOutput::~StandaloneCoreMidiOutput() { stop(); }

    bool StandaloneCoreMidiOutput::matches (const juce::MidiDeviceInfo& info, bool virtualSource) const
    {
        return impl != nullptr && impl->info == info && impl->virtualSource == virtualSource
               && impl->endpoint == (virtualSource ? findSource (info) : findDestination (info));
    }

    juce::String StandaloneCoreMidiOutput::open (const juce::MidiDeviceInfo& info, bool virtualSource)
    {
        stop();
        impl.reset();
        auto next = std::make_unique<Impl>();
        next->info = info;
        next->virtualSource = virtualSource;
        next->endpoint = virtualSource ? findSource (info) : findDestination (info);
        if (next->endpoint == 0)
            return "MIDI endpoint not found: " + info.name;
        if (mach_timebase_info (&next->timebase) != KERN_SUCCESS || next->timebase.numer == 0 || next->timebase.denom == 0)
            return "Host clock unavailable";
        if (!virtualSource)
        {
            if (MIDIClientCreateWithBlock (CFSTR ("KSH Timestamped MIDI"), &next->client, nil) != noErr
                || MIDIOutputPortCreate (next->client, CFSTR ("KSH Timestamped Out"), &next->port) != noErr)
                return "Could not create MIDI output port";
            if (@available (macOS 15.0, iOS 18.0, *))
            {
                SInt32 bitmap = 0;
                if (MIDIObjectGetIntegerProperty (next->endpoint, kMIDIPropertyUMPActiveGroupBitmap, &bitmap) == noErr && bitmap != 0)
                    for (uint8_t group = 0; group < 16; ++group)
                        if ((bitmap & (1 << group)) != 0)
                        {
                            next->group = group;
                            break;
                        }
            }
        }
        impl = std::move (next);
        return {};
    }

    void StandaloneCoreMidiOutput::prepare (double sampleRate, int maximumBlockSize)
    {
        if (impl != nullptr)
        {
            stop();
            impl->clock.prepare (sampleRate, maximumBlockSize);
        }
    }

    void StandaloneCoreMidiOutput::send (const juce::MidiBuffer& midi, juce::Optional<juce::uint64> hostTimeNs, int numSamples,
                                        juce::Optional<juce::uint64> presentationTimeNs)
    {
        if (impl == nullptr)
            return;
        const auto now = mach_absolute_time();
        const auto nowNs = StandaloneMidiClock::nanosecondsToHostTicks (now, impl->timebase.denom, impl->timebase.numer);
        const auto scheduledTime = impl->clock.beginBlock (hostTimeNs, nowNs, numSamples);
        const auto blockTime = presentationTimeNs.orFallback (scheduledTime);
        if (@available (macOS 11.0, *))
        {
            alignas (MIDIEventList) std::byte storage[4096] {};
            auto* events = reinterpret_cast<MIDIEventList*> (storage);
            auto* end = MIDIEventListInit (events, kMIDIProtocol_1_0);
            forEachStandaloneMidiPacket (midi, impl->group, [&] (int offset, const juce::ump::View& view)
            {
                const auto time = StandaloneMidiClock::nanosecondsToHostTicks (impl->clock.eventTime (blockTime, offset),
                                                                             impl->timebase.numer, impl->timebase.denom);
                const auto* words = reinterpret_cast<const UInt32*> (view.data());
                auto* added = MIDIEventListAdd (events, sizeof (storage), end, time, view.size(), words);
                if (added == nullptr)
                {
                    impl->submit (events);
                    end = MIDIEventListInit (events, kMIDIProtocol_1_0);
                    added = MIDIEventListAdd (events, sizeof (storage), end, time, view.size(), words);
                }
                jassert (added != nullptr); // A UMP is at most 16 bytes.
                end = added;
                impl->lastScheduledTime = juce::jmax (impl->lastScheduledTime, time);
            });
            if (events->numPackets != 0)
                impl->submit (events);
        }
    }

    void StandaloneCoreMidiOutput::stop()
    {
        if (impl == nullptr || impl->lastScheduledTime == 0)
            return;
        const auto now = mach_absolute_time();
        if (!impl->virtualSource)
            MIDIFlushOutput (impl->endpoint);
        if (@available (macOS 11.0, *))
        {
            impl->silence (now);
            // Published virtual events cannot be recalled from other clients.
            if (impl->lastScheduledTime > now)
                impl->silence (impl->lastScheduledTime + 1);
        }
        impl->lastScheduledTime = 0;
    }

   #if JUCE_IOS
    // JUCE 8 on iOS 18+ treats the session's port as a multi-group UMP device
    // and gives it no MIDI 1.0 identifier, so MidiOutput cannot open it.
    struct StandaloneNetworkMidiForwarder::Impl
    {
        MIDIClientRef client = 0;
        MIDIPortRef input = 0;
        MIDIPortRef output = 0;
        MIDIEndpointRef source = 0;
        MIDIEndpointRef destination = 0;

        ~Impl()
        {
            if (input != 0)
                MIDIPortDispose (input);
            if (output != 0)
                MIDIPortDispose (output);
            if (client != 0)
                MIDIClientDispose (client);
        }
    };

    StandaloneNetworkMidiForwarder::StandaloneNetworkMidiForwarder() = default;
    StandaloneNetworkMidiForwarder::~StandaloneNetworkMidiForwarder() { stop(); }

    juce::String StandaloneNetworkMidiForwarder::start (const juce::MidiDeviceInfo& source)
    {
        stop();
        const auto destination = findNetworkDestination (enableSession());
        if (destination == 0)
            return "Network Session destination not found";
        const auto sourceEndpoint = findSource (source);
        if (sourceEndpoint == 0)
            return "Source not found: " + source.name + " (" + source.identifier + ")";

        auto next = std::make_unique<Impl>();
        next->source = sourceEndpoint;
        next->destination = destination;
        const auto failed = [] (const char* step, OSStatus status) { return juce::String (step) + " failed (" + juce::String ((int) status) + ")"; };
        if (const auto status = MIDIClientCreateWithBlock (CFSTR ("KSH Network Session"), &next->client, nil); status != noErr)
            return failed ("MIDIClientCreate", status);
        if (const auto status = MIDIOutputPortCreate (next->client, CFSTR ("KSH Network Out"), &next->output); status != noErr)
            return failed ("MIDIOutputPortCreate", status);
        const auto outputPort = next->output;
        if (const auto status = MIDIInputPortCreateWithProtocol (next->client, CFSTR ("KSH Network In"), kMIDIProtocol_1_0, &next->input,
                                                                 ^(const MIDIEventList* events, void*) {
                                                                     MIDISendEventList (outputPort, destination, events);
                                                                 });
            status != noErr)
            return failed ("MIDIInputPortCreate", status);
        if (const auto status = MIDIPortConnectSource (next->input, next->source, nullptr); status != noErr)
            return failed ("MIDIPortConnectSource", status);
        impl = std::move (next);
        return {};
    }

    void StandaloneNetworkMidiForwarder::stop()
    {
        if (impl == nullptr)
            return;
        MIDIPortDisconnectSource (impl->input, impl->source);
        MIDIFlushOutput (impl->destination);
        alignas (MIDIEventList) std::byte storage[1024] {};
        auto* events = reinterpret_cast<MIDIEventList*> (storage);
        auto* packet = MIDIEventListInit (events, kMIDIProtocol_1_0);
        for (UInt32 channel = 0; channel < 16 && packet != nullptr; ++channel)
        {
            const UInt32 silence[] { 0x20b04000u | (channel << 16), 0x20b07b00u | (channel << 16) };
            packet = MIDIEventListAdd (events, sizeof (storage), packet, 0, 2, silence);
        }
        MIDISendEventList (impl->output, impl->destination, events);
        impl.reset();
    }

    bool StandaloneNetworkMidiForwarder::isActive() const
    {
        return impl != nullptr;
    }
   #endif
}
