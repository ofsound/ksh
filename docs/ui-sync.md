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

iOS standalone uses automatic viewport fitting instead of the saved zoom. Landscape viewports up to 1100 × 500 points use a compact layout with eight channel rows, manual 16-step pages, an always-visible inspector and auxiliary control pages; see [iphone-release.md](iphone-release.md). Compact geometry and gesture modes are editor-local and never persisted. Wide viewports retain the original layout. The fullscreen wrapper owns editor bounds, with no fixed aspect ratio or minimum zoom constraint. The selected native layout is scaled uniformly and centred inside the available safe area on every resize or preferred-layout change. The scale control displays a read-only `Fit`; fitting does not overwrite project zoom metadata. Desktop standalone and hosted plugins retain their adjustable scale.

The footer `Tips` toggle beside `DC` enables or destroys the editor's hover-tooltip window. It defaults on and is saved as a local UI preference (`ofsound/KSH.ui.settings` in Application Support on Apple platforms), independently of project and host state.

Select beside Velocity/Chance is an editor-local Shift modifier. Tap to latch or unlatch; holding for at least 300 ms, or using it for a modified action with another touch, is momentary. Physical Shift also lights Select, and releasing it preserves any latch. While active, cells show their channel color without values, velocity shading, roll divisions, or cycle marks; selection outlines remain visible. The modifier covers grid selection and selected-value drags, channel solo, and pattern copy-source selection. Undo/redo use the physical Command/Shift keys independently of Select. It preserves the Velocity/Chance layer and is cleared by Escape or editor focus loss. This state is never persisted or published to audio processing.

With no cells selected, Option temporarily shows Chance. With a selection, Option preserves the current layer and arms duplication: the grid uses a copy cursor, the compact Copy button lights, and dragging duplicates the selected cells without changing their values. Releasing Option returns to the latched Copy setting. An Option-click without a drag leaves the selected cells intact.

The string command adapter is retained solely for the existing command-contract tests and compatibility with internal callers. It has no web transport, resources or browser objects.
