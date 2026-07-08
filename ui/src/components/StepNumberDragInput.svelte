<script>
  import { absorbPointerDragFocus, releasePointerDragFocus } from "./pointerDragFocus.js";
  import { emeraldRowAccent } from "./rowAccentTheme.js";

  /**
   * @typedef {Object} Props
   * @property {import('./rowAccentTheme.js').RowAccent} [accent]
   * @property {boolean} [muted]
   * @property {any} value
   * @property {number} [min]
   * @property {number} [max]
   * @property {any} [formatValue]
   * @property {number} [displayAdd] - Added to the stored value for display and aria only (e.g. 1 for 1-based cycle offset).
   * @property {any} [resetValue] - Value restored on double-click; omit to disable reset.
   * @property {string} [ariaLabel]
   * @property {boolean} [disabled]
   * @property {boolean} [boxed] - When true, use header control box styling (matches DiscreteDragSelect).
   * @property {boolean} [compact] - Tight boxed width for compact header controls with short values.
   * @property {number} [valueStep] - Drag/keyboard increment (default 1).
   * @property {number} [boxChars] - Minimum character width for boxed layout (largest formatted value).
   * @property {boolean} [deferCommit] - Keep a local drag value during the gesture; preview on every move; commit on release.
   * @property {() => void} [onGestureStart] - Called at drag start when {@link deferCommit} is true.
   * @property {(value: number) => void | Promise<void>} [onValueCommit] - Final commit on release.
   * @property {(value: number) => void | Promise<void>} [onValueChange]
   */

  /** @type {Props} */
  let {
    accent = emeraldRowAccent,
    muted = false,
    value,
    min = 0,
    max = 127,
    formatValue = undefined,
    displayAdd = 0,
    resetValue = undefined,
    ariaLabel = "Value",
    disabled = false,
    boxed = false,
    compact = false,
    valueStep = 1,
    boxChars = undefined,
    deferCommit = false,
    onGestureStart = undefined,
    onValuePreview = undefined,
    onValueCommit = undefined,
    onValueChange = () => {},
  } = $props();

  const boxedControlBaseClasses =
    "mp-param-box mp-control-gradient flex h-8 items-center justify-center rounded-md border text-sm font-semibold tabular-nums transition-[border-color,box-shadow,filter] duration-75";

  const pixelsPerStep = 4;

  let dragging = $state(false);
  let dragStartY = 0;
  let dragStartValue = 0;
  let dragValue = $state(0);

  let displayedValue = $derived(dragging ? dragValue : value);
  let effectiveBoxChars = $derived(boxChars ?? (compact ? 3 : 4));
  let displayValue = $derived(
    formatValue !== undefined
      ? formatValue(displayedValue)
      : String(Math.round(displayedValue + displayAdd)),
  );
  let ariaValueMin = $derived(min + displayAdd);
  let ariaValueMax = $derived(max + displayAdd);
  let ariaValueNow = $derived(displayedValue + displayAdd);

  /** @param {number} next */
  function snapValue(next) {
    if (valueStep === 1) return Math.round(next);

    return Math.round(next / valueStep) * valueStep;
  }

  /** @param {number} next */
  function clampValue(next) {
    return Math.min(max, Math.max(min, snapValue(next)));
  }

  /** @param {number} clientY */
  function valueFromDrag(clientY) {
    const steps = Math.round((dragStartY - clientY) / pixelsPerStep);

    return clampValue(dragStartValue + steps * valueStep);
  }

  /** @param {number} next */
  function commitDeferredValue(next) {
    onGestureStart?.();
    onValueCommit?.(next);
  }

  /** @param {number} next */
  function applyValue(next) {
    if (deferCommit && onValueCommit) {
      commitDeferredValue(next);
      return;
    }

    onValueChange(next);
  }

  /** @param {PointerEvent} event */
  function onPointerDown(event) {
    if (disabled) return;

    absorbPointerDragFocus(event);
    event.currentTarget.setPointerCapture(event.pointerId);
    dragging = true;
    dragStartY = event.clientY;
    dragStartValue = value;
    dragValue = value;

    if (deferCommit) {
      onGestureStart?.();
    }
  }

  /** @param {PointerEvent} event */
  function onPointerMove(event) {
    if (!dragging || disabled) return;

    const next = valueFromDrag(event.clientY);

    if (next === dragValue) return;

    dragValue = next;

    if (deferCommit && onValueCommit) {
      onValuePreview?.(next);
      return;
    }

    if (Math.abs(next - value) >= valueStep / 2 && next !== value) onValueChange(next);
  }

  /** @param {PointerEvent} event */
  function onPointerUp(event) {
    if (!dragging) return;

    dragging = false;
    event.currentTarget.releasePointerCapture(event.pointerId);

    if (deferCommit && onValueCommit) {
      onValueCommit(dragValue);
    }

    releasePointerDragFocus(event);
  }

  /** @param {MouseEvent} event */
  function onDoubleClick(event) {
    if (disabled || resetValue === undefined || dragging) return;

    event.preventDefault();

    if (value !== resetValue) applyValue(resetValue);
  }

</script>

<div
  data-cursor={disabled ? "default" : "vertical-drag"}
  style:--param-box-chars={boxed ? effectiveBoxChars : undefined}
  class="touch-none select-none outline-none {boxed
    ? boxedControlBaseClasses
    : 'inline-flex items-center rounded-sm'} {disabled
    ? 'opacity-50'
    : ''} {accent.ringFocusWithWidth} {boxed
    ? dragging && !muted && !disabled
      ? 'border-border-strong shadow-sm'
      : muted
        ? 'border-border-subtle'
        : disabled
          ? 'border-border'
          : 'border-border hover:border-border-strong'
    : ''} {muted
    ? 'text-text-muted'
    : dragging
      ? accent.textAccentLight
      : 'text-text'}"
  role="slider"
  aria-label={ariaLabel}
  aria-valuemin={ariaValueMin}
  aria-valuemax={ariaValueMax}
  aria-valuenow={ariaValueNow}
  aria-valuetext={displayValue}
  aria-disabled={disabled}
  tabindex="-1"
  onpointerdown={onPointerDown}
  onpointermove={onPointerMove}
  onpointerup={onPointerUp}
  onpointercancel={onPointerUp}
  ondblclick={onDoubleClick}
  title={!disabled && resetValue !== undefined
    ? "Drag to change · double-click to reset"
    : undefined}
  onkeydown={(event) => {
    if (disabled) return;

    if (event.key === "ArrowUp") {
      event.preventDefault();

      if (displayedValue < max) applyValue(displayedValue + valueStep);
    } else if (event.key === "ArrowDown") {
      event.preventDefault();

      if (displayedValue > min) applyValue(displayedValue - valueStep);
    }
  }}
>
  <span
    class="font-sans leading-none tabular-nums {boxed
      ? ''
      : 'text-xs font-bold'} {dragging && !muted ? accent.textAccentLight : ''}"
    >{displayValue}</span
  >
</div>
