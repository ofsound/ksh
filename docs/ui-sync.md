# Native editor and engine contract

The C++ engine is authoritative. `PluginProcessor::nativeUiState()` copies an immutable playback snapshot, refresh length, revision and document epoch under the engine mutex. No painting calls the engine or builds JSON.

`KshUiBridge` is a message-thread notification boundary. Engine callbacks increment its revision; they never call an editor while the engine mutex is held. The processor alone drains the audio hit FIFO and delivers typed `NativeHit` notifications. The native controller registers for its lifetime and removes its callback on destruction. Audio processing remains snapshot-only and lock-free.

The visible editor polls at 60 Hz. Only changed state or flash/playhead regions repaint. Pending cell edits carry their original expected values and document epoch. `applyNativeCellEdits` validates the entire batch before applying it and publishes one coherent snapshot. Recording wins conflicts; replacement documents cancel drafts and clear history.

The audio callback only enqueues hit notifications and sets work flags. The processor drains them on its 120 Hz message-thread timer, including with the editor closed. The visible editor also asks the processor to drain hits immediately before its playback repaint pass. Audio processing never calls `triggerAsyncUpdate()` to wake the UI.

`RealtimeMailbox` coalesces unconsumed updates through one lock-free atomic pointer. A new publication replaces and recycles the previous pending snapshot, while the consumer's current snapshot remains immutable. Retired slots are recycled on the producer side; all producer operations are serialized by the engine mutex. Bursts while audio processing is suspended still deliver the latest state on the next callback.

Deferred MIDI note-ons and note-offs use absolute deadlines on a monotonic callback sample timeline. Block sizes can change without shifting an event's deadline. Host seeks retain the existing cancellation rules, and playback reset clears the pending events and sample timeline together.

Discrete commands go directly to `dispatchUiEngineCommand`, without JavaScript or serialized command messages. Convert native zero-based indices to the engine's one-based command indices once at this boundary. Numeric APVTS gestures pair begin/end callbacks. Row audition calls `triggerRowFromUi` directly through the existing realtime queue.

History groups a gesture into one action and conditionally merges only its changed fields. External changes are rebased out of active transactions; undo/cancel preserve unrelated recording and automation. Histories are capped at 128 entries. Text fields retain their native text undo. Project loads use the existing strict v:1 persistence validator and atomic file replacement.

`KshLayout` supplies cell bounds and hit tests. The grid and generated preview remain together in one native editor. Overall scale is stored in project/host metadata at 50–120%. Imported theme metadata remains readable; all rendering uses the fixed Alternative palette and newly saved projects use `alt`.

The string command adapter is retained solely for the existing command-contract tests and compatibility with internal callers. It has no web transport, resources or browser objects.
