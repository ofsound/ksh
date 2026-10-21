# Unquantized recording investigation

Status: investigation and proposed behavior, 2026-10-20. No recording or UI behavior changed.

User clarification: preserving human feel is the goal; literal sample accuracy is not required. The existing one-hit-per-channel/step storage limit can remain. The recommended first implementation therefore uses the existing integer nudge data, without a new precision field or multi-event model.

## Feasibility

Unquantized recording can use a cell plus a signed timing offset. A grid cell becomes an address for the hit rather than a requirement that the hit sound on that grid line. This fits KSH's existing nudge rendering, early-note lookahead, immutable playback snapshots, and recording undo journal.

The practical feature is feasible with today's integer nudges. At 120 BPM on a sixteenth-note grid, a hit played 7 ms late becomes approximately +6% nudge and replays 7.5 ms late, preserving the performance rather than snapping it to zero. A hit played early stores a negative nudge. Ordinary grid addressing still chooses which cell holds the hit.

Sample-resolution MIDI capture and replay would also be feasible relative to the processor's supplied transport and MIDI timestamps, but are outside this first implementation. This does not establish sample-accurate physical key/pad detection or external MIDI delivery. QWERTY currently receives its timestamp when the message-thread key handler executes; a stalled message thread loses the original physical press time.

The current integer nudge cannot store sample-accurate timing. `Cell::nudge`, its setter, cloning, persistence validation, inspector values, and recording undo all use integer percentages of a step. At 48 kHz and 120 BPM, a sixteenth-note step is 6,000 samples and one nudge increment is 60 samples (1.25 ms). Rounding an arbitrary hit to that representation can introduce 30 samples of error. At 60 BPM on a quarter-note grid, the maximum error is 240 samples (5 ms).

## Current paths

- `PluginProcessor::consumeMidiInput` converts block PPQ plus the MIDI event's sample offset into event PPQ, immediately rounds to a step, and queues only source/channel/step/velocity/session. The fractional timing is discarded before the message thread sees it.
- `quantizedStepForUiTrigger` extrapolates from the latest block's PPQ and wall-clock publication time, then also rounds to a step. Native ASDFGHJK handlers call `triggerRowFromUi` directly.
- `recordPatternCellLocked` writes a cell with zero nudge. Recording's conditional undo journal already includes nudge.
- `MidiPlaybackRunner::evaluateGlobalStep` adds swing and humanization, then cell nudge; `scheduleHit` converts the result to a sample offset. Positive delays and anticipated early hits already use absolute sample deadlines across callbacks.
- Playback currently rounds the nominal step offset and the hit delay separately. A sample-accuracy implementation should calculate the complete deadline before rounding; preserving a precise offset alone does not prove exact round-trip playback.
- Standalone input uses JUCE's `MidiMessageCollector`, which maps incoming timestamped events from the preceding callback interval into the next processing block. The processor's standalone PPQ advances per callback. The relationship between that input mapping, QWERTY's wall-clock estimate, and monitored output needs a targeted timing test; current processor tests bypass the device collector.

## Recommended first implementation

Keep `Cell::nudge` as the existing integer percentage of a step. Calculate the early/late residual at capture time, round it to the nearest whole percentage, and carry it in `PatternRecordEvent` alongside the cell address and velocity. Apply that nudge when writing the recorded cell rather than always writing zero.

The quantization unit becomes 1% of a step rather than a whole step. At 120 BPM and sixteenth notes that is 1.25 ms, with at most 0.625 ms of rounding error. This is a reasonable compromise for the requested human feel. Resolution gets coarser at slower tempos and wider grids; this tradeoff is accepted rather than hidden under a sample-accuracy claim.

This reuses existing nudge persistence, graphics, selection edits, copy, export, and conditional undo. Musical-time storage also retains relative placement when tempo or sample rate changes. No new timing-persistence format or arbitrary-event recording model is needed.

For each hit:

