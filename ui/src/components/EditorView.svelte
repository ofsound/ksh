<script>
  import { onMount } from "svelte";
  import HeaderValueDrag from "./HeaderValueDrag.svelte";
  import {
    GRID_CELL_H,
    GRID_CELL_W,
    cycleOffsetLabel,
    editorDimensions,
    generationModeLabel,
    isStepBeyondLoopLength,
    channelColor,
    lockLabel,
    loopLengthForChannel,
    modifierLayerMode,
    mutedChannelColor,
    normalizeSourceLayerMode,
    playbackModeLabel,
    phaseOffsetMs,
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
  import { CHANNEL_RENAME_MS, MAX_STEPS, SOURCE_COUNT } from "../lib/kshConstants.js";
  import {
    adjustChannelNote,
    auditionChannel,
    clearPattern,
    closeEditor,
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
    shiftChannelRow,
    shiftPattern,
    toggleDcColors,
    toggleDeviceActive,
  } from "../lib/kshSession.svelte.js";

  let gridEl = $state(null);
  let headerDrag = $state(null);
  let cellDrag = $state(null);
  let loopDrag = $state(null);
  /** @type {{ source: number, paintMuted: number, touched: Record<number, boolean> } | null} */
  let muteDrag = $state(null);
  let hoverLayerMode = $state(null);
  let editingChannel = $state(-1);
  let labelDraft = $state("");
  let channelRenameTap = $state({ channel: -1, at: 0 });
  let sourceRowResetTap = $state({ channel: -1, at: 0 });
  let cycleCellTap = $state({ source: -1, channel: -1, step: -1, at: 0, wasEnabled: 0, valueMode: "" });
  let lastAudition = $state({ channel: -1, at: 0 });

  const dims = $derived(editorDimensions(session.kshState));
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

    let color = channelColor(channel, false, session.dcColors);
    let lightColor = channelColor(channel, true, session.dcColors);
    if (muted) {
      color = mutedChannelColor(color);
      lightColor = mutedChannelColor(lightColor);
    }

    if (!cell.enabled) {
      const downbeat = step % 4 === 0;
      return downbeat
        ? "background:#1e2228;color:#8c969e;opacity:0.55;"
        : "background:#1a1c21;color:#8c969e;opacity:0.55;";
    }

    const layerValue = sourceLayerValue(cell, effectiveLayerMode);
    const fill =
      effectiveLayerMode === "velocity"
        ? layerValue / 127
        : effectiveLayerMode === "probability"
          ? layerValue / 100
          : Math.min(1, layerValue / 8);

    if (effectiveLayerMode === "cycle" && cell.cycleInverted) {
      return `background:linear-gradient(to top, ${lightColor} ${Math.round(fill * 100)}%, rgba(26,28,33,0.85) ${Math.round(fill * 100)}%);color:#dbdee5;`;
    }

    return `background:linear-gradient(to top, ${color} ${Math.round(fill * 100)}%, rgba(26,28,33,0.85) ${Math.round(fill * 100)}%);color:#dbdee5;`;
  }

  function cellClass(channel, step) {
    const beyondSteps = step >= session.kshState.stepCount;
    const beyondLoop = isStepBeyondLoopLength(session.kshState, channel, step);
    const selected =
      session.selectedChannel === channel &&
      session.selectedStep === step &&
      !beyondSteps &&
      !beyondLoop;
    const cycleLayer = effectiveLayerMode === "cycle";

    return [
      "relative mr-0 flex overflow-hidden rounded-sm border text-[9px]",
      selected ? "border-ksh-text" : beyondLoop ? "border-ksh-stroke-soft/40" : "border-black/20",
      cycleLayer && !beyondSteps && !beyondLoop ? "cell-cycle" : "items-end justify-center",
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

  function cycleCellMatches(source, channel, step, valueMode) {
    return (
      cycleCellTap.source === source &&
      cycleCellTap.channel === channel &&
      cycleCellTap.step === step &&
      cycleCellTap.valueMode === valueMode &&
      Date.now() - cycleCellTap.at <= CHANNEL_RENAME_MS &&
      cycleCellTap.wasEnabled
    );
  }

  function handleCycleCellDoubleClick(source, channel, step, valueMode) {
    if (valueMode !== "cycle" || !cycleCellMatches(source, channel, step, valueMode)) {
      return false;
    }

    const cell = session.kshState.sources[source][channel][step];
    if (cell.cycle <= 1) {
      cycleCellTap = { source: -1, channel: -1, step: -1, at: 0, wasEnabled: 0, valueMode: "" };
      return false;
    }

    cell.enabled = 1;
    cell.cycleInverted = cell.cycleInverted ? 0 : 1;
    sendCell(source, channel, step);
    cycleCellTap = { source: -1, channel: -1, step: -1, at: 0, wasEnabled: 0, valueMode: "" };
    return true;
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

    if (handleCycleCellDoubleClick(source, channel, step, valueMode)) {
      return;
    }

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
    if (!cellDrag || (event.buttons & 1) === 0) {
      return;
    }

    if (!cellDrag.mode) {
      const mode = resolveCellDragMode(cellDrag, event.clientX, event.clientY);
      if (mode) {
        cellDrag = { ...cellDrag, mode, moved: true };
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
      const cell = session.kshState.sources[source][cellDrag.channel][cellDrag.step];
      const wasEnabled = cell.enabled ? 1 : 0;

      if (cellDrag.valueMode === "cycle" && wasEnabled && cell.cycle > 1) {
        cycleCellTap = {
          source,
          channel: cellDrag.channel,
          step: cellDrag.step,
          at: Date.now(),
          wasEnabled: 1,
          valueMode: "cycle",
        };
      } else {
        cycleCellTap = { source: -1, channel: -1, step: -1, at: 0, wasEnabled: 0, valueMode: "" };
      }

      toggleCellOnRelease(session.kshState, source, cellDrag);
      await sendCell(source, cellDrag.channel, cellDrag.step);
    }

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
    if (!muteDrag || channel < 0 || muteDrag.touched[channel]) {
      return;
    }

    muteDrag = {
      ...muteDrag,
      touched: { ...muteDrag.touched, [channel]: true },
    };
    setSelectedCell(channel, session.selectedStep);

    if (session.kshState.sourceChannelMutes[muteDrag.source][channel] === muteDrag.paintMuted) {
      return;
    }

    session.kshState.sourceChannelMutes[muteDrag.source][channel] = muteDrag.paintMuted;
    setSourceChannelMute(muteDrag.source, channel, muteDrag.paintMuted);
  }

  function onMutePointerDown(channel, event) {
    const now = Date.now();
    const source = session.selectedSource;

    if (sourceRowResetTap.channel === channel && now - sourceRowResetTap.at <= CHANNEL_RENAME_MS) {
      sourceRowResetTap = { channel: -1, at: 0 };
      muteDrag = null;
      resetSourceChannelRow(source, channel);
      return;
    }

    sourceRowResetTap = { channel, at: now };
    muteDrag = {
      source,
      paintMuted: session.kshState.sourceChannelMutes[source][channel] ? 0 : 1,
      touched: {},
    };
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

  function onEditorKeyDown(event) {
    if (event.key === "1") {
      setSourceLayerMode("velocity");
    } else if (event.key === "2") {
      setSourceLayerMode("cycle");
    } else if (event.key === "3") {
      setSourceLayerMode("probability");
    }
  }

  onMount(() => {
    const onKeyDown = (event) => {
      if (session.viewMode !== "editor") {
        return;
      }
      onEditorKeyDown(event);
    };

    window.addEventListener("keydown", onKeyDown);
    return () => window.removeEventListener("keydown", onKeyDown);
  });
</script>

<div
  class="editor-view overflow-hidden rounded-md border border-ksh-stroke-soft bg-ksh-bg text-ksh-text"
  role="application"
  aria-label="KSH pattern editor"
  style={`width:${dims.width}px;height:${dims.height}px;`}
  onpointermove={(event) => {
    hoverLayerMode = modifierLayerMode(event.metaKey, event.shiftKey, event.altKey);
  }}
  onpointerleave={() => {
    hoverLayerMode = null;
  }}
>
  <header class="flex flex-wrap items-end gap-3 border-b border-ksh-stroke-soft px-3 py-2 text-[11px]">
    <div class="flex items-center gap-1">
      {#each Array.from({ length: SOURCE_COUNT }, (_, source) => source) as source (source)}
        <button
          type="button"
          class={`rounded px-2 py-1 ${session.selectedSource === source ? "bg-ksh-amber text-ksh-off" : "bg-ksh-panel2 text-ksh-text"}`}
          onclick={() => selectSource(source)}
        >
          {source + 1}
        </button>
      {/each}
    </div>

    <HeaderValueDrag
      id="steps"
      label="Steps"
      value={session.kshState.stepCount}
      active={headerDrag?.id === "steps"}
      onBegin={beginHeaderDrag}
      onMove={moveHeaderDrag}
      onEnd={endHeaderDrag}
    />
    <button
      type="button"
      class="rounded bg-ksh-panel2 px-2 py-1"
      onclick={(event) => cycleRateCommand(event.shiftKey ? -1 : 1)}
    >
      {session.kshState.rate}
    </button>
    <button type="button" class="rounded bg-ksh-panel2 px-2 py-1" onclick={cycleMode}>
      {generationModeLabel(session.kshState.generationMode)}
    </button>
    <HeaderValueDrag
      id="refresh"
      label="Ref"
      value={session.kshState.refreshSteps}
      active={headerDrag?.id === "refresh"}
      onBegin={beginHeaderDrag}
      onMove={moveHeaderDrag}
      onEnd={endHeaderDrag}
    />

    <button type="button" class="rounded bg-ksh-panel2 px-2 py-1" onclick={() => shiftPattern(-1)}>◀</button>
    <button type="button" class="rounded bg-ksh-panel2 px-2 py-1" onclick={() => shiftPattern(1)}>▶</button>
    <button type="button" class="rounded bg-ksh-panel2 px-2 py-1" onclick={clearPattern}>Clear</button>

    <HeaderValueDrag
      id="phase_early_ms"
      label="Phase ms"
      value={phaseOffsetMs(session.kshState.phaseOffsetBeats, session.kshState.tempo)}
      active={headerDrag?.id === "phase_early_ms"}
      onBegin={beginHeaderDrag}
      onMove={moveHeaderDrag}
      onEnd={endHeaderDrag}
    />
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
      label="Vel"
      value={session.kshState.velocityHumanize}
      active={headerDrag?.id === "velocity_humanize"}
      onBegin={beginHeaderDrag}
      onMove={moveHeaderDrag}
      onEnd={endHeaderDrag}
    />
    <HeaderValueDrag
      id="timing_humanize"
      label="Time"
      value={session.kshState.timingHumanize}
      active={headerDrag?.id === "timing_humanize"}
      onBegin={beginHeaderDrag}
      onMove={moveHeaderDrag}
      onEnd={endHeaderDrag}
    />

    <button type="button" class="rounded bg-ksh-panel2 px-2 py-1" onclick={cycleSourceLayerMode}>
      {sourceLayerLabel(session.sourceLayerMode)}
    </button>
    <button type="button" class="rounded bg-ksh-panel2 px-2 py-1" onclick={toggleDcColors}>
      DC {session.dcColors ? "On" : "Off"}
    </button>
    <button
      type="button"
      class={`rounded px-2 py-1 ${session.kshState.deviceActive ? "bg-ksh-blue text-ksh-off" : "bg-ksh-panel2"}`}
      onclick={toggleDeviceActive}
    >
      {session.kshState.deviceActive ? "ON" : "OFF"}
    </button>
  </header>

  <div class="px-3 pt-2">
    <div class="flex items-center pl-[210px]">
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
        <div class="flex w-[210px] shrink-0 items-center gap-1 pr-2 text-[11px]">
          {#if editingChannel === channel}
            <input
              class="w-8 rounded border border-ksh-amber bg-ksh-off px-1 text-[11px] text-ksh-text outline-none"
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
            <button type="button" class="w-8 text-left text-ksh-text" onclick={() => onLabelClick(channel)}>
              {session.kshState.channels[channel]?.label ?? channel + 1}
            </button>
          {/if}
          <button
            type="button"
            class="w-6 text-ksh-blue"
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
          <button
            type="button"
            class={`w-8 text-center ${loopLengthClass(channel)}`}
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
          <button type="button" class="w-6 text-center text-ksh-blue" onclick={() => cycleChannelLock(channel)}>
            {lockLabel(session.kshState.channels[channel]?.lock ?? -1)}
          </button>
          <button
            type="button"
            class="w-5 text-center text-ksh-amber"
            onclick={() => cycleChannelPlaybackMode(channel)}
          >
            {playbackModeLabel(session.kshState.channels[channel]?.playbackMode ?? "normal")}
          </button>
          <button type="button" class="w-4 text-ksh-muted" onclick={() => shiftChannelRow(channel, -1)}>◀</button>
          <button type="button" class="w-4 text-ksh-muted" onclick={() => shiftChannelRow(channel, 1)}>▶</button>
          <button
            type="button"
            class={`h-3.5 w-3.5 rounded-full border ${session.kshState.sourceChannelMutes[session.selectedSource][channel] ? "border-ksh-muted bg-ksh-muted" : "border-ksh-stroke-soft bg-transparent"}`}
            aria-label="Mute channel"
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
              {#if effectiveLayerMode === "cycle" && isCellInteractive(channel, step)}
                <span class="pointer-events-none absolute left-1 top-1 text-[8px] leading-none">
                  {cyclePrimaryLabel(channel, step)}
                </span>
                {#if session.kshState.sources[session.selectedSource][channel][step].enabled}
                  <span class="pointer-events-none absolute bottom-1 right-1 text-[8px] leading-none">
                    {cycleOffsetLabel(session.kshState.sources[session.selectedSource][channel][step].cycleOffset)}
                  </span>
                {/if}
                <span class="cell-cycle-divider pointer-events-none absolute inset-0" aria-hidden="true"></span>
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

  <footer class="flex items-center justify-between border-t border-ksh-stroke-soft px-3 py-1.5 text-[11px]">
    <button type="button" class="rounded border border-ksh-stroke-soft px-3 py-1" onclick={closeEditor}>
      Compact
    </button>
    <span class="text-ksh-muted">
      Source {session.selectedSource + 1} · {session.kshState.channelCount} channel(s) · cycle layer: drag ↖ cycle ↘ offset · Shift/Cmd/Opt layers
    </span>
  </footer>
</div>
