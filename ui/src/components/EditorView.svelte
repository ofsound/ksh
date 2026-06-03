<script>
  import { onMount } from "svelte";
  import HeaderValueDrag from "./HeaderValueDrag.svelte";
  import {
    CHANNEL_LABEL_W,
    GRID_CELL_H,
    GRID_CELL_W,
    GRID_SIDEBAR_W,
    cycleOffsetLabel,
    editorDimensions,
    generationModeLabel,
    gridCellPadding,
    gridTopPadding,
    isStepBeyondLoopLength,
    channelToneColor,
    lockLabel,
    loopLengthForChannel,
    modifierLayerMode,
    mutedChannelColor,
    normalizeSourceLayerMode,
    playbackModeLabel,
    resolveCellTriangle,
    sourceLayerLabel,
    sourceLayerValue,
    valueModeForCellInteraction,
  } from "../lib/kshEditorUtils.js";
  import {
    applySourcePaintRange,
    applySourceValueDrag,
    createCellDrag,
    headerDragNextValue,
    headerValueForState,
    loopDragNextValue,
    resolveCellDragMode,
    stepFromGridX,
    toggleCellOnRelease,
  } from "../lib/kshEditorInteractions.js";
  import { CHANNEL_RENAME_MS, MAX_STEPS, SOURCE_COUNT, SOURCE_ROW_RESET_MS } from "../lib/kshConstants.js";
  import {
    adjustChannelNote,
    auditionChannel,
    clearPattern,
    cycleChannelLock,
    cycleChannelPlaybackMode,
    cycleMode,
    cycleRateCommand,
    cycleSourceLayerMode,
    incrementChannelNote,
    isEditorFlashing,
    resetSourceChannelRow,
    selectSource,
    sendCell,
    sendCellsForChannel,
    setSourceChannelMute,
    session,
    setHeaderValue,
    setChannelLabel,
    setRowLoopLength,
    setSelectedCell,
    setSourceLayerMode,
    setStandaloneTransportPlaying,
    shiftChannelRow,
    shiftPattern,
    adjustStandaloneTempo,
    toggleDcColors,
    toggleDeviceActive,
  } from "../lib/kshSession.svelte.js";

  let gridEl = $state(null);
  let headerDrag = $state(null);
  let cellDrag = $state(null);
  let loopDrag = $state(null);
  /** @type {{ source: number, lastChannel: number } | null} */
  let muteDrag = $state(null);
  let hoverLayerMode = $state(null);
  let editingChannel = $state(-1);
  let labelDraft = $state("");
  let channelRenameTap = $state({ channel: -1, at: 0 });
  let sourceRowResetTap = $state({ channel: -1, at: 0 });
  let lastAudition = $state({ channel: -1, at: 0 });

  const dims = $derived(editorDimensions(session.kshState));
  const gridTopPad = $derived(gridTopPadding(session.kshState.channelCount, dims.height));
  const gridBottomPad = $derived(gridCellPadding(session.kshState.channelCount, dims.height));
  const channelRows = $derived(Array.from({ length: session.kshState.channelCount }, (_, channel) => channel));
  const stepCols = $derived(
    Array.from({ length: session.kshState.stepCount }, (_, step) => step)
  );
  const effectiveLayerMode = $derived(
    normalizeSourceLayerMode(hoverLayerMode ?? session.sourceLayerMode)
  );

  function gridLeft() {
    return gridEl?.getBoundingClientRect().left ?? 0;
  }

  function cellStyle(channel, step) {
    const source = session.selectedSource;
    const cell = session.kshState.sources[source][channel][step];
    const muted = session.kshState.sourceChannelMutes[source][channel];
    const beyondSteps = step >= session.kshState.stepCount;
    const beyondLoop = isStepBeyondLoopLength(session.kshState, channel, step);
    const flashing = isEditorFlashing(source, channel, step);

    if (flashing) {
      return "background:#dbdee5;color:#1a1c21;";
    }

    if (beyondSteps) {
      return "background:#242930;color:#5c636b;";
    }

    if (beyondLoop) {
      if (cell.enabled) {
        return "background:#242930;color:#5c636b;";
      }
      return "background:#242930;color:#5c636b;opacity:0.55;";
    }

    let lightColor = channelToneColor(channel, "light", session.dcColors);
    let darkColor = channelToneColor(channel, "dark", session.dcColors);
    let dividerColor = channelToneColor(channel, "divider", session.dcColors);
    if (muted) {
      lightColor = mutedChannelColor(lightColor);
      darkColor = mutedChannelColor(darkColor);
      dividerColor = mutedChannelColor(dividerColor);
    }
    [lightColor, darkColor] = [darkColor, lightColor];

    if (!cell.enabled) {
      const downbeat = step % 4 === 0;
      return downbeat
        ? "background:#121212;color:#8c969e;"
        : "background:#1e2025;color:#8c969e;";
    }

    const layerValue = sourceLayerValue(cell, effectiveLayerMode);
    if (effectiveLayerMode === "cycle") {
      const topLeft = cell.cycleInverted ? lightColor : darkColor;
      const bottomRight = cell.cycleInverted ? darkColor : lightColor;
      return `background:linear-gradient(to bottom right, ${topLeft} 0%, ${topLeft} calc(50% - 0.5px), ${dividerColor} calc(50% - 0.5px), ${dividerColor} calc(50% + 0.5px), ${bottomRight} calc(50% + 0.5px), ${bottomRight} 100%);color:#090b0f;`;
    }

    if (effectiveLayerMode === "roll") {
      const partCount = Math.max(1, Math.round(layerValue));
      const stops = Array.from({ length: partCount }, (_, part) => {
        const start = (part * 100) / partCount;
        const end = ((part + 1) * 100) / partCount;
        const tone = part % 2 === 0 ? darkColor : lightColor;
        return `${tone} ${start}%, ${tone} ${end}%`;
      }).join(", ");
      return `background:linear-gradient(to right, ${stops});color:#090b0f;`;
    }

    const fill = effectiveLayerMode === "velocity" ? layerValue / 127 : layerValue / 100;
    const fillPercent = Math.round(fill * 100);

    return `background:linear-gradient(to top, ${darkColor} 0%, ${darkColor} ${fillPercent}%, ${lightColor} ${fillPercent}%, ${lightColor} 100%);color:#090b0f;`;
  }

  function cellClass(channel, step) {
    const beyondSteps = step >= session.kshState.stepCount;
    const beyondLoop = isStepBeyondLoopLength(session.kshState, channel, step);
    const cell = session.kshState.sources[session.selectedSource][channel][step];
    const cycleLayer = effectiveLayerMode === "cycle" && cell.enabled;

    return [
      "relative mr-0 flex overflow-hidden rounded-sm border font-medium leading-none outline-none focus:outline-none focus-visible:outline-none",
      beyondLoop ? "border-ksh-cell-border/40" : "border-ksh-cell-border",
      cycleLayer && !beyondSteps && !beyondLoop ? "cell-cycle text-[14px]" : "items-center justify-center text-[18px]",
    ].join(" ");
  }

  function cellLabel(channel, step) {
    const cell = session.kshState.sources[session.selectedSource][channel][step];
    if (!cell.enabled) {
      return "";
    }
    if (effectiveLayerMode === "cycle") {
      return "";
    }
    return String(sourceLayerValue(cell, effectiveLayerMode));
  }

  function cyclePrimaryLabel(channel, step) {
    const cell = session.kshState.sources[session.selectedSource][channel][step];
    if (!cell.enabled || effectiveLayerMode !== "cycle") {
      return "";
    }
    const value = sourceLayerValue(cell, "cycle");
    return `${cell.cycleInverted ? "!" : ""}${value}`;
  }

  function loopLengthClass(channel) {
    const shortened = loopLengthForChannel(session.kshState, channel) < session.kshState.stepCount;
    if (loopDrag?.channel === channel) {
      return "text-ksh-amber";
    }
    if (shortened) {
      return "text-ksh-amber";
    }
    return "text-ksh-blue";
  }

  function isCellInteractive(channel, step) {
    return step < session.kshState.stepCount && !isStepBeyondLoopLength(session.kshState, channel, step);
  }

  function stepLabelClass(step) {
    return session.playingStep > 0 && step + 1 === session.playingStep
      ? "text-ksh-text"
      : "text-ksh-muted";
  }

  function beginHeaderDrag(id, clientY) {
    headerDrag = {
      id,
      startY: clientY,
      startValue: headerValueForState(session.kshState, id),
    };
  }

  function moveHeaderDrag(clientY) {
    if (!headerDrag) {
      return;
    }
    const next = headerDragNextValue(headerDrag, clientY);
    setHeaderValue(headerDrag.id, next);
  }

  function endHeaderDrag() {
    headerDrag = null;
  }

  function hideCellAdjustmentCursor() {
    document.documentElement.classList.add("ksh-hide-cursor");
  }

  function showCellAdjustmentCursor() {
    document.documentElement.classList.remove("ksh-hide-cursor");
  }

  function onCellPointerDown(event, channel, step) {
    if (!isCellInteractive(channel, step)) {
      return;
    }

    const source = session.selectedSource;
    const rect = event.currentTarget.getBoundingClientRect();
    const localX = event.clientX - rect.left;
    const localY = event.clientY - rect.top;
    const layerMode =
      modifierLayerMode(event.metaKey, event.shiftKey, event.altKey) ?? session.sourceLayerMode;
    const triangle =
      normalizeSourceLayerMode(layerMode) === "cycle"
        ? resolveCellTriangle(localX, localY, GRID_CELL_W, GRID_CELL_H)
        : null;
    const valueMode = valueModeForCellInteraction(layerMode, triangle);

    const cell = session.kshState.sources[source][channel][step];

    cellDrag = createCellDrag(
      source,
      channel,
      step,
      cell,
      layerMode,
      valueMode,
      event.clientX,
      event.clientY
    );
    setSelectedCell(channel, step);
    event.currentTarget.setPointerCapture(event.pointerId);
  }

  async function onCellPointerMove(event) {
    if (!cellDrag) {
      return;
    }
    if ((event.buttons & 1) === 0) {
      await onCellPointerUp();
      return;
    }

    if (!cellDrag.mode) {
      const mode = resolveCellDragMode(cellDrag, event.clientX, event.clientY);
      if (mode) {
        cellDrag = { ...cellDrag, mode, moved: true };
        if (mode === "value") {
          hideCellAdjustmentCursor();
        }
      }
    }

    if (cellDrag.mode === "paint") {
      const toStep = stepFromGridX(event.clientX, gridLeft(), session.kshState.stepCount);
      const changed = applySourcePaintRange(
        session.kshState,
        session.selectedSource,
        cellDrag,
        cellDrag.step,
        toStep
      );
      setSelectedCell(cellDrag.channel, toStep);
      if (changed.length > 0) {
        await sendCellsForChannel(session.selectedSource, cellDrag.channel, changed);
      }
      return;
    }

    if (cellDrag.mode === "value") {
      const changed = applySourceValueDrag(
        session.kshState,
        session.selectedSource,
        cellDrag,
        event.clientY
      );
      if (changed) {
        await sendCell(session.selectedSource, cellDrag.channel, cellDrag.step);
      }
    }
  }

  async function onCellPointerUp() {
    if (!cellDrag) {
      return;
    }

    if (!cellDrag.moved) {
      const source = session.selectedSource;
      toggleCellOnRelease(session.kshState, source, cellDrag);
      await sendCell(source, cellDrag.channel, cellDrag.step);
    }

    showCellAdjustmentCursor();
    cellDrag = null;
  }

  function beginLoopDrag(channel, clientY, event) {
    loopDrag = {
      channel,
      startY: clientY,
      startValue: session.kshState.channels[channel].loopLength,
    };
    setSelectedCell(channel, session.selectedStep);
    event.currentTarget.setPointerCapture(event.pointerId);
  }

  function moveLoopDrag(clientY) {
    if (!loopDrag) {
      return;
    }
    setRowLoopLength(loopDrag.channel, loopDragNextValue(loopDrag, clientY));
  }

  function endLoopDrag() {
    loopDrag = null;
  }

  function channelFromMuteDrag(clientX, clientY) {
    const el = document.elementFromPoint(clientX, clientY);
    const row = el?.closest("[data-channel-row]");
    if (!row) {
      return -1;
    }
    return Number.parseInt(row.getAttribute("data-channel-row") ?? "-1", 10);
  }

  function applyMuteDragForChannel(channel) {
    if (!muteDrag || channel < 0 || channel === muteDrag.lastChannel) {
      return;
    }

    muteDrag = { ...muteDrag, lastChannel: channel };
    setSelectedCell(channel, session.selectedStep);

    const muted = session.kshState.sourceChannelMutes[muteDrag.source][channel];
    setSourceChannelMute(muteDrag.source, channel, !muted);
  }

  function onMutePointerDown(channel, event) {
    const now = Date.now();
    const source = session.selectedSource;

    if (sourceRowResetTap.channel === channel && now - sourceRowResetTap.at <= SOURCE_ROW_RESET_MS) {
      sourceRowResetTap = { channel: -1, at: 0 };
      muteDrag = null;
      resetSourceChannelRow(source, channel);
      return;
    }

    sourceRowResetTap = { channel, at: now };
    muteDrag = { source, lastChannel: -1 };
    applyMuteDragForChannel(channel);
    event.currentTarget.setPointerCapture(event.pointerId);
  }

  function onMutePointerMove(event) {
    if (!muteDrag || (event.buttons & 1) === 0) {
      return;
    }
    applyMuteDragForChannel(channelFromMuteDrag(event.clientX, event.clientY));
  }

  function endMuteDrag() {
    muteDrag = null;
  }

  function auditionChannelOnce(channel, keepEditor = false) {
    const now = Date.now();
    if (lastAudition.channel === channel && now - lastAudition.at < 60) {
      return;
    }
    lastAudition = { channel, at: now };
    if (!keepEditor && editingChannel >= 0) {
      editingChannel = -1;
    }
    auditionChannel(channel);
  }

  function onLabelClick(channel) {
    const now = Date.now();
    const isRenameTap = channelRenameTap.channel === channel && now - channelRenameTap.at <= CHANNEL_RENAME_MS;

    auditionChannelOnce(channel, isRenameTap);

    if (isRenameTap) {
      channelRenameTap = { channel: -1, at: 0 };
      editingChannel = channel;
      labelDraft = session.kshState.channels[channel]?.label ?? String(channel + 1);
      return;
    }

    channelRenameTap = { channel, at: now };
  }

  function commitLabelEdit() {
    if (editingChannel < 0) {
      return;
    }
    setChannelLabel(editingChannel, labelDraft);
    editingChannel = -1;
  }

  function cancelLabelEdit() {
    editingChannel = -1;
  }

  function syncHoverLayerModeFromModifiers(metaKey, shiftKey, altKey) {
    hoverLayerMode = modifierLayerMode(metaKey, shiftKey, altKey);
  }

  function onEditorKeyDown(event) {
    if (event.key === "1") {
      setSourceLayerMode("velocity");
    } else if (event.key === "2") {
      setSourceLayerMode("cycle");
    } else if (event.key === "3") {
      setSourceLayerMode("probability");
    } else if (event.key === "4") {
      setSourceLayerMode("roll");
    }
  }

  onMount(() => {
    const onKeyDown = (event) => {
      syncHoverLayerModeFromModifiers(event.metaKey, event.shiftKey, event.altKey);
      onEditorKeyDown(event);
    };

    const onKeyUp = (event) => {
      syncHoverLayerModeFromModifiers(event.metaKey, event.shiftKey, event.altKey);
    };

    const onBlur = () => {
      hoverLayerMode = null;
      showCellAdjustmentCursor();
    };

    window.addEventListener("keydown", onKeyDown);
    window.addEventListener("keyup", onKeyUp);
    window.addEventListener("blur", onBlur);
    return () => {
      window.removeEventListener("keydown", onKeyDown);
      window.removeEventListener("keyup", onKeyUp);
      window.removeEventListener("blur", onBlur);
      showCellAdjustmentCursor();
    };
  });
