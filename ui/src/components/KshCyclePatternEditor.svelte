<script>
  import { absorbPointerDragFocus, releasePointerDragFocus } from "./pointerDragFocus.js";

  const maxCyclePatternCells = 8;

  /**
   * @typedef {Object} Props
   * @property {number} [cycle]
   * @property {number} [cycleOffset]
   * @property {boolean|number} [cycleInverted]
   * @property {() => void} [onGestureStart]
   * @property {(cycle: number, cycleOffset: number) => void | Promise<void>} [onPatternPreview]
   * @property {(cycle: number, cycleOffset: number) => void | Promise<void>} [onPatternCommit]
   */

  /** @type {Props} */
  let {
    cycle = 1,
    cycleOffset = 0,
    cycleInverted = false,
    onGestureStart = () => {},
    onPatternPreview = () => {},
    onPatternCommit = () => {},
  } = $props();

  /** @type {Array<HTMLButtonElement | undefined>} */
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
  let draftOffset = $state(0);
  let draggingLength = $state(false);
  let dragPointerId = -1;

  $effect(() => {
    if (draggingLength) return;

    draftCycle = Math.min(maxCyclePatternCells, Math.max(1, Math.round(cycle)));
    draftOffset = Math.min(draftCycle - 1, Math.max(0, Math.round(cycleOffset)));
  });

  function applyDraft(nextCycle, nextOffset) {
    const normalizedCycle = Math.min(maxCyclePatternCells, Math.max(1, Math.round(nextCycle)));
    const normalizedOffset = Math.min(normalizedCycle - 1, Math.max(0, Math.round(nextOffset)));
    draftCycle = normalizedCycle;
    draftOffset = normalizedOffset;
    onPatternPreview(normalizedCycle, normalizedOffset);
  }

  function cycleCellIsActive(index) {
    if (index >= draftCycle) return false;

    return Boolean(cycleInverted) ? index !== draftOffset : index === draftOffset;
  }

  function onCellPointerDown(index) {
    if (draggingLength) return;

    onGestureStart();
    applyDraft(Math.max(draftCycle, index + 1), index);
    onPatternCommit(draftCycle, draftOffset);
  }

  function cycleLengthFromClientX(clientX) {
    for (let index = cellRefs.length - 1; index >= 0; index -= 1) {
      const cell = cellRefs[index];
      if (!cell) continue;

      if (clientX >= cell.getBoundingClientRect().left) {
        return index + 1;
      }
    }

    return 1;
  }

  function applyLengthFromClientX(clientX) {
    const nextCycle = Math.min(maxCyclePatternCells, Math.max(1, cycleLengthFromClientX(clientX)));
    applyDraft(nextCycle, Math.min(draftOffset, nextCycle - 1));
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
    onPatternCommit(draftCycle, draftOffset);
  }
</script>

<div class="flex min-w-0 flex-col" role="group" aria-label="Cycle pattern">
  <div class="relative min-w-0 rounded-md border border-border bg-surface/60 p-1 shadow-[0_12px_28px_rgba(0,0,0,0.28)]">
    <div class="grid grid-cols-8 gap-1">
      {#each Array.from({ length: maxCyclePatternCells }, (_, index) => index) as index (index)}
        {@const inPattern = index < draftCycle}
        {@const active = cycleCellIsActive(index)}
        <button
          type="button"
          {@attach registerCell(index)}
          data-cursor="pointer"
          class="relative z-[1] h-8 rounded-sm border outline-none transition-colors focus-visible:ring-1 focus-visible:ring-focus-ring {inPattern
            ? active
              ? 'border-accent bg-accent text-text-inverse'
              : 'border-border-strong bg-surface-subtle text-text-muted hover:border-text-muted'
            : 'border-border/55 bg-surface/80 text-text-faint hover:border-border-strong hover:brightness-110'}"
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
          class="pointer-events-auto z-10 flex h-8 w-3.5 translate-x-1/2 touch-none select-none items-center justify-center rounded-sm border border-border bg-surface text-text-muted shadow-sm outline-none hover:border-border-strong hover:text-text focus-visible:ring-1 focus-visible:ring-focus-ring {draggingLength ? 'border-accent text-accent shadow-[0_0_10px_color-mix(in_srgb,var(--color-accent)_35%,transparent)]' : ''}"
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
  <div class="mt-1 flex items-center justify-between px-1 text-[9px] font-semibold uppercase tracking-[0.12em] text-text-muted">
    <span>{Boolean(cycleInverted) ? "Inverted" : "Cycle"}</span>
    <span>{draftCycle} steps · offset {draftOffset + 1}</span>
  </div>
</div>
