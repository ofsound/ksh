#import <CoreMIDI/CoreMIDI.h>
#include "StandaloneMidiRouting.h"

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
        juce::MidiDeviceInfo getJuceEndpointInfo (MIDIEndpointRef endpoint)
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
            if (info == juce::MidiDeviceInfo() || result.name.startsWithIgnoreCase (info.name))
                return result;
            return { (info.name + " " + result.name).trimEnd(), info.identifier + " " + result.identifier };
        }

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
        alignas (MIDIEventList) std::byte storage[1024] {};
        auto* events = reinterpret_cast<MIDIEventList*> (storage);
        auto* packet = MIDIEventListInit (events, kMIDIProtocol_1_0);
        for (UInt32 channel = 0; channel < 16 && packet != nullptr; ++channel)
        {
            const UInt32 allNotesOff = 0x20000000u | ((0xB0u | channel) << 16) | (123u << 8);
            packet = MIDIEventListAdd (events, sizeof (storage), packet, 0, 1, &allNotesOff);
        }
        MIDISendEventList (impl->output, impl->destination, events);
        impl.reset();
    }

    bool StandaloneNetworkMidiForwarder::isActive() const
    {
        return impl != nullptr;
    }
}
