# KSH performance review — 2026-10-13

**Follow-up:** The three P1 findings below have been fixed. See [fixes and validation](fixes.md) for the implementation, regression coverage, and new measurements. The original review and baseline results below are retained for comparison.

Reviewed the current working tree, including the uncommitted native UI migration. No production code was changed. The strongest opportunities are timing correctness, realtime safety, and reducing message-thread work; raw note scheduling is already inexpensive.

## Findings, in priority order

### P1 — Deferred MIDI events arrive one buffer late

`source/engine/KshMidiPlayback.cpp:264,279,403-472`

When a newly evaluated step schedules a note beyond the current block, it stores the current-block offset unchanged. The pending queues have already been drained for this block. On the next callback, the offset is therefore interpreted relative to the wrong block. The same problem affects note-offs created after the note-off drain, including auditions. Existing pending note-ons are drained before note-offs, so a fix must account for that ordering rather than blindly subtracting a block at every insertion.

Reproduced at 48 kHz, 120 BPM, sixteenth notes, roll=2. Expected note-on samples: 0 and 3000; expected note-off samples: 2688 and 5688 (56 ms roll gate).

| Buffer | Actual second note-on | Lateness | Actual note-offs |
| --- | ---: | ---: | --- |
| 64 | 3064 | 1.33 ms | 2752, 5752 |
| 128 | 3128 | 2.67 ms | 2816, 5816 |
| 512 | 3512 | 10.67 ms | 3200, 6200 |
| 1024 | 4024 | 21.33 ms | 3712, 6712 |

Recommendation: give pending events an unambiguous time origin, preferably absolute sample deadlines, or normalize all deadlines once per block with explicit queue-drain ordering. Add absolute-timestamp assertions across blocks and varying block sizes. The existing test named “emits evenly spaced roll hits across blocks” checks exact timing within one large block but only checks eventual delivery across small blocks.

### P1 — Every hit-bearing callback can post to the OS message queue

`source/PluginProcessor.cpp:706-709`

`processBlock()` calls `triggerAsyncUpdate()` when hits were enqueued. JUCE's vendored `juce_AsyncUpdater.h:64-78` explicitly warns that this posts to the system queue and may block on realtime threads. This runs even with no editor attached. Fixed-size hit queues therefore do not make the whole callback realtime-safe.

Recommendation: keep the audio-to-message boundary to atomics/FIFOs and drain hits from a message-thread timer. Prefer draining directly before the editor's playback repaint pass to avoid unnecessary polling stages. Preserve processor-side work needed for generation/recording when the editor is closed. Measure tail callback time, not just mean scheduling time.

### P1 — Snapshot saturation silently loses the latest edit

`source/RealtimeMailbox.h:38-49`; `source/PluginProcessor.cpp:241-246,255-258`

There are four snapshot slots. When they are occupied, `publish()` silently returns, but the processor still records the engine version as successfully published. Retiring slots does not retry the dropped version. Rapid changes between callbacks, or changes while processing is suspended, can leave MIDI playing an older pattern than the UI until another engine change publishes successfully.

Reproduction: publish integer values 1 through 10 without consumption; `current()` returns 4, and still returns 4 after retired slots are drained. Once the consumer holds a snapshot, only three other slots remain available for a burst.

Recommendation: return publication success and retain/retry the newest unpublished version, or use a proven latest-value handoff that coalesces superseded updates. Keep recycling/destruction on the producer side and add a saturation/recovery test at processor level.

### P2 — Input/output handling still permits heap allocation on the audio thread

`source/PluginProcessor.cpp:749-751,790-800`; `source/engine/KshMidiPlayback.cpp:253-264`

Input classification constructs an owning `MidiMessage` for every event. Large SysEx messages allocate through `MidiMessage::allocateSpace()`, even if the message is only being passed through. Filtering repeats the construction. Output `MidiBuffer::addEvent()` can also grow its backing array; only the input scratch buffer is explicitly reserved, and `swapWith()` exchanges that reserved storage with the host buffer. An 8192-byte initial reservation is not a general no-allocation guarantee.

Recommendation: classify short channel messages directly from non-owning metadata, and design bounded, preallocated output/filter storage with a clear policy for large input and overflow. Validate capacities and wrapper behavior with allocation instrumentation before claiming an allocation-free callback. Ordinary short note messages themselves fit inline; the SysEx issue is conditional, not an allocation on every note.

### P2 — Native preview invalidation still constructs discarded JSON

`source/engine/KickSnareHatEngine.cpp:909-917,956-1010`; `source/KshUiBridge.h:17`

`flushPreview()` builds the complete JSON preview, including source settings and generated cells, then invokes a callback whose native bridge ignores the payload and merely increments a revision. Generation refreshes and edits pay for allocations/serialization that the typed native UI does not use. This is on the message thread, where it competes with flashes and painting.

