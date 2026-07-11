<script>
  import { absorbPointerDragFocus, releasePointerDragFocus } from "./pointerDragFocus.js";

  let {
    id,
    label,
    value,
    options,
    resetValue = undefined,
    ariaLabel = label,
    active = false,
    brightLabel = false,
    onBegin = () => {},
    onEnd = () => {},
    onValueChange = () => {},
  } = $props();

  const pixelsPerStep = 10;
  let dragging = $state(false);
  let dragStartY = 0;
  let dragStartValue = 0;

  let maxIndex = $derived(Math.max(0, options.length - 1));
  let valuePosition = $derived(Math.max(0, options.findIndex((option) => option.index === value)));
  let currentLabel = $derived(options.find((option) => option.index === value)?.label ?? "");

  function indexAtPosition(position) {
    const clamped = Math.min(maxIndex, Math.max(0, position));
    return options[clamped]?.index ?? value;
  }

  function indexFromDrag(clientY) {
    const startPosition = options.findIndex((option) => option.index === dragStartValue);
    const steps = Math.round((dragStartY - clientY) / pixelsPerStep);
    return indexAtPosition(startPosition + steps);
  }

  function onPointerDown(event) {
    absorbPointerDragFocus(event);
    event.currentTarget.setPointerCapture(event.pointerId);
    dragging = true;
    dragStartY = event.clientY;
    dragStartValue = value;
    onBegin(id, event.clientY);
  }

  function onPointerMove(event) {
    if (!dragging) return;

    const next = indexFromDrag(event.clientY);
    if (next !== value) onValueChange(next);
  }

  function onPointerUp(event) {
    dragging = false;
    event.currentTarget.releasePointerCapture(event.pointerId);
    releasePointerDragFocus(event);
    onEnd();
  }

  function onDoubleClick(event) {
    if (resetValue === undefined || dragging) return;

    event.preventDefault();
    if (value !== resetValue) {
      onBegin(id, event.clientY);
      onValueChange(resetValue);
      onEnd();
    }
  }

  function onKeyDown(event) {
    if (event.key === "ArrowUp") {
      event.preventDefault();
      if (valuePosition < maxIndex) onValueChange(options[valuePosition + 1].index);
    } else if (event.key === "ArrowDown") {
      event.preventDefault();
      if (valuePosition > 0) onValueChange(options[valuePosition - 1].index);
    }
  }
</script>

<div class="flex flex-col items-start">
  <span class={`header-label ${brightLabel ? "text-text" : ""}`}>{label}</span>
  <button
    type="button"
    class={`header-button min-w-[42px] border tabular-nums ${active || dragging ? "border-accent text-accent" : "border-border-subtle text-text"}`}
    aria-label={ariaLabel}
    aria-valuemin={options[0]?.index}
    aria-valuemax={options[options.length - 1]?.index}
    aria-valuenow={value}
    aria-valuetext={currentLabel}
    role="slider"
    tabindex="0"
    data-cursor="vertical-drag"
    title={resetValue !== undefined ? "Drag to change · double-click to reset" : "Drag vertically to change"}
    onpointerdown={onPointerDown}
    onpointermove={onPointerMove}
    onpointerup={onPointerUp}
    onpointercancel={onPointerUp}
    ondblclick={onDoubleClick}
    onkeydown={onKeyDown}
  >
    {currentLabel}
  </button>
</div>