1. Capture event time against one coherent transport anchor, including document/session and discontinuity identity. Use host-provided sample offsets for plugin MIDI. Establish and test the standalone collector-to-transport mapping rather than applying a blanket one-block correction to all wrappers.
2. Find the intended playback occurrence and destination cell, accounting for the armed pattern's rate/length, channel loop, playback direction, and host loop wrapping. Current global-step modulo mapping is insufficient to establish correct reverse/ping-pong recording.
3. Store the difference between the captured event and that occurrence's deterministic playback onset, rounded to an integer nudge percentage. Because playback adds swing, account for swing in this calculation; adding a raw offset to an already swung onset would apply groove twice. Choose a nearby representable occurrence rather than silently clipping an out-of-range residual at a loop or swing boundary.
4. Carry that timing decision in the bounded record event. A later quantization-toggle change must not reinterpret already queued hits. Apply the value and undo journal on the message thread, using the existing independent MIDI/UI producer queues.
5. Keep audition immediate and independent of the recording mode. Test and prevent a second sequencer attack when a newly recorded cell becomes eligible during the same pass.

With swing and timing humanize disabled, input-to-replay positions at the same tempo/sample rate should agree within half of one nudge increment, allowing separately measured sample-rounding tolerance. With swing enabled, recorded timing should match within that tolerance at the captured swing setting; later changing swing intentionally changes the groove. Random timing humanize remains an additional playback transformation, so it cannot promise exact reproduction while enabled.

Literal sample accuracy is an optional future project. It would require a canonical high-precision musical offset throughout cells, persistence, cloning, graphics, selection edits, undo, export, and sample scheduling. Do not silently put fractional values into `cellNudges`, whose current validator requires integers. That expansion is unnecessary for the clarified request.

## Record lock interaction

Place a small native vector padlock just above and to the right of the Record circle, like a superscript badge. Give it its own accessible button and non-overlapping click target; do not make the tiny visible glyph the entire hit area. Check desktop scaling and the compact pattern panel separately.

- Closed lock: **Record quantization on**. New hits snap to the chosen deterministic grid onset, with no captured residual offset. Use this as the default to preserve existing behavior.
- Open lock: **Record quantization off**. New hits retain their early/late timing as existing integer nudge data. This means off the main grid, with the accepted fine rounding described above.
- Record remains the arm/disarm action. The lock changes only the capture mode and does not arm recording or rewrite existing cells.
- Permit toggling while armed; the new mode applies to subsequent hits only.
- Tooltips/accessibility names should explicitly say “Record quantization on/off,” since a lock alone could be mistaken for record protection or a channel lock.
- Keep the mode processor-owned so closing/reopening the editor does not reset it. Persist it as an explicitly validated optional setting if project/host restoration should retain it; no new host-automatable macro parameter is needed.

## Limits and verification

One cell holds one attack timing and velocity. Repeated hits assigned to the same channel/step overwrite each other; rolls cannot encode arbitrary flam spacings and velocities. The user explicitly accepts this existing limitation. Preserve it; do not expand storage to multiple events or automatically convert extra hits into rolls.

Required regression coverage before claiming accuracy:

- Captured MIDI timing replays within the integer-nudge rounding bound with irregular as well as 64/128/256/512/1024-sample blocks, including offsets not divisible by a percent-of-step increment. Closed-lock recording retains the current zero-nudge behavior.
- Negative offsets, the first/last pattern step, host loop seams, transport stop/seek/restart, armed-pattern changes, and tempo/rate/sample-rate changes have explicit behavior.
- Swing is accounted for once; straight playback, swung playback, and early-note lookahead share the same timing calculation as the grid and MIDI export.
- Recording undo/redo, manual-edit conflicts, save/reload, clone/copy/generation, and older integer-nudge projects retain captured nudge values correctly. Repeated hits keep the existing last-write-per-cell behavior.
- Quantization toggles affect only future hits; UI congestion delays presentation rather than changing already captured MIDI timing.
- Monitor output has no duplicate attacks, and standalone timestamp mapping is tested through its device/collector boundary rather than only by injecting already positioned plugin MIDI.
- QWERTY tests distinguish handler-time accuracy from physical keypress accuracy. MIDI loopback tests measure external latency/jitter separately from internal sample placement.

Validation performed for this investigation: existing `Builds/Tests "[nudge]"` passed 146 assertions in 6 cases; existing `Builds/Tests "[record]"` passed 1,135 assertions in 12 cases. These are the existing built executable's tests, not new capture-to-replay or hardware latency measurements. A separate arithmetic enumeration confirmed the integer-nudge error examples above. No source or test changes were made, so no rebuild was required by this investigation.
