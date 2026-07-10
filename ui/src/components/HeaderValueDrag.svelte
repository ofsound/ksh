<script>
  let {
    id,
    label,
    value,
    suffix = "",
    horizontal = false,
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

<div class={horizontal ? "flex items-center gap-2.5" : "flex flex-col items-start"}>
  <span class={horizontal ? "header-label-horizontal" : "header-label"}>{label}</span>
  <button
    type="button"
    class={`header-button border bg-transparent ${horizontal ? "h-[48px] min-w-[72px] px-4 text-[20px]" : "min-w-[42px]"} ${active ? "border-accent text-accent" : "border-border-subtle text-text"}`}
    onpointerdown={onPointerDown}
    onpointermove={onPointerMove}
    onpointerup={onPointerUp}
    onpointercancel={onPointerUp}
  >
    {value}{suffix}
  </button>
</div>
