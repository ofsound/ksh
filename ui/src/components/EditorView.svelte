<script>
  import { onMount } from "svelte";
  import HeaderValueDrag from "./HeaderValueDrag.svelte";
  import {
    GRID_CELL_H,
    GRID_CELL_W,
    editorDimensions,
    generationModeLabel,
    laneColor,
    lockLabel,
    modifierLayerMode,
    mutedLaneColor,
    normalizeSourceLayerMode,
    phaseOffsetMs,
    playbackModeLabel,
    sourceLayerLabel,
    sourceLayerValue,
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
  import { LANE_RENAME_MS, MAX_STEPS, SOURCE_COUNT } from "../lib/kshConstants.js";
  import {
    adjustLaneNote,
    auditionLane,
    clearPattern,
    closeEditor,
    cycleLaneLock,
    cycleLanePlaybackMode,
    cycleMode,
    cycleRateCommand,
    cycleSourceLayerMode,
    incrementLaneNote,
    isEditorFlashing,
    resetSourceChannelRow,
    selectSource,
    sendCell,
    sendCellsForLane,
    setSourceChannelMute,
    session,
    setHeaderValue,
    setLaneLabel,
    setRowLoopLength,
    setSelectedLane,
    setSourceLayerMode,
    shiftLaneRow,
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
  let editingLane = $state(-1);
  let labelDraft = $state("");
  let laneRenameTap = $state({ lane: -1, at: 0 });
  let sourceRowResetTap = $state({ lane: -1, at: 0 });
  let cycleCellTap = $state({ source: -1, lane: -1, step: -1, at: 0, wasEnabled: 0 });
  let lastAudition = $state({ lane: -1, at: 0 });

  const dims = $derived(editorDimensions(session.kshState));
  const laneRows = $derived(Array.from({ length: session.kshState.laneCount }, (_, lane) => lane));
  const stepCols = $derived(
    Array.from({ length: session.kshState.stepCount }, (_, step) => step)
  );
  const effectiveLayerMode = $derived(
    normalizeSourceLayerMode(hoverLayerMode ?? session.sourceLayerMode)
  );

  function gridLeft() {
    return gridEl?.getBoundingClientRect().left ?? 0;
  }

  function cellStyle(lane, step) {
    const source = session.selectedSource;
    const cell = session.kshState.sources[source][lane][step];
    const muted = session.kshState.sourceChannelMutes[source][lane];
    const inactive = step >= session.kshState.stepCount;
    const flashing = isEditorFlashing(source, lane, step);

    if (flashing) {
      return "background:#dbdee5;color:#1a1c21;";
    }

    if (inactive) {
      return "background:#242930;color:#5c636b;";
    }

    let color = laneColor(lane, false, session.dcColors);
    if (muted) {
      color = mutedLaneColor(color);
    }

    if (!cell.enabled) {
      return `background:${color};opacity:0.22;color:#8c969e;`;
    }

    const layerValue = sourceLayerValue(cell, effectiveLayerMode);
    const fill =
      effectiveLayerMode === "velocity"
        ? layerValue / 127
        : effectiveLayerMode === "probability"
          ? layerValue / 100
          : Math.min(1, layerValue / 8);

    return `background:linear-gradient(to top, ${color} ${Math.round(fill * 100)}%, rgba(26,28,33,0.85) ${Math.round(fill * 100)}%);color:#dbdee5;`;
  }

  function cellLabel(lane, step) {
    const cell = session.kshState.sources[session.selectedSource][lane][step];
    if (!cell.enabled) {
      return "";
    }
    return String(sourceLayerValue(cell, effectiveLayerMode));
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

  function cycleCellMatches(source, lane, step) {
    return (
      cycleCellTap.source === source &&
      cycleCellTap.lane === lane &&
      cycleCellTap.step === step &&
      Date.now() - cycleCellTap.at <= LANE_RENAME_MS &&
      cycleCellTap.wasEnabled
    );
  }

  function handleCycleCellDoubleClick(source, lane, step) {
    if (!cycleCellMatches(source, lane, step)) {
      return false;
    }

    const cell = session.kshState.sources[source][lane][step];
    if (cell.cycle <= 1) {
      cycleCellTap = { source: -1, lane: -1, step: -1, at: 0, wasEnabled: 0 };
      return false;
    }

    cell.enabled = 1;
    cell.cycleInverted = cell.cycleInverted ? 0 : 1;
    sendCell(source, lane, step);
    cycleCellTap = { source: -1, lane: -1, step: -1, at: 0, wasEnabled: 0 };
    return true;
  }

  function onCellPointerDown(event, lane, step) {
    if (step >= session.kshState.stepCount) {
      return;
    }

    const source = session.selectedSource;
    if (handleCycleCellDoubleClick(source, lane, step)) {
      return;
    }

    const cell = session.kshState.sources[source][lane][step];
    const layerMode =
      modifierLayerMode(event.metaKey, event.shiftKey, event.altKey) ?? session.sourceLayerMode;

    cellDrag = createCellDrag(source, lane, step, cell, layerMode, event.clientX, event.clientY);
    setSelectedLane(lane);
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
      if (changed.length > 0) {
        await sendCellsForLane(session.selectedSource, cellDrag.lane, changed);
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
        await sendCell(session.selectedSource, cellDrag.lane, cellDrag.step);
      }
    }
  }

  async function onCellPointerUp() {
    if (!cellDrag) {
      return;
    }

    if (!cellDrag.moved) {
      const source = session.selectedSource;
      const cell = session.kshState.sources[source][cellDrag.lane][cellDrag.step];
      const wasEnabled = cell.enabled ? 1 : 0;

      if (cellDrag.layerMode === "cycle" && wasEnabled && cell.cycle > 1) {
        cycleCellTap = {
          source,
          lane: cellDrag.lane,
          step: cellDrag.step,
          at: Date.now(),
          wasEnabled: 1,
        };
      } else {
        cycleCellTap = { source: -1, lane: -1, step: -1, at: 0, wasEnabled: 0 };
      }

      toggleCellOnRelease(session.kshState, source, cellDrag);
      await sendCell(source, cellDrag.lane, cellDrag.step);
    }

    cellDrag = null;
  }

  function beginLoopDrag(lane, clientY, event) {
    loopDrag = {
      lane,
      startY: clientY,
      startValue: session.kshState.lanes[lane].loopLength,
    };
    setSelectedLane(lane);
    event.currentTarget.setPointerCapture(event.pointerId);
  }

  function moveLoopDrag(clientY) {
    if (!loopDrag) {
      return;
    }
    setRowLoopLength(loopDrag.lane, loopDragNextValue(loopDrag, clientY));
  }

  function endLoopDrag() {
    loopDrag = null;
  }

  function laneFromMuteDrag(clientX, clientY) {
    const el = document.elementFromPoint(clientX, clientY);
    const row = el?.closest("[data-lane-row]");
    if (!row) {
      return -1;
    }
    return Number.parseInt(row.getAttribute("data-lane-row") ?? "-1", 10);
  }

  function applyMuteDragForLane(lane) {
    if (!muteDrag || lane < 0 || muteDrag.touched[lane]) {
      return;
    }

    muteDrag = {
      ...muteDrag,
      touched: { ...muteDrag.touched, [lane]: true },
    };
    setSelectedLane(lane);

    if (session.kshState.sourceChannelMutes[muteDrag.source][lane] === muteDrag.paintMuted) {
      return;
    }

    session.kshState.sourceChannelMutes[muteDrag.source][lane] = muteDrag.paintMuted;
    setSourceChannelMute(muteDrag.source, lane, muteDrag.paintMuted);
  }

  function onMutePointerDown(lane, event) {
    const now = Date.now();
    const source = session.selectedSource;

    if (sourceRowResetTap.lane === lane && now - sourceRowResetTap.at <= LANE_RENAME_MS) {
      sourceRowResetTap = { lane: -1, at: 0 };
      muteDrag = null;
      resetSourceChannelRow(source, lane);
      return;
    }

    sourceRowResetTap = { lane, at: now };
    muteDrag = {
      source,
      paintMuted: session.kshState.sourceChannelMutes[source][lane] ? 0 : 1,
      touched: {},
    };
    applyMuteDragForLane(lane);
    event.currentTarget.setPointerCapture(event.pointerId);
  }

  function onMutePointerMove(event) {
    if (!muteDrag || (event.buttons & 1) === 0) {
      return;
    }
    applyMuteDragForLane(laneFromMuteDrag(event.clientX, event.clientY));
  }

  function endMuteDrag() {
    muteDrag = null;
  }

  function auditionLaneOnce(lane, keepEditor = false) {
    const now = Date.now();
    if (lastAudition.lane === lane && now - lastAudition.at < 60) {
      return;
    }
    lastAudition = { lane, at: now };
    if (!keepEditor && editingLane >= 0) {
      editingLane = -1;
    }
    auditionLane(lane);
  }

  function onLabelClick(lane) {
    const now = Date.now();
    const isRenameTap = laneRenameTap.lane === lane && now - laneRenameTap.at <= LANE_RENAME_MS;

    auditionLaneOnce(lane, isRenameTap);

    if (isRenameTap) {
      laneRenameTap = { lane: -1, at: 0 };
      editingLane = lane;
      labelDraft = session.kshState.lanes[lane]?.label ?? String(lane + 1);
      return;
    }

    laneRenameTap = { lane, at: now };
  }

  function commitLabelEdit() {
    if (editingLane < 0) {
      return;
    }
    setLaneLabel(editingLane, labelDraft);
    editingLane = -1;
  }

  function cancelLabelEdit() {
    editingLane = -1;
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

    {#each laneRows as lane (lane)}
      <div class="flex items-center py-0.5" data-lane-row={lane}>
        <div class="flex w-[210px] shrink-0 items-center gap-1 pr-2 text-[11px]">
          {#if editingLane === lane}
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
            <button type="button" class="w-8 text-left text-ksh-text" onclick={() => onLabelClick(lane)}>
              {session.kshState.lanes[lane]?.label ?? lane + 1}
            </button>
          {/if}
          <button
            type="button"
            class="w-6 text-ksh-blue"
            onclick={(event) => {
              if (event.shiftKey) {
                adjustLaneNote(lane, -1);
              } else {
                incrementLaneNote(lane);
              }
            }}
          >
            {session.kshState.lanes[lane]?.note ?? 36}
          </button>
          <button
            type="button"
            class={`w-8 text-center ${loopDrag?.lane === lane ? "text-ksh-amber" : "text-ksh-blue"}`}
            onpointerdown={(event) => beginLoopDrag(lane, event.clientY, event)}
            onpointermove={(event) => {
              if (loopDrag?.lane === lane) {
                moveLoopDrag(event.clientY);
              }
            }}
            onpointerup={endLoopDrag}
            onpointercancel={endLoopDrag}
          >
            L{session.kshState.lanes[lane]?.loopLength ?? 16}
          </button>
          <button type="button" class="w-6 text-center text-ksh-blue" onclick={() => cycleLaneLock(lane)}>
            {lockLabel(session.kshState.lanes[lane]?.lock ?? -1)}
          </button>
          <button
            type="button"
            class="w-5 text-center text-ksh-amber"
            onclick={() => cycleLanePlaybackMode(lane)}
          >
            {playbackModeLabel(session.kshState.lanes[lane]?.playbackMode ?? "normal")}
          </button>
          <button type="button" class="w-4 text-ksh-muted" onclick={() => shiftLaneRow(lane, -1)}>◀</button>
          <button type="button" class="w-4 text-ksh-muted" onclick={() => shiftLaneRow(lane, 1)}>▶</button>
          <button
            type="button"
            class={`h-3.5 w-3.5 rounded-full border ${session.kshState.sourceChannelMutes[session.selectedSource][lane] ? "border-ksh-muted bg-ksh-muted" : "border-ksh-stroke-soft bg-transparent"}`}
            aria-label="Mute lane"
            onpointerdown={(event) => onMutePointerDown(lane, event)}
            onpointermove={onMutePointerMove}
            onpointerup={endMuteDrag}
            onpointercancel={endMuteDrag}
          ></button>
        </div>

        <div class="flex">
          {#each Array.from({ length: MAX_STEPS }, (_, step) => step) as step (step)}
            <button
              type="button"
              class="relative mr-0 flex items-end justify-center overflow-hidden rounded-sm border border-black/20 text-[9px]"
              style={`width:${GRID_CELL_W}px;height:${GRID_CELL_H}px;${cellStyle(lane, step)}`}
              disabled={step >= session.kshState.stepCount}
              onpointerdown={(event) => onCellPointerDown(event, lane, step)}
              onpointermove={onCellPointerMove}
              onpointerup={onCellPointerUp}
              onpointercancel={onCellPointerUp}
            >
              {cellLabel(lane, step)}
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
      Source {session.selectedSource + 1} · {session.kshState.laneCount} lane(s) · drag ↑↓ values · Shift/Cmd/Opt layers
    </span>
  </footer>
</div>
