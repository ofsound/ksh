<script>
  let {
    id,
    label,
    value,
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

<div class="flex flex-col items-center">
  <span class="text-[9px] text-ksh-muted">{label}</span>
  <button
    type="button"
    class={`mt-0.5 min-w-[42px] rounded border px-1.5 py-0.5 text-[11px] ${active ? "border-ksh-amber text-ksh-amber" : "border-ksh-stroke-soft text-ksh-text"}`}
    onpointerdown={onPointerDown}
    onpointermove={onPointerMove}
    onpointerup={onPointerUp}
    onpointercancel={onPointerUp}
  >
    {value}{label === "Swing" || label === "Vel" || label === "Time" ? "%" : ""}
  </button>
</div>