Recommendation: provide a payload-free invalidation callback for the native editor. Retain serialization only for callers that actually require it. Its isolated cost was not measured in this review.

### P2 — Revision polling synchronizes the whole editor twice

`source/ui/KshEditorController.cpp:192-200,252-265`; `source/ui/KshNativeEditor.cpp:83,108-165`

On a revision change, `poll()` invokes `refresh()`, which calls `notify()`, then calls `notify()` again at the end. Both call `NativeEditor::sync()`, including control updates, accessibility descriptions for 256 cells, layout, and preview invalidation. Repaint requests may coalesce, but the synchronization work already occurred twice.

Recommendation: issue one notification after polling/flush completes, then narrow synchronization to changed state and avoid layout when geometry is unchanged. Preserve gesture/conflict and document-epoch behavior.

## Visual feedback timing

The hit callback writes a flash expiry of “message-thread receipt time + 200 ms”; repaint occurs separately in a 60 Hz editor timer. The audio hit payload carries no actual sample offset or due timestamp. Therefore immediate message delivery does not mean immediate or sample-aligned visual feedback. A hit may wait almost 16.7 ms for the next timer, plus message-thread contention and display presentation. Conversely, a hit scheduled near the end of a large audio block can be shown early because its notification is published while the block is being processed.

Recommendation: timestamp hits in the audio timeline and consume/coalesce due hits once per visual frame. Use deadlines to expire stale notifications after message-thread stalls. Preserve the existing dirty-cell repaint strategy. A 200 ms Boolean flash also merges rapid retriggers of the same cell into a continuous highlight; use timestamp/intensity changes if each roll hit should be visibly distinct.

## Measurements

Release build `Builds-native-release`, existing flags unchanged. `cmake --build Builds-native-release --target Tests Benchmarks -j 4` reported no work to do. Full test suite: **153 cases, 2546 assertions passed**. Probe compiled with the same Release flags and linked against the existing benchmark objects without warnings.

| Existing UI benchmark | Mean |
| --- | ---: |
| Dense 32-step full editor snapshot | 19.79 ms |
| Eight-channel narrow flash-strip snapshot | 1.09 ms |
| 32-cell edit and publish | 2.51 ms |

The full snapshot exceeds a 16.67 ms frame budget; the narrow flash path is much cheaper. These are offscreen `createComponentSnapshot()` measurements, including image creation, not live display frame timings. The idle-poll benchmark measures an unchanged fast path and does not represent active playback, generation, or editing. The edit benchmark also does not exercise a concurrently draining audio consumer, which matters given snapshot saturation.

Dense scheduler probe: eight channels, 32 steps, every cell enabled, roll=8, probability=100%, 300 BPM, 32nd-note triplets, 48 kHz. 30,000 timed callbacks after 1,000 warmup callbacks; output MIDI buffer reserved to 64 KiB. Times include clock measurement overhead and ordinary system interference.

| Samples/block | Mean | p99 | Observed max | Block duration |
| --- | ---: | ---: | ---: | ---: |
| 64 | 0.84 µs | 1.50 µs | 43.17 µs | 1.33 ms |
| 128 | 1.15 µs | 2.21 µs | 41.33 µs | 2.67 ms |
| 512 | 8.56 µs | 32.96 µs | 1138.50 µs | 10.67 ms |
| 1024 | 30.79 µs | 68.38 µs | 122.58 µs | 21.33 ms |

These numbers cover `MidiPlaybackRunner`, not full processor callbacks, OS message posting, live UI interaction, or DAW routing. The isolated 512-sample maximum is not evidence of a reproducible scheduler stall. No allocation tracer or Logic/Ableton end-to-end latency capture was run. Current timing bugs are present during these measurements.

## Reproduction and next validation

Raw evidence is beside this file: `release-tests.txt`, `release-ui-benchmarks.txt`, `release-playback-probes.txt`. The diagnostic source is `playback-probe.cpp`; it prints observations rather than asserting the incorrect timings as expected behavior.

From the repository root:

```sh
./Builds-native-release/Tests
./Builds-native-release/Benchmarks 'Native editor performance' --benchmark-samples 20 --benchmark-warmup-time 100
python3 docs/performance-review/build-probe.py
```

The helper requires the existing Ninja Release build and reuses its exact compile/link commands, writing its executable to a temporary directory. It does not install plugins.

Fix the three P1 issues first. Then validate full processor callback p99/max and allocations at 64/128/512/1024 samples, with UI closed/open, dense rolls, incoming SysEx, automation, recording, and fast edits. Measure visual hit-to-frame latency separately in a live host. Preserve host sample offsets; DAW routing and audio-device buffering remain outside this scheduler's control.
