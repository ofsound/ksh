<script>
  import { absorbPointerDragFocus, releasePointerDragFocus } from "./pointerDragFocus.js";
  import {
    cycleLengthFromCellElements,
    isCycleCellActive,
    maxCyclePatternCells,
    normalizeEditorCyclePattern,
    resizeCyclePattern,
    toggleCycleCell,
  } from "../lib/cyclePattern.js";

  /**
   * @typedef {Object} Props
   * @property {number} [cycle]
   * @property {number} [cycleMask]
   * @property {() => void} [onGestureStart]
   * @property {(cycle: number, cycleMask: number) => void | Promise<void>} [onPatternPreview]
   * @property {(cycle: number, cycleMask: number) => void | Promise<void>} [onPatternCommit]
   */

  /** @type {Props} */
  let {
    cycle = 1,
    cycleMask = 1,
    onGestureStart = () => {},
    onPatternPreview = () => {},
    onPatternCommit = () => {},
  } = $props();

  const cellRefs = Array.from({ length: maxCyclePatternCells }, () => undefined);

  function registerCell(index) {
    return (element) => {
      cellRefs[index] = element;

      return () => {
        if (cellRefs[index] === element) {
          cellRefs[index] = undefined;
        }
      };
    };
  }

  let draftCycle = $state(1);
  let draftMask = $state(1);
  let draggingLength = $state(false);
  let dragPointerId = -1;

  $effect(() => {
    if (draggingLength) return;

    const next = normalizeEditorCyclePattern(cycle, cycleMask);
    draftCycle = next.cycle;
    draftMask = next.mask;
  });

  function applyDraft(nextCycle, nextMask) {
    const next = normalizeEditorCyclePattern(nextCycle, nextMask);
    draftCycle = next.cycle;
    draftMask = next.mask;
    onPatternPreview(next.cycle, next.mask);
  }

  function applyLengthFromClientX(clientX) {
    const next = resizeCyclePattern(
      draftCycle,
      draftMask,
      cycleLengthFromCellElements(clientX, cellRefs),
    );
    draftCycle = next.cycle;
    draftMask = next.mask;
    onPatternPreview(next.cycle, next.mask);
  }

  function onCellPointerDown(index) {
    if (draggingLength) return;

    onGestureStart();
    if (index >= draftCycle) {
      applyDraft(index + 1, draftMask);
    } else {
      const next = toggleCycleCell(draftCycle, draftMask, index);
      applyDraft(next.cycle, next.mask);
    }
    onPatternCommit(draftCycle, draftMask);
  }

  function onHandlePointerDown(event) {
    absorbPointerDragFocus(event);
    event.stopPropagation();
    event.currentTarget.setPointerCapture(event.pointerId);

    onGestureStart();
    draggingLength = true;
    dragPointerId = event.pointerId;
    applyLengthFromClientX(event.clientX);
  }

  function onHandlePointerMove(event) {
    if (!draggingLength || event.pointerId !== dragPointerId) return;
    applyLengthFromClientX(event.clientX);
  }

  function onHandlePointerUp(event) {
    if (!draggingLength || event.pointerId !== dragPointerId) return;

    draggingLength = false;
    dragPointerId = -1;
    event.currentTarget.releasePointerCapture(event.pointerId);
    releasePointerDragFocus(event);
    onPatternCommit(draftCycle, draftMask);
  }

  function cycleCellStyle(inPattern, active) {
    if (!inPattern) {
      return "border-color:color-mix(in srgb, var(--cycle-row-divider, var(--color-border)) 55%, transparent);background:color-mix(in srgb, var(--cycle-row-dark, var(--color-surface)) 18%, var(--color-surface));color:var(--color-text-faint);";
    }

    if (active) {
      return [
        "border-color:color-mix(in srgb, var(--color-text) 72%, var(--color-app))",
        "background:linear-gradient(to bottom, var(--color-text), color-mix(in srgb, var(--color-text) 68%, var(--color-app)))",
        "color:var(--color-app)",
        "box-shadow:inset 0 1px 0 color-mix(in srgb, white 70%, transparent), inset 0 -2px 4px color-mix(in srgb, var(--color-app) 18%, transparent)",
      ].join(";");
    }

    return "border-color:var(--cycle-row-light, var(--color-accent));background:var(--cycle-row-light, var(--color-accent));color:var(--color-text-inverse);";
  }

  function cycleHandleStyle(isDragging) {
    return `border-color:var(--cycle-row-divider, var(--color-border));background:color-mix(in srgb, var(--cycle-row-dark, var(--color-surface)) 42%, var(--color-surface));color:${isDragging ? "var(--cycle-row-light, var(--color-accent))" : "var(--cycle-row-light, var(--color-text-muted))"};`;
  }
