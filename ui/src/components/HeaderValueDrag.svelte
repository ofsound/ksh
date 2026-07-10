<script>
  let {
    id,
    label,
    value,
    suffix = "",
    horizontal = false,
    compactHorizontal = false,
    showLabel = true,
    brightLabel = false,
    active = false,
    onBegin = () => {},
    onMove = () => {},
    onEnd = () => {},
  } = $props();

  function onPointerDown(event) {
    event.currentTarget.setPointerCapture(event.pointerId);
    onBegin(id, event.clientY);
  }

  function onPointerMove(event) {
    if ((event.buttons & 1) === 0) {
      return;
    }
    onMove(event.clientY);
  }

  function onPointerUp() {
    onEnd();
  }
</script>

<div class={horizontal ? "flex items-center gap-2" : "flex flex-col items-start"}>
  {#if showLabel}
    <span class={`${horizontal ? "header-label-horizontal" : "header-label"} ${brightLabel ? "text-text" : ""}`}>{label}</span>
  {/if}
  <button
    type="button"
    class={`header-button border bg-transparent ${horizontal ? (compactHorizontal ? "h-[42px] w-[122px] shrink-0 px-3.5 text-[16px] font-semibold leading-none" : "h-[54px] min-w-[160px] px-5 text-[24px]") : "min-w-[42px]"} ${active ? "border-accent text-accent" : "border-border-subtle text-text"}`}
    aria-label={label}
    onpointerdown={onPointerDown}
    onpointermove={onPointerMove}
    onpointerup={onPointerUp}
    onpointercancel={onPointerUp}
  >
    {value}{suffix}
  </button>
</div>
