<script>
  import { onMount } from "svelte";
  import { SvelteSet } from "svelte/reactivity";
  import HeaderValueDrag from "./HeaderValueDrag.svelte";
  import ChannelNoteControl from "./ChannelNoteControl.svelte";
  import PlaybackModeSelect from "./PlaybackModeSelect.svelte";
  import RowDisableIcon from "./RowDisableIcon.svelte";
  import { absorbPointerDragFocus, releasePointerDragFocus } from "./pointerDragFocus.js";
  import { onBackendEvent, parseBackendJson } from "../lib/kshBridge.js";
  import {
    CHANNEL_LABEL_W,
    GRID_CELL_LEFT_GAP,
    GRID_ROW_CELL_LEFT_GAP,
    GRID_SIDEBAR_W,
    cycleOffsetLabel,
    editorDimensions,
    gridCellHeight,
    gridCellPadding,
    gridCellWidth,
    gridRowPaddingY,
    gridTopPadding,
    STEP_LABEL_CELL_GAP,
    stepLabelFontPx,
    stepLabelOuterMargin,
    isStepBeyondLoopLength,
    channelToneColor,
    loopRangeForChannel,
    modifierLayerMode,
    mutedChannelColor,
    normalizeSourceLayerMode,
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
    resolveCellDragMode,
    stepFromGridX,
    toggleCellOnRelease,
  } from "../lib/kshEditorInteractions.js";
  import { CHANNEL_RENAME_MS, MAX_CHANNELS, MAX_STEPS, SILENT_SOURCE, SOURCE_COUNT, SOURCE_ROW_RESET_MS } from "../lib/kshConstants.js";
  import {
    auditionChannel,
    beginPatternCopy,
    cancelPatternCopy,
    clearPattern,
    clearSourceChannelSteps,
    copyPatternToSource,
    cycleSourceLayerMode,
    isEditorFlashing,
    resetSourceChannelRow,
    recordPatternRow,
    selectSource,
    sendCell,
    sendCellsForChannel,
    setSourceChannelMute,
    session,
    setRateCommand,
    setHeaderValue,
    setChannelLabel,
    setChannelPlaybackMode,
    setRowLoopRange,
    setSelectedCell,
    setSourceLayerMode,
    setStandaloneTransportPlaying,
    setStandaloneTempoFromInput,
    shiftChannelRow,
    beginEditGestureHistory,
    cancelEditGestureHistory,
    commitEditGestureHistory,
    createNewProject,
    cycleProject,
    redoEdit,
    redoStack,
    formatProjectDate,
    loadProject,
    shiftPattern,
    saveProject,
    togglePatternRecording,
    toggleSourceChannelSolo,
    undoEdit,
    undoStack,
  } from "../lib/kshSession.svelte.js";

  let headerDrag = $state(null);
  let cellDrag = $state(null);
  let loopRangeDrag = $state(null);
  /** @type {{ source: number, lastChannel: number } | null} */
  let muteDrag = $state(null);
  let hoverLayerMode = $state(null);
  let editingChannel = $state(-1);
  let labelDraft = $state("");
  let channelRenameTap = $state({ channel: -1, at: 0 });
  let sourceRowResetTap = $state({ channel: -1, at: 0 });
  let lastAudition = $state({ channel: -1, at: 0 });
  let stepValueMenuOpen = $state(false);
  let stepValueHighlightIndex = $state(-1);
  let stepValueGesturePointerId = $state(null);
  const recordPadCodes = ["KeyA", "KeyS", "KeyD", "KeyF", "KeyG", "KeyH", "KeyJ", "KeyK"];
  const recordPadKeysHeld = new SvelteSet();
  const stepValueOptions = [
    { value: "4n", mark: "♩", shortLabel: "Quarter", menuLabel: "Quarter" },
    { value: "4nt", mark: "♩³", shortLabel: "Quarter Triplet", menuLabel: "Quarter Triplet" },
    { value: "8n", mark: "♪", shortLabel: "Eighth", menuLabel: "Eighth" },
    { value: "8nt", mark: "♪³", shortLabel: "Eighth Triplet", menuLabel: "Eighth Triplet" },
    { value: "16n", mark: "♬", shortLabel: "16th", menuLabel: "16th" },
    { value: "16nt", mark: "♬³", shortLabel: "16th Triplet", menuLabel: "16th Triplet" },
    { value: "32n", mark: "♬", shortLabel: "32nd", menuLabel: "32nd" },
    { value: "32nt", mark: "♬³", shortLabel: "32nd Triplet", menuLabel: "32nd Triplet" },
  ];

  const patternScale = $derived(session.patternViewScale);
  const dims = $derived(editorDimensions(session.kshState, patternScale));
  const gridCellW = $derived(gridCellWidth(patternScale));
  const gridCellH = $derived(gridCellHeight(patternScale));
  const channelLabelFontPx = $derived(Math.round(gridCellH * 0.45));
  const gridRowPadY = $derived(gridRowPaddingY(patternScale));
  const cellFontPx = $derived(Math.round(18 * patternScale));
  const stepLabelFontSize = $derived(stepLabelFontPx(patternScale));
  const stepLabelMargin = $derived(stepLabelOuterMargin(patternScale));
  const cycleCellFontPx = $derived(Math.round(14 * patternScale));
  const cellInsetPx = $derived(Math.round(8 * patternScale));
  const gridTopPad = $derived(gridTopPadding(session.kshState.channelCount, dims.height, patternScale, session.kshState));
  const gridBottomPad = $derived(gridCellPadding(session.kshState.channelCount, dims.height, patternScale, session.kshState));
  const channelRows = $derived(Array.from({ length: session.kshState.channelCount }, (_, channel) => channel));
  const sourceButtonRows = $derived([
    Array.from({ length: SOURCE_COUNT / 2 }, (_, source) => source),
    Array.from({ length: SOURCE_COUNT / 2 }, (_, source) => source + SOURCE_COUNT / 2),
  ]);
  const stepCols = $derived(
    Array.from({ length: session.kshState.stepCount }, (_, step) => step)
  );
  const projectDateLabel = $derived(formatProjectDate(session.projectModifiedAt || session.projectCreatedAt));
  const allStepCols = $derived(Array.from({ length: MAX_STEPS }, (_, step) => step));
  const patternHeading = $derived(
    session.selectedSource === SILENT_SOURCE ? "M" : `P${session.selectedSource + 1}`
  );
  const patternHeadingWidthLabel = "P16: velocity";
  const effectiveLayerMode = $derived(
    normalizeSourceLayerMode(hoverLayerMode ?? session.sourceLayerMode)
  );
  const layerHeading = $derived(sourceLayerLabel(effectiveLayerMode).toLowerCase());
  const selectedStepValueOption = $derived(
    stepValueOptions.find((option) => option.value === session.kshState.rate) ?? stepValueOptions[4]
  );

  function stepAtClientX(clientX) {
    if (!cellDrag?.cellWidth) {
      return cellDrag?.step ?? 0;
    }

    return stepFromGridX(
      clientX,
      cellDrag.gridLeft,
      session.kshState.stepCount,
      cellDrag.cellWidth
    );
  }

  function cellStyleFromBackground(background, color, extra = "") {
    return `--cell-bg:${background};color:${color};${extra}`;
  }

  function activeCellBackground(background) {
    return `linear-gradient(to bottom, color-mix(in srgb, var(--color-text) 18%, transparent) 0%, transparent 36%, color-mix(in srgb, var(--color-app) 18%, transparent) 100%), ${background}`;
  }

  async function chooseStepValue(value) {
    stepValueMenuOpen = false;
    stepValueHighlightIndex = -1;
    await setRateCommand(value);
  }

  function removeStepValueGestureListeners() {
    window.removeEventListener("pointermove", onStepValueWindowPointerMove);
    window.removeEventListener("pointerup", onStepValueWindowPointerUp);
    window.removeEventListener("pointercancel", onStepValueWindowPointerUp);
  }

  function stepValueOptionIndexFromPoint(x, y) {
    const target = document.elementFromPoint(x, y);
    const optionEl = target?.closest?.("[data-step-value-option]");
    return optionEl ? Number.parseInt(optionEl.getAttribute("data-index") ?? "-1", 10) : -1;
  }

  function beginStepValueGesture(event) {
    event.stopPropagation();
    absorbPointerDragFocus(event);
    stepValueMenuOpen = true;
    stepValueGesturePointerId = event.pointerId;
    stepValueHighlightIndex = stepValueOptionIndexFromPoint(event.clientX, event.clientY);
    removeStepValueGestureListeners();
    window.addEventListener("pointermove", onStepValueWindowPointerMove);
    window.addEventListener("pointerup", onStepValueWindowPointerUp);
    window.addEventListener("pointercancel", onStepValueWindowPointerUp);
  }

  function onStepValueWindowPointerMove(event) {
    if (stepValueGesturePointerId !== event.pointerId) {
      return;
    }
    const index = stepValueOptionIndexFromPoint(event.clientX, event.clientY);
    if (index >= 0) {
      stepValueHighlightIndex = index;
    }
  }

  async function onStepValueWindowPointerUp(event) {
    if (stepValueGesturePointerId !== event.pointerId) {
      return;
    }
    const index = stepValueHighlightIndex >= 0
      ? stepValueHighlightIndex
      : stepValueOptionIndexFromPoint(event.clientX, event.clientY);
    stepValueGesturePointerId = null;
    removeStepValueGestureListeners();
    releasePointerDragFocus(event);
    if (index >= 0) {
      await chooseStepValue(stepValueOptions[index].value);
    }
  }

  function closeStepValueMenuOnFocusOut(event) {
    if (!event.currentTarget.contains(event.relatedTarget)) {
      stepValueMenuOpen = false;
    }
  }

  function cellStyle(channel, step) {
    const source = session.selectedSource;
    if (source === SILENT_SOURCE) {
      return step >= session.kshState.stepCount
        ? cellStyleFromBackground("var(--color-grid-inactive-step)", "var(--color-text-faint)", "opacity:0.55;")
        : cellStyleFromBackground("var(--color-grid-off-strong)", "var(--color-text-muted)");
    }

    const cell = session.kshState.sources[source][channel][step];
    const muted = session.kshState.sourceChannelMutes[source][channel];
    const beyondSteps = step >= session.kshState.stepCount;

    if (beyondSteps) {
      return cellStyleFromBackground("var(--color-grid-inactive-step)", "var(--color-text-faint)");
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
        ? cellStyleFromBackground("var(--color-grid-off-strong)", "var(--color-text-muted)")
        : cellStyleFromBackground("var(--color-grid-off)", "var(--color-text-muted)");
    }

    const layerValue = sourceLayerValue(cell, effectiveLayerMode);
    if (effectiveLayerMode === "cycle") {
      const topLeft = cell.cycleInverted ? lightColor : darkColor;
      const bottomRight = cell.cycleInverted ? darkColor : lightColor;
      return cellStyleFromBackground(
        activeCellBackground(`linear-gradient(to bottom right, ${topLeft} 0%, ${topLeft} calc(50% - 0.5px), ${dividerColor} calc(50% - 0.5px), ${dividerColor} calc(50% + 0.5px), ${bottomRight} calc(50% + 0.5px), ${bottomRight} 100%)`),
        "var(--color-text-inverse)"
      );
    }

    if (effectiveLayerMode === "roll") {
      const partCount = Math.max(1, Math.round(layerValue));
      const stops = Array.from({ length: partCount }, (_, part) => {
        const start = (part * 100) / partCount;
        const end = ((part + 1) * 100) / partCount;
        const tone = part % 2 === 0 ? darkColor : lightColor;
        return `${tone} ${start}%, ${tone} ${end}%`;
      }).join(", ");
      return cellStyleFromBackground(
        activeCellBackground(`linear-gradient(to right, ${stops})`),
        "var(--color-text-inverse)"
      );
    }

    const fill = effectiveLayerMode === "velocity" ? layerValue / 127 : layerValue / 100;
    const fillPercent = Math.round(fill * 100);

    return cellStyleFromBackground(
      activeCellBackground(`linear-gradient(to top, ${darkColor} 0%, ${darkColor} ${fillPercent}%, ${lightColor} ${fillPercent}%, ${lightColor} 100%)`),
      "var(--color-text-inverse)"
    );
  }

  function cellClass(channel, step) {
    const beyondSteps = step >= session.kshState.stepCount;
    const silent = session.selectedSource === SILENT_SOURCE;
    const cell = silent ? null : session.kshState.sources[session.selectedSource][channel][step];
    const cycleLayer = !silent && effectiveLayerMode === "cycle" && cell.enabled;
    const flashing = !silent && isEditorFlashing(session.selectedSource, channel, step);
    const active = !silent && !beyondSteps && cell.enabled;

    return [
      "ksh-grid-cell relative mr-0 flex overflow-hidden border border-grid-cell-border font-medium leading-none outline-none focus:outline-none focus-visible:outline-none",
      active ? "ksh-grid-cell-active" : "",
      flashing ? "ksh-cell-text-flash" : "",
      cycleLayer && !beyondSteps ? "cell-cycle" : "items-center justify-center",
    ].join(" ");
  }

  function cellLabel(channel, step) {
    if (session.selectedSource === SILENT_SOURCE) {
      return "";
    }

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
    if (session.selectedSource === SILENT_SOURCE) {
      return "";
    }

    const cell = session.kshState.sources[session.selectedSource][channel][step];
    if (!cell.enabled || effectiveLayerMode !== "cycle") {
      return "";
    }
    const value = sourceLayerValue(cell, "cycle");
    return `${cell.cycleInverted ? "!" : ""}${value}`;
  }

  function loopBraceStyle(channel) {
    const range = loopRangeForChannel(session.kshState, channel);
    return `left:${range.start * gridCellW}px;width:${range.length * gridCellW}px;height:${gridCellH}px;`;
  }

  function isCellInteractive(channel, step) {
    return session.selectedSource !== SILENT_SOURCE && step < session.kshState.stepCount && !isStepBeyondLoopLength(session.kshState, channel, step);
  }

  function stepLabelClass(step) {
    return session.playingStep > 0 && step + 1 === session.playingStep
      ? "text-text"
      : "text-text-muted";
  }

  function sourceButtonClass(source) {
    const selected = session.selectedSource === source;
    const copySource = session.patternCopySource === source;

    return [
      "pattern-slot-button",
      selected ? "bg-accent-strong text-text-inverse" : "bg-control-secondary text-control-secondary-text",
      copySource ? "ksh-pattern-copy-source" : "",
    ].join(" ");
  }

  function mutePatternClass() {
    return [
      "pattern-mute-button",
      session.selectedSource === SILENT_SOURCE ? "bg-accent-strong text-text-inverse" : "bg-control-secondary text-control-secondary-text",
    ].join(" ");
  }

  async function onSourceClick(event, source) {
    if (event.shiftKey) {
      if (session.patternCopySource === source) {
        cancelPatternCopy();
      } else {
        beginPatternCopy(source);
      }
      return;
    }

    if (session.patternCopySource >= 0) {
      await copyPatternToSource(source);
      return;
    }

    await selectSource(source);
  }

  async function onMutePatternClick() {
    cancelPatternCopy();
    await selectSource(SILENT_SOURCE);
  }

  function headerHistoryLabel(id) {
    switch (id) {
      case "steps":
        return "Change steps";
      case "refresh":
        return "Change refresh rate";
      case "swing":
        return "Change swing";
      case "velocity_humanize":
        return "Change velocity humanize";
      case "timing_humanize":
        return "Change timing humanize";
      default:
        return "Change value";
    }
  }

  function beginHeaderDrag(id, clientY) {
    beginEditGestureHistory();
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

  async function endHeaderDrag() {
    if (headerDrag) {
      await commitEditGestureHistory(headerHistoryLabel(headerDrag.id));
    }
    headerDrag = null;
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
      modifierLayerMode(event.shiftKey, event.altKey) ?? session.sourceLayerMode;
    const triangle =
      normalizeSourceLayerMode(layerMode) === "cycle"
        ? resolveCellTriangle(localX, localY, gridCellW, gridCellH)
        : null;
    const valueMode = valueModeForCellInteraction(layerMode, triangle);

    const cell = session.kshState.sources[source][channel][step];

    beginEditGestureHistory();
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
    cellDrag = {
      ...cellDrag,
      gridLeft: rect.left - step * rect.width,
      cellWidth: rect.width,
    };
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

    const pointerStep = stepAtClientX(event.clientX);

    if (!cellDrag.mode) {
      const mode = pointerStep !== cellDrag.step
        ? "paint"
        : resolveCellDragMode(cellDrag, event.clientX, event.clientY);
      if (mode) {
        cellDrag = { ...cellDrag, mode, moved: true };
      }
    }

    if (cellDrag.mode === "paint") {
      const toStep = pointerStep;
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

    await commitEditGestureHistory("Edit cell");
    cellDrag = null;
  }

  function beginLoopRangeDrag(channel, edge, clientX, event) {
    const range = loopRangeForChannel(session.kshState, channel);
    beginEditGestureHistory();
    loopRangeDrag = {
      channel,
      edge,
      startX: clientX,
      startStart: range.start,
      startLength: range.length,
    };
    setSelectedCell(channel, session.selectedStep);
    event.currentTarget.setPointerCapture(event.pointerId);
  }

  function moveLoopRangeDrag(clientX) {
    if (!loopRangeDrag) {
      return;
    }

    const deltaSteps = Math.round((clientX - loopRangeDrag.startX) / gridCellW);
    if (loopRangeDrag.edge === "left") {
      const originalEnd = loopRangeDrag.startStart + loopRangeDrag.startLength - 1;
      const nextStart = Math.min(
        Math.max(0, loopRangeDrag.startStart + deltaSteps),
        originalEnd
      );
      setRowLoopRange(loopRangeDrag.channel, nextStart, originalEnd - nextStart + 1);
      return;
    }

    const nextEnd = Math.max(
      loopRangeDrag.startStart,
      Math.min(session.kshState.stepCount - 1, loopRangeDrag.startStart + loopRangeDrag.startLength - 1 + deltaSteps)
    );
    setRowLoopRange(loopRangeDrag.channel, loopRangeDrag.startStart, nextEnd - loopRangeDrag.startStart + 1);
  }

  async function endLoopRangeDrag() {
    if (loopRangeDrag) {
      await commitEditGestureHistory("Change row range");
    }
    loopRangeDrag = null;
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

  async function onMutePointerDown(channel, event) {
    if (session.selectedSource === SILENT_SOURCE) {
      return;
    }

    const now = Date.now();
    const source = session.selectedSource;

    if (event.shiftKey) {
      sourceRowResetTap = { channel: -1, at: 0 };
      muteDrag = null;
      setSelectedCell(channel, session.selectedStep);
      event.preventDefault();
      await toggleSourceChannelSolo(source, channel);
      return;
    }

    if (sourceRowResetTap.channel === channel && now - sourceRowResetTap.at <= SOURCE_ROW_RESET_MS) {
      sourceRowResetTap = { channel: -1, at: 0 };
      muteDrag = null;
      resetSourceChannelRow(source, channel);
      return;
    }

    sourceRowResetTap = { channel, at: now };
    beginEditGestureHistory();
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

  async function endMuteDrag() {
    if (muteDrag) {
      await commitEditGestureHistory("Change channel mute");
    }
    muteDrag = null;
  }

  function historyButtonClass(enabled) {
    return [
      "project-history-button",
      enabled ? "project-history-button-active" : "project-history-button-disabled",
    ].join(" ");
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

  function syncHoverLayerModeFromModifiers(shiftKey, altKey) {
    hoverLayerMode = modifierLayerMode(shiftKey, altKey);
  }

  function modifierStateFromKeyEvent(event) {
    const key = event.key;
    const isKeyDown = event.type === "keydown";
    return {
      metaKey: key === "Meta" ? isKeyDown : event.metaKey,
      shiftKey: key === "Shift" ? isKeyDown : event.shiftKey,
      altKey: key === "Alt" ? isKeyDown : event.altKey,
    };
  }

  function syncHoverLayerModeFromKeyEvent(event) {
    const modifiers = modifierStateFromKeyEvent(event);
    syncHoverLayerModeFromModifiers(modifiers.shiftKey, modifiers.altKey);
  }

  function syncHoverLayerModeFromNativeModifiers(payload) {
    const modifiers = parseBackendJson(payload);
    syncHoverLayerModeFromModifiers(
      Boolean(modifiers?.shiftKey),
      Boolean(modifiers?.altKey)
    );
  }

  function onEditorKeyDown(event) {
    if (event.metaKey && !event.altKey && !event.ctrlKey && event.key.toLowerCase() === "z") {
      event.preventDefault();
      if (event.shiftKey) {
        redoEdit();
      } else {
        undoEdit();
      }
      return;
    }

    if (event.key === "Escape") {
      cancelPatternCopy();
      cancelEditGestureHistory();
    } else if (event.key === "1") {
      setSourceLayerMode("velocity");
    } else if (event.key === "2") {
      setSourceLayerMode("cycle");
    } else if (event.key === "3") {
      setSourceLayerMode("probability");
    } else if (event.key === "4") {
      setSourceLayerMode("roll");
    }
  }

  function textInputBlocked(event) {
    const target = event.target;
    return Boolean(target?.closest?.("input, textarea, select, [contenteditable='true']"));
  }

  function onStandaloneTransportShortcut(event) {
    if (!session.kshState.standaloneTransportAvailable || event.repeat || event.code !== "Space") {
      return false;
    }

    if (textInputBlocked(event)) {
      return false;
    }

    event.preventDefault();
    event.stopPropagation();
    void setStandaloneTransportPlaying(!session.kshState.standaloneTransportPlaying);
    return true;
  }

  function recordPadChannelForEvent(event) {
    const index = recordPadCodes.indexOf(event.code);
    return index >= 0 && index < Math.min(MAX_CHANNELS, session.kshState.channelCount) ? index : -1;
  }

  function onRecordPadKeyDown(event) {
    if (textInputBlocked(event)) {
      return;
    }

    const channel = recordPadChannelForEvent(event);
    if (channel < 0 || recordPadKeysHeld.has(event.code)) {
      return;
    }

    event.preventDefault();
    recordPadKeysHeld.add(event.code);

    if (session.patternRecordingEnabled) {
      void recordPatternRow(channel);
    } else {
      void auditionChannel(channel);
    }
  }

  function onRecordPadKeyUp(event) {
    if (recordPadKeysHeld.delete(event.code)) {
      event.preventDefault();
    }
  }

  onMount(() => {
    const removeModifierListener = onBackendEvent("modifier_keys", syncHoverLayerModeFromNativeModifiers);

    const onKeyDown = (event) => {
      if (onStandaloneTransportShortcut(event)) {
        return;
      }

      syncHoverLayerModeFromKeyEvent(event);
      onEditorKeyDown(event);
      onRecordPadKeyDown(event);
    };

    const onKeyUp = (event) => {
      syncHoverLayerModeFromKeyEvent(event);
      onRecordPadKeyUp(event);
    };

    const onBlur = () => {
      hoverLayerMode = null;
      recordPadKeysHeld.clear();
    };

    window.addEventListener("keydown", onKeyDown);
    window.addEventListener("keyup", onKeyUp);
    window.addEventListener("blur", onBlur);
    return () => {
      removeModifierListener();
      window.removeEventListener("keydown", onKeyDown);
      window.removeEventListener("keyup", onKeyUp);
      window.removeEventListener("blur", onBlur);
      removeStepValueGestureListeners();
    };
  });
</script>

<div
  class="editor-view flex shrink-0 flex-col overflow-hidden bg-app text-text"
  role="application"
  aria-label="KSH pattern editor"
  style={`width:${dims.width}px;height:${dims.height}px;`}
  onpointermove={(event) => {
    syncHoverLayerModeFromModifiers(event.shiftKey, event.altKey);
  }}
  onpointerleave={(event) => {
    syncHoverLayerModeFromModifiers(event.shiftKey, event.altKey);
  }}
>
  {#if session.kshState.standaloneTransportAvailable}
    <div class="flex h-[44px] shrink-0 items-center justify-end gap-2 border-b border-border-subtle px-3">
      <button
        type="button"
        aria-label={session.kshState.standaloneTransportPlaying ? "Stop standalone transport" : "Start standalone transport"}
        aria-pressed={Boolean(session.kshState.standaloneTransportPlaying)}
        class={`header-button min-w-[64px] px-3 text-[12px] ${session.kshState.standaloneTransportPlaying ? "bg-accent-strong text-text-inverse" : "bg-control-secondary text-text"}`}
        onclick={() => setStandaloneTransportPlaying(!session.kshState.standaloneTransportPlaying)}
      >
        {session.kshState.standaloneTransportPlaying ? "Stop" : "Play"}
      </button>
      <label class="flex items-center gap-1.5 text-[10px] font-semibold uppercase text-text-muted">
        BPM
        <input
          type="number"
          min="20"
          max="300"
          step="1"
          value={Math.round(session.kshState.standaloneTempo)}
          class="header-button h-[28px] w-[4.5rem] bg-control-secondary px-2 text-center text-[11px] font-semibold text-text outline-none focus:ring-1 focus:ring-focus-ring"
          onchange={setStandaloneTempoFromInput}
        />
      </label>
    </div>
  {/if}

  <div class="project-row flex h-[86px] shrink-0 items-center justify-center border-b border-border-subtle px-3">
    <div class="project-bar flex min-w-0 flex-1 items-center gap-3">
    <div class="project-controls flex h-[40px] min-w-0 flex-1 items-center justify-start gap-2">
      <button
        type="button"
        class="project-button"
        disabled={session.projectOperationBusy}
        onclick={createNewProject}
      >
        New
      </button>
      <button
        type="button"
        class="project-button"
        disabled={session.projectOperationBusy}
        onclick={loadProject}
      >
        Load
      </button>
      <button
        type="button"
        class="project-button"
        disabled={session.projectOperationBusy}
        onclick={saveProject}
      >
        Save
      </button>
      <button
        type="button"
        aria-label="Load previous project"
        title="Previous project"
        class="project-arrow-button"
        disabled={session.projectOperationBusy || !session.hasPreviousProject}
        onclick={() => cycleProject(-1)}
      >
        ◀
      </button>
      <div class="project-title-shell" title={session.projectFileName || "Unsaved project"}>
        <input
          aria-label="Project title"
          class="project-title-input"
          bind:value={session.projectName}
          placeholder="Untitled Project"
        />
        <input
          aria-label="Project description"
          class="project-description-input"
          bind:value={session.projectDescription}
          placeholder="Add Description..."
        />
        <span class="project-date">{projectDateLabel}</span>
      </div>
      <button
        type="button"
        aria-label="Load next project"
        title="Next project"
        class="project-arrow-button"
        disabled={session.projectOperationBusy || !session.hasNextProject}
        onclick={() => cycleProject(1)}
      >
        ▶
      </button>
    </div>
    <div class="project-trailing ml-auto">
    <div class="project-settings header-section">
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
    <div class="pattern-bank flex shrink-0 items-center gap-3">
      <span class="text-right text-[11px] font-extrabold text-text">Patterns:</span>
      <div class="grid grid-rows-2 gap-1.5">
        {#each sourceButtonRows as row, rowIndex (rowIndex)}
          <div class="flex gap-1.5">
            {#each row as source (source)}
              <button
                type="button"
                class={sourceButtonClass(source)}
                aria-pressed={session.selectedSource === source ? "true" : "false"}
                title={session.patternCopySource === source ? "Copy source selected" : "Shift-click to copy from this pattern"}
                onclick={(event) => onSourceClick(event, source)}
              >
                {source + 1}
              </button>
            {/each}
          </div>
        {/each}
      </div>
      <button
        type="button"
        class={mutePatternClass()}
        aria-pressed={session.selectedSource === SILENT_SOURCE ? "true" : "false"}
        title="Mute pattern"
        onclick={onMutePatternClick}
      >
        M
      </button>
    </div>
    </div>
    </div>
    {#if session.projectOperationError}
      <div class="project-error" role="status">
        {session.projectOperationError}
      </div>
    {/if}
  </div>

  <header class="flex h-[68px] shrink-0 items-center border-b border-border-subtle px-3 text-[11px]">
    <div class="grid h-full shrink-0 items-center">
      <span
        class="invisible col-start-1 row-start-1 h-[54px] whitespace-nowrap text-[54px] font-extrabold leading-none tracking-tighter"
        aria-hidden="true"
      >
        {patternHeadingWidthLabel}
      </span>
      <button
        type="button"
        class="col-start-1 row-start-1 h-[54px] w-full whitespace-nowrap text-left text-[54px] font-extrabold leading-none tracking-tighter text-accent-strong outline-none focus-visible:ring-1 focus-visible:ring-focus-ring"
        aria-label={session.selectedSource === SILENT_SOURCE ? `Mute pattern, ${layerHeading} layer` : `Pattern ${session.selectedSource + 1}, ${layerHeading} layer`}
        title="Click to cycle layer"
        onclick={cycleSourceLayerMode}
      >
        {patternHeading}: {layerHeading}
      </button>
    </div>

    <div class="header-section h-full items-center">
      <div class="flex items-center gap-2">
        <button
          type="button"
          class="header-icon-button text-danger"
          aria-label={session.patternRecordingEnabled ? "Stop pattern recording" : "Record pattern"}
          aria-pressed={Boolean(session.patternRecordingEnabled)}
          title={session.patternRecordingEnabled ? "Stop pattern recording" : "Record MIDI or ASDFGHJK into the selected pattern"}
          onclick={(event) => {
            event.currentTarget.blur();
            togglePatternRecording();
          }}
        >
          ●
        </button>
        <button type="button" class="header-icon-button" disabled={session.selectedSource === SILENT_SOURCE} onclick={() => shiftPattern(-1)}>◀</button>
        <button type="button" class="header-icon-button" disabled={session.selectedSource === SILENT_SOURCE} onclick={() => shiftPattern(1)}>▶</button>
        <button
          type="button"
          class="header-icon-button"
          disabled={session.selectedSource === SILENT_SOURCE}
          aria-label="Clear pattern"
          title="Double-click to clear this pattern"
          ondblclick={(event) => {
            event.currentTarget.blur();
            clearPattern();
          }}
        >
          ×
        </button>
      </div>
    </div>
    <div class="flex h-full flex-1 items-center justify-center">
      <div class="flex items-center gap-2">
        <button
          type="button"
          class="header-icon-button"
          aria-label="Undo"
          title="Undo"
          disabled={undoStack.length === 0}
          onclick={undoEdit}
        >
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
            <path d="M9 14 4 9l5-5" />
            <path d="M4 9h10a6 6 0 0 1 0 12h-2" />
          </svg>
        </button>
        <button
          type="button"
          class="header-icon-button"
          aria-label="Redo"
          title="Redo"
          disabled={redoStack.length === 0}
          onclick={redoEdit}
        >
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
            <path d="m15 14 5-5-5-5" />
            <path d="M20 9H10a6 6 0 0 0 0 12h2" />
          </svg>
        </button>
      </div>
    </div>

    <div class="ml-auto flex items-center">
      <div class="header-section border-l-0">
        <HeaderValueDrag
          id="steps"
          label="Steps"
          value={session.kshState.stepCount}
          suffix=" steps"
          horizontal
          compactHorizontal
          showLabel={false}
          active={headerDrag?.id === "steps"}
          onBegin={beginHeaderDrag}
          onMove={moveHeaderDrag}
          onEnd={endHeaderDrag}
        />
        <div
          class="relative flex items-center"
          onfocusout={closeStepValueMenuOnFocusOut}
        >
          <button
            type="button"
            class="header-button flex h-[42px] w-[250px] shrink-0 items-center justify-between gap-3 border border-border-subtle bg-transparent px-3.5 text-[16px] font-semibold text-text"
            aria-haspopup="listbox"
            aria-expanded={stepValueMenuOpen}
            onpointerdown={beginStepValueGesture}
          >
            <span class="flex min-w-0 items-center gap-2">
              <span class="w-9 shrink-0 text-left text-[21px] leading-none text-accent-strong">{selectedStepValueOption.mark}</span>
              <span class="whitespace-nowrap leading-tight">{selectedStepValueOption.shortLabel}</span>
            </span>
            <span class="shrink-0 text-[9px] text-text-muted">▾</span>
          </button>
          {#if stepValueMenuOpen}
            <div
              class="absolute left-0 top-full z-30 mt-1 w-[250px] overflow-hidden border border-border-strong bg-app shadow-[0_14px_34px_rgba(0,0,0,0.36)]"
              role="listbox"
              aria-label="Step value"
            >
              {#each stepValueOptions as option, index (option.value)}
                <button
                  type="button"
                  data-step-value-option
                  data-index={index}
                  class={`flex h-8 w-full items-center gap-2.5 px-3 text-left text-[11px] font-bold normal-case tracking-normal outline-none hover:bg-control-secondary focus-visible:bg-control-secondary ${stepValueHighlightIndex === index ? "bg-control-secondary" : ""} ${option.value === session.kshState.rate ? "text-accent-strong" : "text-text"}`}
                  role="option"
                  aria-selected={option.value === session.kshState.rate}
                  onpointerdown={beginStepValueGesture}
                >
                  <span class="w-8 shrink-0 whitespace-nowrap text-[15px] leading-none">{option.mark}</span>
                  <span class="flex-1 whitespace-nowrap">{option.menuLabel}</span>
                  <span class="w-4 text-right text-[12px]">{option.value === session.kshState.rate ? "✓" : ""}</span>
                </button>
              {/each}
            </div>
          {/if}
        </div>
      </div>
    </div>
  </header>

  <div class="bg-grid-bg flex min-h-0 flex-1 flex-col overflow-hidden px-3">
    <div class="shrink-0" style={`height:${gridTopPad}px`} aria-hidden="true"></div>
    <div class="shrink-0">
    <div
      class="flex items-center"
      style={`padding-left:${GRID_SIDEBAR_W + GRID_CELL_LEFT_GAP}px;margin-top:${stepLabelMargin}px;margin-bottom:${STEP_LABEL_CELL_GAP}px`}
    >
      <div class="flex">
      {#each stepCols as step (step)}
        <div
          class={`flex items-center justify-center leading-none ${stepLabelClass(step)}`}
          style={`width:${gridCellW}px;height:18px;font-size:${stepLabelFontSize}px;`}
        >
          {step + 1}
        </div>
      {/each}
      </div>
    </div>

    {#each channelRows as channel (channel)}
      <div
        class="flex items-center"
        style={`padding-top:${gridRowPadY}px;padding-bottom:${gridRowPadY}px;`}
        data-channel-row={channel}
      >
        <div class="flex shrink-0 items-center gap-0.5 pr-1 font-medium" style={`width:${GRID_SIDEBAR_W}px`}>
          <button
            type="button"
            class="row-clear-button mr-[30px]"
            disabled={session.selectedSource === SILENT_SOURCE}
            aria-label="Clear channel row steps"
            title="Double-click to clear this row"
            ondblclick={(event) => {
              event.currentTarget.blur();
              clearSourceChannelSteps(session.selectedSource, channel);
            }}
          >
            <span aria-hidden="true"></span>
          </button>
          {#if editingChannel === channel}
            <input
              class="rounded border border-accent bg-grid-off px-1 text-accent outline-none"
              style={`width:${CHANNEL_LABEL_W}px;height:${gridCellH}px;font-size:${channelLabelFontPx}px;line-height:1;`}
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
              class="channel-label flex shrink-0 items-center truncate text-left text-accent"
              style={`width:${CHANNEL_LABEL_W}px;height:${gridCellH}px;font-size:${channelLabelFontPx}px;line-height:1;`}
              onclick={() => onLabelClick(channel)}
            >
              {session.kshState.channels[channel]?.label ?? channel + 1}
            </button>
          {/if}
          <ChannelNoteControl {channel} />
          <PlaybackModeSelect
            value={session.kshState.channels[channel]?.playbackMode ?? "normal"}
            onChange={(mode) => setChannelPlaybackMode(channel, mode)}
          />
          <div class="flex items-center">
            <button
              type="button"
              class="w-5 text-[13px] leading-none text-accent"
              disabled={session.selectedSource === SILENT_SOURCE}
              onclick={() => shiftChannelRow(channel, -1)}
            >
              ◀
            </button>
            <button
              type="button"
              class="w-5 text-[13px] leading-none text-accent"
              disabled={session.selectedSource === SILENT_SOURCE}
              onclick={() => shiftChannelRow(channel, 1)}
            >
              ▶
            </button>
          </div>
          <button
            type="button"
            class={`channel-power-toggle ml-0.5 flex h-5 w-5 shrink-0 items-center justify-center border-0 bg-transparent p-0 outline-none focus-visible:ring-1 focus-visible:ring-focus-ring ${session.selectedSource === SILENT_SOURCE ? "opacity-35" : session.kshState.sourceChannelMutes[session.selectedSource][channel] ? "text-text-faint" : "text-accent"}`}
            aria-label={session.selectedSource !== SILENT_SOURCE && session.kshState.sourceChannelMutes[session.selectedSource][channel] ? "Turn channel on" : "Turn channel off"}
            aria-pressed={session.selectedSource !== SILENT_SOURCE && session.kshState.sourceChannelMutes[session.selectedSource][channel] ? "false" : "true"}
            disabled={session.selectedSource === SILENT_SOURCE}
            title="Shift-click to solo channel"
            onpointerdown={(event) => onMutePointerDown(channel, event)}
            onpointermove={onMutePointerMove}
            onpointerup={endMuteDrag}
            onpointercancel={endMuteDrag}
          >
            <RowDisableIcon class="channel-power-toggle-icon h-4 w-4" />
          </button>
        </div>

        <div class="relative flex" style={`margin-left:${GRID_ROW_CELL_LEFT_GAP}px`}>
          <div
            class="loop-range-brace pointer-events-none absolute top-0 z-10"
            style={loopBraceStyle(channel)}
            aria-hidden="true"
          ></div>
          <button
            type="button"
            class="loop-range-handle loop-range-handle-start absolute top-0 z-20 flex w-4 -translate-x-full items-center justify-center text-[15px] font-bold leading-none text-accent outline-none"
            style={`left:${loopRangeForChannel(session.kshState, channel).start * gridCellW}px;height:${gridCellH}px;`}
            aria-label="Move row range start"
            onpointerdown={(event) => beginLoopRangeDrag(channel, "left", event.clientX, event)}
            onpointermove={(event) => {
              if (loopRangeDrag?.channel === channel && loopRangeDrag?.edge === "left") {
                moveLoopRangeDrag(event.clientX);
              }
            }}
            onpointerup={endLoopRangeDrag}
            onpointercancel={endLoopRangeDrag}
          >
            ⋮
          </button>
          <button
            type="button"
            class="loop-range-handle loop-range-handle-end absolute top-0 z-20 flex w-4 items-center justify-center text-[15px] font-bold leading-none text-accent outline-none"
            style={`left:${(loopRangeForChannel(session.kshState, channel).end + 1) * gridCellW}px;height:${gridCellH}px;`}
            aria-label="Move row range end"
            onpointerdown={(event) => beginLoopRangeDrag(channel, "right", event.clientX, event)}
            onpointermove={(event) => {
              if (loopRangeDrag?.channel === channel && loopRangeDrag?.edge === "right") {
                moveLoopRangeDrag(event.clientX);
              }
            }}
            onpointerup={endLoopRangeDrag}
            onpointercancel={endLoopRangeDrag}
          >
            ⋮
          </button>
          {#each allStepCols as step (step)}
            {#if isStepBeyondLoopLength(session.kshState, channel, step)}
              <div
                class="shrink-0"
                style={`width:${gridCellW}px;height:${gridCellH}px;`}
                aria-hidden="true"
              ></div>
            {:else}
            <button
              type="button"
              class={cellClass(channel, step)}
              style={`width:${gridCellW}px;height:${gridCellH}px;font-size:${cellFontPx}px;${cellStyle(channel, step)}`}
              disabled={!isCellInteractive(channel, step)}
              onpointerdown={(event) => onCellPointerDown(event, channel, step)}
              onpointermove={onCellPointerMove}
              onpointerup={onCellPointerUp}
              onpointercancel={onCellPointerUp}
            >
              {#if session.selectedSource !== SILENT_SOURCE && effectiveLayerMode === "cycle" && isCellInteractive(channel, step) && session.kshState.sources[session.selectedSource][channel][step].enabled}
                <span
                  class="pointer-events-none absolute leading-none"
                  style={`left:${cellInsetPx}px;top:${cellInsetPx}px;font-size:${cycleCellFontPx}px;`}
                >
                  {cyclePrimaryLabel(channel, step)}
                </span>
                <span
                  class="pointer-events-none absolute leading-none"
                  style={`right:${cellInsetPx}px;bottom:${cellInsetPx}px;font-size:${cycleCellFontPx}px;`}
                >
                  {cycleOffsetLabel(session.kshState.sources[session.selectedSource][channel][step].cycleOffset)}
                </span>
              {:else}
                {cellLabel(channel, step)}
              {/if}
            </button>
            {/if}
          {/each}
        </div>
      </div>
    {/each}
    </div>
    <div class="shrink-0" style={`height:${gridBottomPad}px`} aria-hidden="true"></div>
  </div>

</div>
