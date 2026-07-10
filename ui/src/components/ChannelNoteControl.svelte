<script>
  import { VELOCITY_DRAG_THRESHOLD } from "../lib/kshConstants.js";
  import { midiNoteLabel } from "../lib/kshEditorUtils.js";
  import { channelNoteDragNextValue } from "../lib/kshEditorInteractions.js";
  import {
    beginEditGestureHistory,
    commitEditGestureHistory,
    session,
    setChannelNote,
    toggleChannelNoteDisplayNames,
  } from "../lib/kshSession.svelte.js";

  /** @type {{ channel: number }} */
  let { channel } = $props();

  let dragging = $state(false);
  /** @type {{ channel: number, startY: number, startValue: number, moved: boolean } | null} */
  let drag = $state(null);

  const note = $derived(session.kshState.channels[channel]?.note ?? 36);
  const label = $derived(session.channelNoteDisplayNames ? midiNoteLabel(note) : String(note));

  function onPointerDown(event) {
    event.currentTarget.setPointerCapture(event.pointerId);
    drag = {
      channel,
      startY: event.clientY,
      startValue: note,
      moved: false,
    };
  }

  function onPointerMove(event) {
    if (!drag || (event.buttons & 1) === 0) {
      return;
    }

    if (!drag.moved && Math.abs(event.clientY - drag.startY) > VELOCITY_DRAG_THRESHOLD) {
      beginEditGestureHistory();
      drag = { ...drag, moved: true };
      dragging = true;
    }

    if (!drag.moved) {
      return;
    }

    const next = channelNoteDragNextValue(drag, event.clientY);
    void setChannelNote(channel, next);
  }

  async function onPointerUp() {
    if (!drag) {
      return;
    }

    if (!drag.moved) {
      toggleChannelNoteDisplayNames();
    } else {
      await commitEditGestureHistory("Change channel note");
    }

    dragging = false;
    drag = null;
  }
</script>

<button
  type="button"
  class={`w-9 shrink-0 text-left text-[13px] outline-none focus-visible:ring-1 focus-visible:ring-focus-ring ${dragging ? "text-accent" : "text-info"}`}
  aria-label={session.channelNoteDisplayNames ? `Channel note ${midiNoteLabel(note)}` : `Channel note ${note}`}
  title={session.channelNoteDisplayNames ? "Click to show MIDI numbers. Drag vertically to change." : "Click to show note names. Drag vertically to change."}
  onpointerdown={onPointerDown}
  onpointermove={onPointerMove}
  onpointerup={onPointerUp}
  onpointercancel={onPointerUp}
>
  {label}
</button>
