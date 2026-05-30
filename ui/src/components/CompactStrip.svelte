<script>
  import { MAX_CHANNELS, MAX_STEPS } from "../lib/kshConstants.js";
  import {
    CHANNEL_LABEL_W,
    compactPreviewPadding,
    editorDimensions,
  } from "../lib/kshEditorUtils.js";
  import {
    isCompactFlashing,
    session,
  } from "../lib/kshSession.svelte.js";

  const previewChannels = $derived(Math.min(MAX_CHANNELS, session.kshState.channelCount));
  const dims = $derived(editorDimensions(session.kshState));
  const previewPad = $derived(compactPreviewPadding(previewChannels));

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

    return step % 4 === 0 ? "bg-ksh-off-dark" : "bg-ksh-off";
  }
</script>

<div
  class="compact-strip relative h-full overflow-hidden text-ksh-text"
  style={`width:${dims.width}px;`}
>
  {#if session.bridgeError}
    <p class="absolute left-3 top-2 z-10 text-xs text-red-400">{session.bridgeError}</p>
  {:else if !session.ready}
    <p class="absolute left-3 top-2 z-10 text-xs text-ksh-muted">Loading…</p>
  {/if}

  <section class="px-3" style={`padding-top:${previewPad}px;`}>
    <div class="flex flex-col gap-0">
      {#each Array.from({ length: previewChannels }, (_, channel) => channel) as channel (channel)}
        <div class="flex h-[18px] items-center gap-2">
          <span
            class="shrink-0 truncate text-left text-[9px] text-ksh-muted"
            style={`width:${CHANNEL_LABEL_W}px`}
          >
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
