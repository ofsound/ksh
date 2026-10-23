#import <CoreMIDI/CoreMIDI.h>

namespace ksh
{
    void enableStandaloneNetworkMidi()
    {
        // CoreMIDI owns this shared session. Keep it enabled for other MIDI apps
        // after KSH closes, and only activate it when the user opens MIDI routing.
        MIDINetworkSession* session = [MIDINetworkSession defaultSession];
        session.connectionPolicy = MIDINetworkConnectionPolicy_Anyone;
        session.enabled = YES;
    }
}