</script>

<style>
  .cycle-popover-focus:focus-visible {
    box-shadow: 0 0 0 1px var(--cycle-row-light, var(--color-focus-ring));
  }
</style>

<div class="flex min-w-0 flex-col" role="group" aria-label="Cycle pattern">
  <div
    class="relative min-w-0 p-1"
    style="background:color-mix(in srgb, var(--cycle-row-dark, var(--color-surface)) 28%, var(--color-app));"
  >
    <div class="grid grid-cols-8 gap-1">
      {#each Array.from({ length: maxCyclePatternCells }, (_, index) => index) as index (index)}
        {@const inPattern = index < draftCycle}
        {@const active = inPattern && isCycleCellActive(draftCycle, draftMask, index)}
        <button
          type="button"
          {@attach registerCell(index)}
          data-cursor="pointer"
          class="cycle-popover-focus relative z-[1] h-8 border outline-none transition-colors hover:brightness-110"
          style={cycleCellStyle(inPattern, active)}
          aria-label={inPattern
            ? `Cycle step ${index + 1}, ${active ? "on" : "off"}`
            : `Cycle step ${index + 1}, extend pattern`}
          aria-pressed={inPattern ? active : undefined}
          onclick={() => onCellPointerDown(index)}
        ></button>
      {/each}
    </div>

    <div class="pointer-events-none absolute inset-1 grid grid-cols-8 gap-1" aria-hidden="true">
      <div class="pointer-events-none flex items-stretch justify-end" style:grid-column={draftCycle}>
        <div
          data-cursor="horizontal-drag"
          class="cycle-popover-focus pointer-events-auto z-10 flex h-8 w-3.5 translate-x-1/2 touch-none select-none items-center justify-center rounded-sm border shadow-sm outline-none hover:brightness-110 {draggingLength ? 'shadow-[0_0_10px_color-mix(in_srgb,var(--cycle-row-light,var(--color-accent))_35%,transparent)]' : ''}"
          style={cycleHandleStyle(draggingLength)}
          role="slider"
          tabindex="0"
          aria-label="Cycle length"
          aria-valuemin="1"
          aria-valuemax={maxCyclePatternCells}
          aria-valuenow={draftCycle}
          aria-valuetext={`${draftCycle} steps`}
          onpointerdown={onHandlePointerDown}
          onpointermove={onHandlePointerMove}
          onpointerup={onHandlePointerUp}
          onpointercancel={onHandlePointerUp}
          title="Drag horizontally to set cycle length"
        >
          <span class="flex gap-0.5" aria-hidden="true">
            <span class="block h-2.5 w-px bg-current"></span>
            <span class="block h-2.5 w-px bg-current"></span>
          </span>
        </div>
      </div>
    </div>
  </div>
  <div class="mt-1 flex items-center justify-between px-1 text-[11px] font-semibold leading-none" style="color:var(--cycle-row-light, var(--color-text-muted));">
    <span>Cycle</span>
    <span>{draftCycle} {draftCycle === 1 ? "step" : "steps"}</span>
  </div>
</div>
