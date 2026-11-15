# Shared desktop and iPad layout

Desktop and iPad use the same 1400 x 1050 design canvas, including the numbered audition pads. The iPhone compact editor remains separate. Project and engine state formats are unchanged.

The shared controls use 64-unit targets: fitting the canvas into a 1024 x 744-point landscape viewport leaves a 44-point minimum target. The standard 16-step grid retains square painted cells and at least 44-point cell hit regions at this size. Longer, 17–32-step patterns retain the existing all-steps overview and consequently have smaller cells; arbitrary smaller AUv3 host windows and manually reduced desktop zoom also reduce targets. The minimum sizing claim does not apply to those views.

Project actions/navigation occupy two lines at the left of the header, Swing/Humanize sit in the middle, and the sixteen pattern buttons and M remain at the right. Standalone transport sits beside undo/redo in the editing toolbar; plug-ins retain host transport. Header values, record/quantization, grid controls, footer actions and inspector controls are enlarged.

Each channel keeps its numbered audition pad, editable name, MIDI note and mute control. Clicking/tapping a row name focuses the channel tools without auditioning; double-clicking renames it. Playing a numbered pad or editing a cell also focuses that row. Direction, ping-pong, shifting and clearing use a single shared strip, with explicit Solo and Reset actions. Focus is editor-local and follows logical channels when rows reverse.

Loop start and length remain controlled by the grid handles. The visible grips are 28 design units wide, with larger grip dots. Touch input uses a 64 x 64 grab area located mainly outside the active loop; mouse input uses the visible grip width to preserve the separate velocity-edge gesture. Row controls and outer gutters reserve space for the full-loop grab areas. Cell-center taps remain available even with a one-step loop. No additional loop numeric fields or steppers are introduced.

The inspector retains direct value dragging and exposes the existing cycle-length value control alongside eight larger cycle buttons. The logo uses less space. Generation controls and project dialogs also have larger targets.

Validation: `[unified-layout]` tests scaled target sizes, toolbar separation, containment, contextual row editing/undo, and loop handle geometry/dragging. `KSH_NATIVE_CAPTURE_DIR` renders shared standalone/plugin snapshots for inspection. Validate real multitouch on a device and test smaller AUv3 host windows before release.
