# iOS standalone MIDI timing

The standalone and AUv3 already generate MIDI on the audio render thread. The
standalone previously handed its output to `AudioProcessorPlayer` and JUCE's
`MidiOutput` background scheduler. In this JUCE checkout that scheduler uses a
locking, allocating event queue, waits at millisecond resolution, and ultimately
sends CoreMIDI packets stamped with the actual send time. This loses the original
audio-clock anchor and introduces delivery jitter.

## Current output path

On iOS only, the standalone holder attaches `StandaloneIOSMidiOutput` to the
processor. After processing, it receives the same MIDI buffer and sample offsets
that a host would receive, together with `AudioPlayHead::PositionInfo`'s host time
(in nanoseconds). JUCE's player supplies that time from the audio device callback.

`StandaloneMidiClock` adds a fixed scheduling lead of one configured audio buffer,
with a 2 ms minimum. At 48 kHz / 256 samples this is approximately 5.33 ms. Event
offsets are converted from samples to nanoseconds, then to Mach host-clock ticks
using the device timebase. CoreMIDI receives future timestamps rather than a
sequence of immediate sends. The lead is intentional added latency, allowing the
first event in a block to be submitted before its deadline. It is reset when the
audio device is prepared, not on every callback or UI update.

If a hardware timestamp is unavailable, the clock advances by rendered sample
count and reanchors after a substantial interruption. Hardware timestamps take
precedence when available. A missed render deadline cannot be repaired by this
output path, and Bluetooth/receiving-app behavior still affects delivered timing.

The native sender converts raw MIDI bytes to MIDI 1 UMP without constructing
allocating SysEx copies. It batches packets into fixed 4 KB storage and submits
full batches before continuing. Physical/Bluetooth destinations use
`MIDISendEventList`; KSH Output uses `MIDIReceivedEventList`. The existing network
bridge preserves those timestamps when relaying KSH Output to the Network Session.
There is no KSH sleeping MIDI worker or newly introduced render-thread lock.

The iOS wrapper keeps `AudioProcessorPlayer`'s MIDI output disconnected, including
when the audio device restarts, to prevent duplicate delivery. Desktop output and
hosted AUv3 output retain their existing paths.

## Lifetime and stopping

The holder owns the native sink. Sink replacement is serialised using the same
processor callback lock that `AudioProcessorPlayer` already holds; route lookup,
port creation, and destruction happen off the render thread. The sink is detached
before processor/holder destruction and output changes. Unchanged routes do not
reset the timing clock. Endpoint identity is checked again on device changes.

Transport stop/restart uses the engine's existing note releases in the timestamped
stream. Audio-device stop and route changes flush queued destination output and
send sustain-off/all-notes-off. Virtual-source events already published to other
clients cannot be recalled; a final timestamped panic follows the last submitted
event as well as an immediate panic. The network bridge also flushes its destination
and sends sustain-off/all-notes-off when disconnected.

## Verification

`./Builds/Tests '[timing][standalone-midi]'` covers hardware-clock scheduling under
simulated callback jitter at 44.1/48/96 kHz and 64–1024 samples, fallback clock
continuity, interruption and sample-rate changes, long-uptime timebase conversion,
MIDI clock/program/note/SysEx conversion, sink replacement, transport stop/restart,
and exclusion of the sink from hosted plugins.

For an end-to-end comparison, build **Release**, use the same iPad, Bluetooth
connection, receiving Mac instrument and audio-buffer setting for standalone and
AUM, and disable swing, timing humanize, nudge and rolls. Capture a steady pattern
for at least a minute in each configuration, then repeat while interacting with
the UI and with KSH in the background. Compare interval jitter separately from
average latency. Also exercise stop/restart, output switching, Bluetooth reconnect,
and audio interruptions to check for duplicates and stuck notes. Unit tests and
SDK compilation do not establish the actual Bluetooth improvement.

CoreMIDI reference: [MIDISendEventList](https://developer.apple.com/documentation/coremidi/midisendeventlist(_:_:_:))
schedules future-dated events; [MIDIReceivedEventList](https://developer.apple.com/documentation/coremidi/midireceivedeventlist(_:_:))
publishes timestamped events from a virtual source.
