<script>
  import {
    COMPACT_HEIGHT,
    MAX_CHANNELS,
    MAX_STEPS,
  } from "../lib/kshConstants.js";
  import { editorDimensions } from "../lib/kshEditorUtils.js";
  import {
    isCompactFlashing,
    session,
  } from "../lib/kshSession.svelte.js";

  const previewChannels = $derived(Math.min(MAX_CHANNELS, session.kshState.channelCount));
  const dims = $derived(editorDimensions(session.kshState));

  function cellFill(channel, step) {
    if (isCompactFlashing(channel, step)) {
      return "bg-ksh-text";
    }

    const activeStep = step < session.kshState.stepCount;
    if (!activeStep) {
      return "bg-ksh-inactive-step";
    }

    const cell = session.previewData?.generated?.[channel]?.[step];
    if (cell?.enabled) {
      return "bg-ksh-blue";
    }

    return "bg-ksh-off";
  }
</script>

<div
  class="compact-strip flex overflow-hidden rounded-md border border-ksh-stroke-soft bg-ksh-bg text-ksh-text"
  style={`width: ${dims.width}px; height: ${COMPACT_HEIGHT}px;`}
>
  <section class="min-w-0 flex-1 overflow-hidden py-[18px] pl-[50px] pr-3">
    {#if session.bridgeError}
      <p class="mb-2 text-xs text-red-400">{session.bridgeError}</p>
    {:else if !session.ready}
      <p class="text-xs text-ksh-muted">Loading…</p>
    {/if}

    <div class="flex flex-col gap-0">
      {#each Array.from({ length: previewChannels }, (_, channel) => channel) as channel (channel)}
        <div class="flex h-[18px] items-center gap-2">
          <span class="w-8 shrink-0 text-right text-[9px] text-ksh-muted">
            {session.kshState.channels[channel]?.label ?? channel + 1}
          </span>
          <div class="flex">
            {#each Array.from({ length: MAX_STEPS }, (_, step) => step) as step (step)}
              <div
                class={`mr-[3px] h-[15px] w-[15px] rounded-sm ${cellFill(channel, step)}`}
                aria-hidden="true"
              ></div>
            {/each}
          </div>
        </div>
      {/each}
    </div>
  </section>
</div>