</script>

<div
  class="editor-view flex shrink-0 flex-col overflow-hidden bg-ksh-bg text-ksh-text"
  role="application"
  aria-label="KSH pattern editor"
  style={`width:${dims.width}px;height:${dims.height}px;`}
  onpointermove={(event) => {
    syncHoverLayerModeFromModifiers(event.metaKey, event.shiftKey, event.altKey);
  }}
  onpointerleave={(event) => {
    syncHoverLayerModeFromModifiers(event.metaKey, event.shiftKey, event.altKey);
  }}
>
  <header class="flex h-[68px] shrink-0 items-center border-b border-ksh-stroke-soft px-3 text-[11px]">
    <div class="header-section border-l-0 pl-0">
      <div class="flex flex-col items-start">
        <span class="header-label">Patterns</span>
        <div class="flex gap-0.5">
          {#each Array.from({ length: SOURCE_COUNT }, (_, source) => source) as source (source)}
            <button
              type="button"
              class={`header-button w-8 ${session.selectedSource === source ? "bg-ksh-amber text-ksh-off" : "bg-ksh-panel2 text-ksh-text"}`}
              onclick={() => selectSource(source)}
            >
              {source + 1}
            </button>
          {/each}
        </div>
      </div>
    </div>

    <div class="header-section">
      <HeaderValueDrag
        id="steps"
        label="Steps"
        value={session.kshState.stepCount}
        active={headerDrag?.id === "steps"}
        onBegin={beginHeaderDrag}
        onMove={moveHeaderDrag}
        onEnd={endHeaderDrag}
      />
      <div class="flex flex-col items-start">
        <span class="header-label">Step Value</span>
        <button
          type="button"
          class="header-button min-w-[68px] bg-ksh-panel2 text-ksh-text"
          onclick={(event) => cycleRateCommand(event.shiftKey ? -1 : 1)}
        >
          {session.kshState.rate}
        </button>
      </div>
    </div>

    <div class="header-section">
      <div class="flex flex-col items-start">
        <span class="header-label">Random</span>
        <button type="button" class="header-button min-w-[82px] bg-ksh-panel2 text-ksh-text" onclick={cycleMode}>
          {generationModeLabel(session.kshState.generationMode)}
        </button>
      </div>
      <HeaderValueDrag
        id="refresh"
        label="Rate"
        value={session.kshState.refreshSteps}
        active={headerDrag?.id === "refresh"}
        onBegin={beginHeaderDrag}
        onMove={moveHeaderDrag}
        onEnd={endHeaderDrag}
      />
    </div>

    <div class="header-section">
      <div class="flex items-center gap-3 pt-[15px]">
        <span class="text-[13px] font-semibold text-ksh-text">Pattern:</span>
        <button type="button" class="header-icon-button" onclick={() => shiftPattern(-1)}>◀</button>
        <button type="button" class="header-icon-button" onclick={() => shiftPattern(1)}>▶</button>
        <button type="button" class="header-icon-button text-[18px]" onclick={clearPattern}>×</button>
      </div>
    </div>

    <div class="ml-auto flex items-center">
      <div class="header-section">
        <HeaderValueDrag
          id="swing"
          label="Swing"
          value={session.kshState.swing}
          active={headerDrag?.id === "swing"}
          onBegin={beginHeaderDrag}
          onMove={moveHeaderDrag}
          onEnd={endHeaderDrag}
        />
        <HeaderValueDrag
          id="velocity_humanize"
          label="Vel %"
          value={session.kshState.velocityHumanize}
          active={headerDrag?.id === "velocity_humanize"}
          onBegin={beginHeaderDrag}
          onMove={moveHeaderDrag}
          onEnd={endHeaderDrag}
        />
        <HeaderValueDrag
          id="timing_humanize"
          label="Time %"
          value={session.kshState.timingHumanize}
          active={headerDrag?.id === "timing_humanize"}
          onBegin={beginHeaderDrag}
          onMove={moveHeaderDrag}
          onEnd={endHeaderDrag}
        />
      </div>

      <div class="header-section">
        <div class="flex flex-col items-start">
          <span class="header-label">Layer</span>
          <button type="button" class="header-button min-w-[76px] bg-ksh-panel2 text-ksh-text" onclick={cycleSourceLayerMode}>
            {sourceLayerLabel(effectiveLayerMode)}
          </button>
        </div>
      </div>

      <div class="header-section pr-0">
        <div class="flex gap-1 pt-[15px]">
          <button
            type="button"
            class={`header-button min-w-[54px] ${session.dcColors ? "bg-ksh-amber text-ksh-off" : "bg-ksh-panel2 text-ksh-text"}`}
            onclick={toggleDcColors}
          >
            DC
          </button>
          <button
            type="button"
            class={`header-button min-w-[54px] ${session.kshState.deviceActive ? "bg-ksh-amber text-ksh-off" : "bg-ksh-panel2 text-ksh-text"}`}
            onclick={toggleDeviceActive}
          >
            {session.kshState.deviceActive ? "ON" : "OFF"}
          </button>
        </div>
      </div>

      {#if session.kshState.standaloneTransportAvailable}
        <div class="header-section pr-0">
          <div class="flex flex-col items-start">
            <span class="header-label">Standalone</span>
            <div class="flex items-center gap-1">
              <button
                type="button"
                class={`header-button w-[38px] px-0 text-[12px] ${session.kshState.standaloneTransportPlaying ? "bg-ksh-amber text-ksh-off" : "bg-ksh-panel2 text-ksh-text"}`}
                title={session.kshState.standaloneTransportPlaying ? "Stop" : "Play"}
                onclick={() => setStandaloneTransportPlaying(!session.kshState.standaloneTransportPlaying)}
              >
                {session.kshState.standaloneTransportPlaying ? "■" : "▶"}
              </button>
              <div class="flex h-[28px] items-center rounded-sm bg-ksh-panel2">
                <button
                  type="button"
                  class="h-[28px] w-5 text-[12px] font-semibold text-ksh-blue"
                  title="Tempo down"
                  onclick={() => adjustStandaloneTempo(-1)}
                >
                  −
                </button>
                <span class="w-8 text-center text-[11px] font-semibold text-ksh-text">
                  {session.kshState.standaloneTempo}
                </span>
                <button
                  type="button"
                  class="h-[28px] w-5 text-[12px] font-semibold text-ksh-blue"
                  title="Tempo up"
                  onclick={() => adjustStandaloneTempo(1)}
                >
                  +
                </button>
              </div>
            </div>
          </div>
        </div>
      {/if}
    </div>
  </header>

  <div class="bg-ksh-grid flex min-h-0 flex-1 flex-col overflow-hidden px-3">
    <div class="shrink-0" style={`height:${gridTopPad}px`} aria-hidden="true"></div>
    <div class="shrink-0">
    <div class="flex items-center" style={`padding-left:${GRID_SIDEBAR_W}px`}>
      <div class="flex" bind:this={gridEl}>
      {#each stepCols as step (step)}
        <div
          class={`flex items-center justify-center text-[10px] ${stepLabelClass(step)}`}
          style={`width:${GRID_CELL_W}px;height:18px;`}
        >
          {step + 1}
        </div>
      {/each}
      </div>
    </div>

    {#each channelRows as channel (channel)}
      <div class="flex items-center py-0.5" data-channel-row={channel}>
        <div class="flex shrink-0 items-center gap-1 pr-2 font-medium" style={`width:${GRID_SIDEBAR_W}px`}>
          {#if editingChannel === channel}
            <input
              class="rounded border border-ksh-amber bg-ksh-off px-1 text-[13px] text-ksh-text outline-none"
              style={`width:${CHANNEL_LABEL_W}px`}
              bind:value={labelDraft}
              onkeydown={(event) => {
                if (event.key === "Enter") {
                  commitLabelEdit();
                } else if (event.key === "Escape") {
                  cancelLabelEdit();
                }
              }}
              onblur={commitLabelEdit}
            />
          {:else}
            <button
              type="button"
              class="channel-label shrink-0 truncate text-left text-[13px] text-ksh-text"
              style={`width:${CHANNEL_LABEL_W}px`}
              onclick={() => onLabelClick(channel)}
            >
              {session.kshState.channels[channel]?.label ?? channel + 1}
            </button>
          {/if}
          <button
            type="button"
            class="w-6 text-[13px] text-ksh-blue"
            onclick={(event) => {
              if (event.shiftKey) {
                adjustChannelNote(channel, -1);
              } else {
                incrementChannelNote(channel);
              }
            }}
          >
            {session.kshState.channels[channel]?.note ?? 36}
          </button>
          <button type="button" class="w-6 text-center text-[13px] text-ksh-blue" onclick={() => cycleChannelLock(channel)}>
            {lockLabel(session.kshState.channels[channel]?.lock ?? -1)}
          </button>
          <button
            type="button"
            class={`w-8 text-center text-[13px] ${loopLengthClass(channel)}`}
            onpointerdown={(event) => beginLoopDrag(channel, event.clientY, event)}
            onpointermove={(event) => {
              if (loopDrag?.channel === channel) {
                moveLoopDrag(event.clientY);
              }
            }}
            onpointerup={endLoopDrag}
            onpointercancel={endLoopDrag}
          >
            L{session.kshState.channels[channel]?.loopLength ?? 16}
          </button>
          <button
            type="button"
            class="w-5 text-center text-[13px] text-ksh-amber"
            onclick={() => cycleChannelPlaybackMode(channel)}
          >
            {playbackModeLabel(session.kshState.channels[channel]?.playbackMode ?? "normal")}
          </button>
          <div class="flex items-center">
            <button
              type="button"
              class="w-5 text-[13px] leading-none text-ksh-amber"
              onclick={() => shiftChannelRow(channel, -1)}
            >
              ◀
            </button>
            <button
              type="button"
              class="w-5 text-[13px] leading-none text-ksh-amber"
              onclick={() => shiftChannelRow(channel, 1)}
            >
              ▶
            </button>
          </div>
          <button
            type="button"
            class={`ml-1 h-3.5 w-3.5 rounded-full border ${session.kshState.sourceChannelMutes[session.selectedSource][channel] ? "border-ksh-amber bg-transparent" : "border-ksh-amber bg-ksh-amber"}`}
            aria-label="Mute channel"
            aria-pressed={session.kshState.sourceChannelMutes[session.selectedSource][channel] ? "true" : "false"}
            onpointerdown={(event) => onMutePointerDown(channel, event)}
            onpointermove={onMutePointerMove}
            onpointerup={endMuteDrag}
            onpointercancel={endMuteDrag}
          ></button>
        </div>

        <div class="flex">
          {#each Array.from({ length: MAX_STEPS }, (_, step) => step) as step (step)}
            <button
              type="button"
              class={cellClass(channel, step)}
              style={`width:${GRID_CELL_W}px;height:${GRID_CELL_H}px;${cellStyle(channel, step)}`}
              disabled={!isCellInteractive(channel, step)}
              onpointerdown={(event) => onCellPointerDown(event, channel, step)}
              onpointermove={onCellPointerMove}
              onpointerup={onCellPointerUp}
              onpointercancel={onCellPointerUp}
            >
              {#if effectiveLayerMode === "cycle" && isCellInteractive(channel, step) && session.kshState.sources[session.selectedSource][channel][step].enabled}
                <span class="pointer-events-none absolute left-2 top-2 leading-none">
                  {cyclePrimaryLabel(channel, step)}
                </span>
                <span class="pointer-events-none absolute bottom-2 right-2 leading-none">
                  {cycleOffsetLabel(session.kshState.sources[session.selectedSource][channel][step].cycleOffset)}
                </span>
              {:else if effectiveLayerMode === "cycle" && isStepBeyondLoopLength(session.kshState, channel, step) && session.kshState.sources[session.selectedSource][channel][step].enabled}
                <span class="pointer-events-none w-full text-center text-[9px] text-[#5c636b]">
                  {cyclePrimaryLabel(channel, step)}/{cycleOffsetLabel(session.kshState.sources[session.selectedSource][channel][step].cycleOffset)}
                </span>
              {:else if isStepBeyondLoopLength(session.kshState, channel, step) && session.kshState.sources[session.selectedSource][channel][step].enabled}
                <span class="pointer-events-none w-full text-center text-[9px] text-[#5c636b]">
                  {cellLabel(channel, step)}
                </span>
              {:else}
                {cellLabel(channel, step)}
              {/if}
            </button>
          {/each}
        </div>
      </div>
    {/each}
    </div>
    <div class="shrink-0" style={`height:${gridBottomPad}px`} aria-hidden="true"></div>
  </div>

</div>
