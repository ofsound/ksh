<script>
  import {
    COMPACT_HEIGHT,
    COMPACT_WIDTH,
    MAX_LANES,
    MAX_STEPS,
  } from "../lib/kshConstants.js";
  import {
    isCompactFlashing,
    openEditor,
    session,
  } from "../lib/kshSession.svelte.js";

  const previewLanes = $derived(Math.min(MAX_LANES, session.kshState.laneCount));

  function cellFill(lane, step) {
    if (isCompactFlashing(lane, step)) {
      return "bg-ksh-text";
    }

    const activeStep = step < session.kshState.stepCount;
    if (!activeStep) {
      return "bg-ksh-inactive-step";
    }

    const cell = session.previewData?.generated?.[lane]?.[step];
    if (cell?.enabled) {
      return "bg-ksh-blue";
    }

    return "bg-ksh-off";
  }
</script>

<div
  class="compact-strip mx-auto flex overflow-hidden rounded-md border border-ksh-stroke-soft bg-ksh-bg text-ksh-text"
  style={`width: ${COMPACT_WIDTH}px; height: ${COMPACT_HEIGHT}px;`}
>
  <aside class="flex w-[86px] shrink-0 flex-col bg-ksh-panel2 px-3.5 py-[18px]">
    <button
      type="button"
      class="rounded border border-ksh-amber bg-ksh-amber px-2 py-1.5 text-xs font-medium text-ksh-off"
      onclick={openEditor}
    >
      Edit
    </button>
  </aside>

  <section class="min-w-0 flex-1 overflow-hidden py-[18px] pl-[50px] pr-3">
    {#if session.bridgeError}
      <p class="mb-2 text-xs text-red-400">{session.bridgeError}</p>
    {:else if !session.ready}
      <p class="text-xs text-ksh-muted">Loading…</p>
    {/if}

    <div class="flex flex-col gap-0">
      {#each Array.from({ length: previewLanes }, (_, lane) => lane) as lane (lane)}
        <div class="flex h-[18px] items-center gap-2">
          <span class="w-8 shrink-0 text-right text-[9px] text-ksh-muted">
            {session.kshState.lanes[lane]?.label ?? lane + 1}
          </span>
          <div class="flex">
            {#each Array.from({ length: MAX_STEPS }, (_, step) => step) as step (step)}
              <div
                class={`mr-[3px] h-[15px] w-[15px] rounded-sm ${cellFill(lane, step)}`}
                aria-hidden="true"
              ></div>
            {/each}
          </div>
        </div>
      {/each}
    </div>
  </section>
</div>
