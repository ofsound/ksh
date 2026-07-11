<script>
  import HeaderValueDrag from "./HeaderValueDrag.svelte";
  import { MAX_CHANNELS } from "../lib/kshConstants.js";
  import { headerDragNextValue, headerValueForState } from "../lib/kshEditorInteractions.js";
  import {
    CHANNEL_LABEL_W,
    compactPreviewCellWidth,
    compactPreviewPadding,
    compactPreviewGridWidth,
    editorDimensions,
    generationModeLabel,
  } from "../lib/kshEditorUtils.js";
  import {
    beginEditGestureHistory,
    commitEditGestureHistory,
    cycleMode,
    isCompactFlashing,
    session,
    setChannelLock,
    setHeaderValue,
  } from "../lib/kshSession.svelte.js";

  let randomHeaderDrag = $state(null);
  let lockDrag = $state(null);

  const previewChannels = $derived(Math.min(MAX_CHANNELS, session.kshState.channelCount));
  const dims = $derived(editorDimensions(session.kshState, session.patternViewScale));
  const previewPad = $derived(compactPreviewPadding(previewChannels));
  const previewRows = $derived(Array.from({ length: previewChannels }, (_, channel) => channel));
  const lockRows = $derived(Array.from({ length: MAX_CHANNELS }, (_, channel) => channel));
  const stepCols = $derived(Array.from({ length: session.kshState.stepCount }, (_, step) => step));
  const compactGridWidth = compactPreviewGridWidth();
  const compactCellWidth = $derived(compactPreviewCellWidth(session.kshState.stepCount));

  function channelLockLabel(channel) {
    const lock = session.kshState.channels[channel]?.lock ?? -1;
    return lock < 0 ? "OFF" : `P${lock + 1}`;
  }

  function channelLockTitle(channel) {
    const lock = session.kshState.channels[channel]?.lock ?? -1;
    return lock < 0 ? `Lane ${channel + 1}: random pattern selection` : `Lane ${channel + 1}: locked to pattern ${lock + 1}`;
  }

  function beginLockDrag(channel, event) {
    event.currentTarget.setPointerCapture(event.pointerId);
    lockDrag = {
      channel,
      startY: event.clientY,
      startValue: session.kshState.channels[channel]?.lock ?? -1,
      nextValue: session.kshState.channels[channel]?.lock ?? -1,
    };
    beginEditGestureHistory();
  }

  function moveLockDrag(event) {
    if (!lockDrag || (event.buttons & 1) === 0) {
      return;
    }

    const delta = Math.round((lockDrag.startY - event.clientY) / 14);
    const nextValue = ((lockDrag.startValue + 1 + delta) % 17 + 17) % 17 - 1;
    if (nextValue !== lockDrag.nextValue) {
      lockDrag = { ...lockDrag, nextValue };
      session.kshState.channels[lockDrag.channel].lock = nextValue;
    }
  }

  async function endLockDrag() {
    if (!lockDrag) {
      return;
    }

    const { channel, startValue, nextValue } = lockDrag;
    lockDrag = null;
    if (startValue !== nextValue) {
      await setChannelLock(channel, nextValue);
    }
    await commitEditGestureHistory("Change channel lock");
  }

  function cellFill(channel, step) {
    if (isCompactFlashing(channel, step)) {
      return "bg-text";
    }

    const activeStep = step < session.kshState.stepCount;
    if (!activeStep) {
      return "bg-grid-inactive-step";
    }

    const cell = session.previewData?.generated?.[channel]?.[step];
    if (cell?.enabled) {
      return "bg-info";
    }

    return step % 4 === 0 ? "bg-grid-off-strong" : "bg-grid-off";
  }

  function beginRandomHeaderDrag(id, clientY) {
    beginEditGestureHistory();
    randomHeaderDrag = {
      id,
      startY: clientY,
      startValue: headerValueForState(session.kshState, id),
    };
  }

  function moveRandomHeaderDrag(clientY) {
    if (!randomHeaderDrag) {
      return;
    }

    const next = headerDragNextValue(randomHeaderDrag, clientY);
    setHeaderValue(randomHeaderDrag.id, next);
  }

  async function endRandomHeaderDrag() {
    if (randomHeaderDrag) {
      await commitEditGestureHistory("Change refresh length");
    }
    randomHeaderDrag = null;
  }
</script>

<div
  class="compact-strip relative h-full overflow-hidden text-text"
  style={`width:${dims.width}px;`}
>
  {#if session.bridgeError}
    <p class="absolute left-3 top-2 z-10 text-xs text-danger">{session.bridgeError}</p>
  {:else if !session.ready}
    <p class="absolute left-3 top-2 z-10 text-xs text-text-muted">Loading…</p>
  {/if}

  <section class="px-3" style={`padding-top:${previewPad}px;padding-bottom:${previewPad}px;`}>
    <div class="flex items-start gap-5">
      <div class="flex flex-col gap-0">
        {#each previewRows as channel (channel)}
          <div class="flex h-[18px] items-center gap-0">
            <span
              class="channel-label shrink-0 truncate text-left text-[9px] text-text-muted"
              style={`width:${CHANNEL_LABEL_W}px`}
            >
              {session.kshState.channels[channel]?.label ?? channel + 1}
            </span>
            <div class="flex" style={`width:${compactGridWidth}px`}>
              {#each stepCols as step (step)}
                <div
                  class={`mr-[3px] h-[15px] shrink-0 rounded-sm ${cellFill(channel, step)}`}
                  style={`width:${compactCellWidth}px`}
                  aria-hidden="true"
                ></div>
              {/each}
            </div>
          </div>
        {/each}
      </div>

      <div class="compact-random-panel flex shrink-0 flex-col gap-3 self-stretch border-l border-border-subtle pl-4">
        <div class="flex items-end gap-6">
          <div class="flex flex-col items-start">
            <span class="header-label text-text">Random Mode</span>
            <button type="button" class="header-button min-w-[164px] bg-control-secondary text-text" onclick={cycleMode}>
              {generationModeLabel(session.kshState.generationMode)}
            </button>
          </div>
          <HeaderValueDrag
            id="refresh"
            label="Length"
            value={session.kshState.refreshSteps}
            suffix=" steps"
            brightLabel
            active={randomHeaderDrag?.id === "refresh"}
            onBegin={beginRandomHeaderDrag}
            onMove={moveRandomHeaderDrag}
            onEnd={endRandomHeaderDrag}
          />
        </div>
        <div class="flex flex-col items-start gap-1.5">
          <span class="header-label text-text">Lane Lock</span>
          <div class="flex items-center gap-2" aria-label="Lane pattern locks">
          {#each lockRows as channel (channel)}
            <div class="flex flex-col items-center gap-1">
              <span class="compact-lock-label">{channel + 1}</span>
              <button
                type="button"
                class={`compact-lock-button ${session.kshState.channels[channel]?.lock >= 0 ? "compact-lock-button-locked" : "compact-lock-button-random"}`}
                aria-label={channelLockTitle(channel)}
                title={`${channelLockTitle(channel)}. Drag vertically to change.`}
                onpointerdown={(event) => beginLockDrag(channel, event)}
                onpointermove={moveLockDrag}
                onpointerup={endLockDrag}
                onpointercancel={endLockDrag}
              >
                {channelLockLabel(channel)}
              </button>
            </div>
          {/each}
          </div>
        </div>
      </div>
    </div>
  </section>
</div>
