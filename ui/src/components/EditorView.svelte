<script>
  import { onMount, tick } from "svelte";
  import { SvelteSet } from "svelte/reactivity";
  import HeaderValueDrag from "./HeaderValueDrag.svelte";
  import HeaderDiscreteSelect from "./HeaderDiscreteSelect.svelte";
  import ChannelNoteControl from "./ChannelNoteControl.svelte";
  import PlaybackModeSelect from "./PlaybackModeSelect.svelte";
  import MoveAroundIcon from "./MoveAroundIcon.svelte";
  import NudgeTriangleIcon from "./NudgeTriangleIcon.svelte";
  import KshCyclePatternEditor from "./KshCyclePatternEditor.svelte";
  import RowDisableIcon from "./RowDisableIcon.svelte";
  import { absorbPointerDragFocus, releasePointerDragFocus } from "./pointerDragFocus.js";
  import { onBackendEvent, parseBackendJson } from "../lib/kshBridge.js";
  import {
    CHANNEL_LABEL_W,
    GRID_GUTTER_PX,
    GRID_ROW_CELL_LEFT_GAP,
    GRID_SIDEBAR_W,
    GRID_NUDGE_BUTTON_W,
    GRID_NUDGE_GAP,
    GRID_NUDGE_LANE_W,
    editorDimensions,
    gridCellHeight,
    gridCellPadding,
    gridCellWidth,
    gridCellFontPx,
    gridCellInsetPx,
    gridLoopHandleWidth,
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
    isCyclePositionActive,
    normalizeCyclePattern,
  } from "../lib/cyclePattern.js";
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
  import {
    bulkDragLabel,
    cellSelectionKey,
    selectedCellLocations,
    wrappedCellDestinations,
  } from "../lib/kshBulkEdit.js";
  import {
    cellSelection,
    clearEditSelection,
    selectedCellKeys,
  } from "../lib/kshCellSelection.svelte.js";
  import { cloneCell, defaultCell } from "../lib/kshUiState.js";
  import {
    CHANNEL_RENAME_MS,
    DEFAULT_SWING_SUBDIVISION_INDEX,
    MAX_CHANNELS,
    MAX_STEPS,
    SILENT_SOURCE,
    SOURCE_COUNT,
    SOURCE_ROW_RESET_MS,
    SWING_SUBDIVISION_OPTIONS,
  } from "../lib/kshConstants.js";
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
  let editGesture = $state(null);
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
  let stepValueMenuCloseTimer = null;
  let stepValueMenuRoot = $state(null);
  let cyclePopover = $state(null);
  let cyclePopoverRoot = $state(null);
  let cyclePopoverAnchor = null;
  let shiftHeld = false;
  let clearBlink = $state({ pattern: false, rowChannel: -1 });
  let patternClearBlinkTimer = null;
  let rowClearBlinkTimer = null;
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
  const gridCellW = $derived(gridCellWidth(patternScale, session.kshState.stepCount));
  // Keep every brace anchored to the 16-step grid's left edge. Shorter braces
  // should leave their unused space to the right instead of being centered.
  const gridOffsetX = 0;
  const gridRowLeftX = $derived(
    GRID_GUTTER_PX + GRID_SIDEBAR_W + GRID_ROW_CELL_LEFT_GAP + gridOffsetX
  );
  const nudgeLaneLeft = $derived(
    dims.width - GRID_GUTTER_PX - GRID_NUDGE_LANE_W - gridRowLeftX
  );
  const gridCellH = $derived(gridCellHeight(patternScale));
  const channelLabelFontPx = $derived(Math.round(gridCellH * 0.45));
  const gridRowPadY = $derived(gridRowPaddingY(patternScale));
  const cellFontPx = $derived(gridCellFontPx(patternScale, session.kshState.stepCount));
  const stepLabelFontSize = $derived(stepLabelFontPx(patternScale));
  const stepLabelMargin = $derived(stepLabelOuterMargin(patternScale));
  const cellInsetPx = $derived(gridCellInsetPx(patternScale, session.kshState.stepCount));
  const loopHandleW = $derived(gridLoopHandleWidth(patternScale, session.kshState.stepCount));
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
  const cyclePopoverCell = $derived.by(() => {
    if (!cyclePopover || session.selectedSource === SILENT_SOURCE) {
      return null;
    }

    return session.kshState.sources[session.selectedSource][cyclePopover.channel][cyclePopover.step];
  });
  const cyclePopoverTheme = $derived.by(() => {
    const channel = cyclePopover?.channel ?? 0;
    const muted = session.selectedSource !== SILENT_SOURCE
      && session.kshState.sourceChannelMutes[session.selectedSource][channel];
    let light = channelToneColor(channel, "light", session.dcColors);
    let dark = channelToneColor(channel, "dark", session.dcColors);
    let divider = channelToneColor(channel, "divider", session.dcColors);

    if (muted) {
      light = mutedChannelColor(light);
      dark = mutedChannelColor(dark);
      divider = mutedChannelColor(divider);
    }

    return { light, dark, divider };
  });
  const cyclePopoverThemeStyle = $derived.by(() => {
    const { light, dark, divider } = cyclePopoverTheme;
    const surface = `color-mix(in srgb, ${dark} 30%, var(--color-app))`;
    const raisedSurface = `color-mix(in srgb, ${light} 12%, ${surface})`;
    return `--cycle-row-light:${light};--cycle-row-dark:${dark};--cycle-row-divider:${divider};--cycle-row-surface:${surface};background:linear-gradient(to bottom, ${raisedSurface}, ${surface});box-shadow:0 10px 28px color-mix(in srgb, var(--color-app) 42%, rgba(0,0,0,0.45)),0 0 24px color-mix(in srgb, var(--color-text) 4%, transparent);`;
  });
  const layerHeading = $derived(sourceLayerLabel(effectiveLayerMode).toLowerCase());
  const selectedStepValueOption = $derived(
    stepValueOptions.find((option) => option.value === session.kshState.rate) ?? stepValueOptions[4]
  );
  const selectedCellCount = $derived(
    selectedCellLocations(
      selectedCellKeys,
      session.kshState.channelCount,
      session.kshState.stepCount,
    ).length,
  );
  const marqueeRectStyle = $derived(editGesture?.kind === "marquee"
    ? `left:${Math.min(editGesture.startX, editGesture.currentX)}px;top:${Math.min(editGesture.startY, editGesture.currentY)}px;width:${Math.abs(editGesture.currentX - editGesture.startX)}px;height:${Math.abs(editGesture.currentY - editGesture.startY)}px;`
    : "");
  const bulkDragPreviewCells = $derived.by(() => {
    if (!editGesture || editGesture.kind !== "drag" || !editGesture.didMove) {
      return new Map();
    }

    const locations = selectedCellLocations(
      selectedCellKeys,
      session.kshState.channelCount,
      session.kshState.stepCount,
    );
    const destinations = wrappedCellDestinations(
      locations,
      { channel: editGesture.anchorChannel, step: editGesture.anchorStep },
      { channel: editGesture.currentChannel, step: editGesture.currentStep },
      session.kshState.channelCount,
      session.kshState.stepCount,
    );

    return new Map(
      destinations.map(({ source, destination }) => [
        destination.key,
        session.kshState.sources[session.selectedSource][source.channel][source.step],
      ]),
    );
  });
  const bulkDragPreviewKeys = $derived(new Set(bulkDragPreviewCells.keys()));

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

  function toggleEditMode() {
    if (cellSelection.editMode) {
      cancelEditGesture();
      clearEditSelection();
    }

    cellSelection.editMode = !cellSelection.editMode;
  }

  function cellAtPoint(clientX, clientY) {
    const element = document.elementFromPoint(clientX, clientY)?.closest?.("[data-ksh-edit-cell]");
    if (!(element instanceof HTMLElement)) {
      return null;
    }

    const channel = Number.parseInt(element.dataset.channel ?? "-1", 10);
    const step = Number.parseInt(element.dataset.step ?? "-1", 10);
    if (channel < 0 || step < 0) {
      return null;
    }

    return { channel, step };
  }

  function bulkDragCellAtPoint(clientX, clientY) {
    const target = cellAtPoint(clientX, clientY);
    if (target || !editGesture || editGesture.kind !== "drag") {
      return target;
    }

    const firstCell = document.querySelector("[data-ksh-edit-cell]");
    const firstRow = document.querySelector('[data-channel-row="0"]');
    if (!(firstCell instanceof HTMLElement) || !(firstRow instanceof HTMLElement)) {
      return null;
    }

    const firstCellRect = firstCell.getBoundingClientRect();
    const firstStep = Number.parseInt(firstCell.dataset.step ?? "0", 10);
    const cellWidth = firstCellRect.width;
    const gridLeft = firstCellRect.left - firstStep * cellWidth;
    const gridRight = gridLeft + session.kshState.stepCount * cellWidth;
    if (clientX < gridLeft || clientX >= gridRight) {
      return null;
    }

    const rowRect = firstRow.getBoundingClientRect();
    const rowHeight = rowRect.height;
    if (rowHeight <= 0) {
      return null;
    }

    return {
      channel: Math.floor((clientY - rowRect.top) / rowHeight),
      step: stepFromGridX(clientX, gridLeft, session.kshState.stepCount, cellWidth),
    };
  }

  function rectsIntersect(left, right) {
    return left.left <= right.right
      && left.right >= right.left
      && left.top <= right.bottom
      && left.bottom >= right.top;
  }

  function marqueeRect() {
    if (!editGesture || editGesture.kind !== "marquee") {
      return { left: 0, top: 0, right: 0, bottom: 0 };
    }

    return {
      left: Math.min(editGesture.startX, editGesture.currentX),
      top: Math.min(editGesture.startY, editGesture.currentY),
      right: Math.max(editGesture.startX, editGesture.currentX),
      bottom: Math.max(editGesture.startY, editGesture.currentY),
    };
  }

  function updateMarqueeSelection() {
    if (!editGesture || editGesture.kind !== "marquee") {
      return;
    }

    const next = new SvelteSet(editGesture.baseKeys);
    const selectionRect = marqueeRect();

    for (const element of document.querySelectorAll("[data-ksh-edit-cell]")) {
      if (!(element instanceof HTMLElement) || !rectsIntersect(element.getBoundingClientRect(), selectionRect)) {
        continue;
      }

      const channel = Number.parseInt(element.dataset.channel ?? "-1", 10);
      const step = Number.parseInt(element.dataset.step ?? "-1", 10);
      if (channel >= 0 && step >= 0) {
        next.add(cellSelectionKey(channel, step));
      }
    }

    selectedCellKeys.clear();
    for (const key of next) {
      selectedCellKeys.add(key);
    }
  }

  function removeEditGestureListeners() {
    document.removeEventListener("pointermove", onEditPointerMove);
    document.removeEventListener("pointerup", onEditPointerUp);
    document.removeEventListener("pointercancel", onEditPointerCancel);
  }

  function beginMarquee(event) {
    if (event.button !== 0 || editGesture) {
      return;
    }

    event.preventDefault();
    event.stopPropagation();
    editGesture = {
      kind: "marquee",
      pointerId: event.pointerId,
      startX: event.clientX,
      startY: event.clientY,
      currentX: event.clientX,
      currentY: event.clientY,
      baseKeys: new Set(),
    };
    updateMarqueeSelection();
    document.addEventListener("pointermove", onEditPointerMove);
    document.addEventListener("pointerup", onEditPointerUp);
    document.addEventListener("pointercancel", onEditPointerCancel);
  }

  function toggleSelectedCell(channel, step) {
    const key = cellSelectionKey(channel, step);
    if (selectedCellKeys.has(key)) {
      selectedCellKeys.delete(key);
    } else {
      selectedCellKeys.add(key);
    }
  }

  function beginBulkDrag(event, channel, step) {
    if (event.button !== 0 || editGesture || !selectedCellKeys.has(cellSelectionKey(channel, step))) {
      return;
    }

    event.preventDefault();
    event.stopPropagation();
    editGesture = {
      kind: "drag",
      pointerId: event.pointerId,
      startX: event.clientX,
      startY: event.clientY,
      currentChannel: channel,
      currentStep: step,
      anchorChannel: channel,
      anchorStep: step,
      mode: event.altKey ? "copy" : "move",
      didMove: false,
    };
    document.addEventListener("pointermove", onEditPointerMove);
    document.addEventListener("pointerup", onEditPointerUp);
    document.addEventListener("pointercancel", onEditPointerCancel);
  }

  function onEditPointerMove(event) {
    if (!editGesture || event.pointerId !== editGesture.pointerId) {
      return;
    }

    if ((event.buttons & 1) === 0) {
      void onEditPointerUp(event);
      return;
    }

    if (editGesture.kind === "marquee") {
      editGesture = { ...editGesture, currentX: event.clientX, currentY: event.clientY };
      updateMarqueeSelection();
      return;
    }

    const target = bulkDragCellAtPoint(event.clientX, event.clientY);
    const distance = Math.hypot(event.clientX - editGesture.startX, event.clientY - editGesture.startY);
    if (!target && !editGesture.didMove) {
      return;
    }

    if (distance >= 5 && !editGesture.didMove) {
      beginEditGestureHistory();
    }

    editGesture = {
      ...editGesture,
      ...(target ? { currentChannel: target.channel, currentStep: target.step } : {}),
      mode: event.altKey ? "copy" : "move",
      didMove: editGesture.didMove || distance >= 5,
    };
  }

  async function onEditPointerUp(event) {
    if (!editGesture || event.pointerId !== editGesture.pointerId) {
      return;
    }

    const gesture = editGesture;
    removeEditGestureListeners();
    editGesture = null;

    if (gesture.kind === "drag" && gesture.didMove) {
      gesture.mode = event.altKey ? "copy" : gesture.mode;
      await applySelectedCells(
        gesture.anchorChannel,
        gesture.anchorStep,
        gesture.currentChannel,
        gesture.currentStep,
        gesture.mode,
      );
      await commitEditGestureHistory(gesture.mode === "copy" ? "Copy selected cells" : "Move selected cells");
    }
  }

  function onEditPointerCancel(event) {
    if (!editGesture || event.pointerId !== editGesture.pointerId) {
      return;
    }

    const gesture = editGesture;
    removeEditGestureListeners();
    editGesture = null;
    if (gesture.kind === "marquee") {
      selectedCellKeys.clear();
      for (const key of gesture.baseKeys) {
        selectedCellKeys.add(key);
      }
    }
    cancelEditGestureHistory();
  }

  function cancelEditGesture() {
    if (!editGesture) {
      return;
    }

    onEditPointerCancel({ pointerId: editGesture.pointerId });
  }

  function onGridPointerDown(event) {
    if (!cellSelection.editMode || event.button !== 0) {
      return;
    }

    const target = event.target;
    if (target instanceof Element && target.closest("button, input, select, textarea")) {
      return;
    }

    beginMarquee(event);
  }

  async function applySelectedCells(anchorChannel, anchorStep, targetChannel, targetStep, mode = "move") {
    const source = session.selectedSource;
    if (source === SILENT_SOURCE) {
      return;
    }

    const locations = selectedCellLocations(
      selectedCellKeys,
      session.kshState.channelCount,
      session.kshState.stepCount,
    );
    if (locations.length === 0) {
      return;
    }

    const destinations = wrappedCellDestinations(
      locations,
      { channel: anchorChannel, step: anchorStep },
      { channel: targetChannel, step: targetStep },
      session.kshState.channelCount,
      session.kshState.stepCount,
    );
    const sourceSnapshots = new Map(
      locations.map(({ channel, step, key }) => [key, cloneCell(session.kshState.sources[source][channel][step])]),
    );
    const destinationKeys = new Set(destinations.map(({ destination }) => destination.key));
    const affectedByChannel = new Map();

    const markAffected = (channel, step) => {
      const steps = affectedByChannel.get(channel) ?? new Set();
      steps.add(step);
      affectedByChannel.set(channel, steps);
    };

    for (const { source: sourceLocation, destination } of destinations) {
      session.kshState.sources[source][destination.channel][destination.step] = cloneCell(
        sourceSnapshots.get(sourceLocation.key),
      );
      markAffected(destination.channel, destination.step);
    }

    if (mode === "move") {
      for (const { channel, step, key } of locations) {
        if (destinationKeys.has(key)) {
          continue;
        }

        session.kshState.sources[source][channel][step] = defaultCell();
        markAffected(channel, step);
      }
    }

    for (const [channel, steps] of affectedByChannel) {
      await sendCellsForChannel(source, channel, [...steps].sort((left, right) => left - right));
    }

    clearEditSelection();
    for (const { destination } of destinations) {
      selectedCellKeys.add(destination.key);
    }
  }

  function cellStyleFromBackground(background, color, extra = "") {
    return `--cell-bg:${background};color:${color};${extra}`;
  }

  function activeCellBackground(background) {
    return `linear-gradient(to bottom, color-mix(in srgb, var(--color-text) 18%, transparent) 0%, transparent 36%, color-mix(in srgb, var(--color-app) 18%, transparent) 100%), ${background}`;
  }

  async function chooseStepValue(value) {
    closeStepValueMenu();
    await setRateCommand(value);
  }

  function cancelStepValueMenuClose() {
    if (stepValueMenuCloseTimer !== null) {
      clearTimeout(stepValueMenuCloseTimer);
      stepValueMenuCloseTimer = null;
    }
  }

  function closeStepValueMenu() {
    cancelStepValueMenuClose();
    stepValueMenuOpen = false;
    stepValueHighlightIndex = -1;
  }

  function closeCyclePopover() {
    cyclePopover = null;
    cyclePopoverAnchor = null;
  }

  function positionCyclePopover() {
    if (!cyclePopover || !cyclePopoverRoot || !cyclePopoverAnchor) {
      return;
    }

    const anchorRect = cyclePopoverAnchor.getBoundingClientRect();
    const popoverRect = cyclePopoverRoot.getBoundingClientRect();
    const margin = 10;
    const gap = 8;
    const viewportWidth = document.documentElement.clientWidth;
    const viewportHeight = document.documentElement.clientHeight;
    const width = popoverRect.width || 280;
    const height = popoverRect.height || 76;

    const left = Math.min(
      Math.max(margin, anchorRect.left + (anchorRect.width - width) / 2),
      Math.max(margin, viewportWidth - width - margin),
    );
    const belowTop = anchorRect.bottom + gap;
    const aboveTop = anchorRect.top - height - gap;
    const placedAbove = belowTop + height > viewportHeight - margin && aboveTop >= margin;
    const top = placedAbove
      ? aboveTop
      : Math.min(Math.max(margin, belowTop), Math.max(margin, viewportHeight - height - margin));
    const pointerLeft = Math.min(
      Math.max(10, anchorRect.left + anchorRect.width / 2 - left - 10),
      Math.max(10, width - 30),
    );

    cyclePopover = { ...cyclePopover, left, top, pointerLeft, placement: placedAbove ? "above" : "below" };
  }

  async function openCyclePopover(channel, step, anchor) {
    cyclePopoverAnchor = anchor;
    cyclePopover = { channel, step, left: 0, top: 0, pointerLeft: 0, placement: "below" };
    await tick();
    positionCyclePopover();
  }

  function onCyclePopoverDocumentPointerDown(event) {
    if (cyclePopoverRoot?.contains(event.target)) {
      return;
    }

    closeCyclePopover();
  }

  function onCyclePopoverKeyDown(event) {
    if (event.key === "Escape" && cyclePopover) {
      event.preventDefault();
      closeCyclePopover();
    }
  }

  function cyclePopoverPointerStyle() {
    if (!cyclePopover) {
      return "";
    }

    const left = `${cyclePopover.pointerLeft ?? 10}px`;
    return cyclePopover.placement === "above"
      ? `left:${left};bottom:-8px;border-top:8px solid var(--cycle-row-divider);`
      : `left:${left};top:-8px;border-bottom:8px solid var(--cycle-row-divider);`;
  }

  function beginCyclePatternGesture() {
    beginEditGestureHistory();
  }

  function applyCyclePattern(cycle, cycleMask) {
    if (!cyclePopoverCell) {
      return false;
    }

    const nextCycle = Math.min(8, Math.max(1, Math.round(cycle)));
    const normalizedMask = normalizeCyclePattern(nextCycle, cycleMask).mask;
    const changed = cyclePopoverCell.cycle !== nextCycle || cyclePopoverCell.cycleMask !== normalizedMask;
    cyclePopoverCell.cycle = nextCycle;
    cyclePopoverCell.cycleMask = normalizedMask;
    return changed;
  }

  function previewCyclePattern(cycle, cycleMask) {
    if (applyCyclePattern(cycle, cycleMask)) {
      void sendCell(session.selectedSource, cyclePopover.channel, cyclePopover.step);
    }
  }

  async function commitCyclePattern(cycle, cycleMask) {
    if (applyCyclePattern(cycle, cycleMask)) {
      await sendCell(session.selectedSource, cyclePopover.channel, cyclePopover.step);
    }
    await commitEditGestureHistory("Edit cycle pattern");
  }

  function scheduleStepValueMenuClose() {
    if (stepValueGesturePointerId !== null) {
      return;
    }

    cancelStepValueMenuClose();
    stepValueMenuCloseTimer = setTimeout(() => {
      stepValueMenuCloseTimer = null;
      if (stepValueGesturePointerId === null) {
        closeStepValueMenu();
      }
    }, 120);
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
    cancelStepValueMenuClose();
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

  function onStepValueMenuPointerMove(event) {
    if (!stepValueMenuOpen || stepValueGesturePointerId !== null) {
      return;
    }

    const target = document.elementFromPoint(event.clientX, event.clientY);
    if (target && stepValueMenuRoot?.contains(target)) {
      cancelStepValueMenuClose();
    } else {
      scheduleStepValueMenuClose();
    }
  }

  function closeStepValueMenuOnFocusOut(event) {
    if (!event.currentTarget.contains(event.relatedTarget)) {
      scheduleStepValueMenuClose();
    }
  }

  function cellStyle(channel, step) {
    const source = session.selectedSource;
    if (source === SILENT_SOURCE) {
      return step >= session.kshState.stepCount
        ? cellStyleFromBackground("var(--color-grid-inactive-step)", "var(--color-text-faint)", "opacity:0.55;")
        : cellStyleFromBackground("var(--color-grid-off-strong)", "var(--color-text-muted)");
    }

    const previewCell = cellSelection.editMode
      ? bulkDragPreviewCells.get(cellSelectionKey(channel, step))
      : null;
    const cell = previewCell ?? session.kshState.sources[source][channel][step];
    const muted = session.kshState.sourceChannelMutes[source][channel];
    const beyondSteps = step >= session.kshState.stepCount;

    if (beyondSteps) {
      return cellStyleFromBackground("var(--color-grid-inactive-step)", "var(--color-text-faint)");
    }

    let lightColor = channelToneColor(channel, "light", session.dcColors);
    let darkColor = channelToneColor(channel, "dark", session.dcColors);
    if (muted) {
      lightColor = mutedChannelColor(lightColor);
      darkColor = mutedChannelColor(darkColor);
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
      return cellStyleFromBackground(
        activeCellBackground(`linear-gradient(to bottom, ${darkColor}, ${lightColor})`),
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

    if (effectiveLayerMode === "probability") {
      const fillPercent = Math.max(0, Math.min(100, Math.round(layerValue)));
      return cellStyleFromBackground(
        `linear-gradient(to top, ${darkColor} 0%, ${darkColor} ${fillPercent}%, var(--color-app) ${fillPercent}%, var(--color-app) 100%)`,
        "var(--color-text)",
        "font-weight:700;text-shadow:0 1px 2px color-mix(in srgb, var(--color-app) 88%, transparent),0 0 1px color-mix(in srgb, var(--color-app) 72%, transparent);",
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
    const flashing = !silent && isEditorFlashing(session.selectedSource, channel, step);
    const previewCell = cellSelection.editMode
      ? bulkDragPreviewCells.get(cellSelectionKey(channel, step))
      : null;
    const active = !silent && !beyondSteps && (previewCell ?? cell).enabled;
    const selected = cellSelection.editMode && selectedCellKeys.has(cellSelectionKey(channel, step));
    const previewTarget = cellSelection.editMode && bulkDragPreviewKeys.has(cellSelectionKey(channel, step));
    const previewCopy = previewTarget && editGesture?.mode === "copy";

    return [
      "ksh-grid-cell relative mr-0 flex overflow-hidden border border-grid-cell-border font-medium leading-none outline-none focus:outline-none focus-visible:outline-none",
      active ? "ksh-grid-cell-active" : "",
      flashing ? "ksh-cell-text-flash" : "",
      "items-center justify-center",
      selected ? "ksh-grid-cell-selected" : "",
      previewCopy
        ? "ksh-grid-cell-preview-copy"
        : previewTarget
          ? "ksh-grid-cell-preview-target"
          : "",
    ].join(" ");
  }

  function cellLabel(channel, step) {
    if (session.selectedSource === SILENT_SOURCE) {
      return "";
    }

    const previewCell = cellSelection.editMode
      ? bulkDragPreviewCells.get(cellSelectionKey(channel, step))
      : null;
    const cell = previewCell ?? session.kshState.sources[session.selectedSource][channel][step];
    if (!cell.enabled) {
      return "";
    }
    if (effectiveLayerMode === "cycle") {
      return "";
    }
    return String(sourceLayerValue(cell, effectiveLayerMode));
  }

  function cyclePatternForCell(cell) {
    return normalizeCyclePattern(cell.cycle, cell.cycleMask);
  }

  function loopBraceStyle(channel) {
    const range = loopRangeForChannel(session.kshState, channel);
    return `left:${range.start * gridCellW}px;width:${range.length * gridCellW}px;height:${gridCellH}px;`;
  }

  function isCellInteractive(channel, step) {
    return session.selectedSource !== SILENT_SOURCE && step < session.kshState.stepCount && !isStepBeyondLoopLength(session.kshState, channel, step);
  }

  function isEditCellInteractive(channel, step) {
    return session.selectedSource !== SILENT_SOURCE && step < session.kshState.stepCount;
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
    closeCyclePopover();

    if (cellSelection.editMode) {
      clearEditSelection();
    }

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
    closeCyclePopover();
    cancelPatternCopy();
    await selectSource(SILENT_SOURCE);
  }

  function onLayerHeadingClick() {
    closeCyclePopover();
    cycleSourceLayerMode();
  }

  function triggerClearBlink(kind, channel = -1) {
    if (kind === "pattern") {
      clearBlink.pattern = true;
      if (patternClearBlinkTimer !== null) {
        clearTimeout(patternClearBlinkTimer);
      }
      patternClearBlinkTimer = setTimeout(() => {
        clearBlink.pattern = false;
        patternClearBlinkTimer = null;
      }, 1000);
      return;
    }

    clearBlink.rowChannel = channel;
    if (rowClearBlinkTimer !== null) {
      clearTimeout(rowClearBlinkTimer);
    }
    rowClearBlinkTimer = setTimeout(() => {
      clearBlink.rowChannel = -1;
      rowClearBlinkTimer = null;
    }, 1000);
  }

  function headerHistoryLabel(id) {
    switch (id) {
      case "steps":
        return "Change steps";
      case "refresh":
        return "Change refresh rate";
      case "swing":
        return "Change swing";
      case "swing_subdivision":
        return "Change swing subdivision";
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
    if (!(cellSelection.editMode ? isEditCellInteractive(channel, step) : isCellInteractive(channel, step))) {
      return;
    }

    if (cellSelection.editMode) {
      event.preventDefault();
      event.stopPropagation();

      if (event.shiftKey) {
        toggleSelectedCell(channel, step);
      } else if (selectedCellKeys.has(cellSelectionKey(channel, step))) {
        beginBulkDrag(event, channel, step);
      } else {
        beginMarquee(event);
      }
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
      element: event.currentTarget,
    };
    setSelectedCell(channel, step);
    event.currentTarget.setPointerCapture(event.pointerId);
  }

  async function onCellPointerMove(event) {
    if (cellSelection.editMode) {
      return;
    }

    if (!cellDrag) {
      return;
    }
    if ((event.buttons & 1) === 0) {
      await onCellPointerUp();
      return;
    }

    if (normalizeSourceLayerMode(cellDrag.layerMode) === "cycle") {
      const distance = Math.hypot(
        event.clientX - cellDrag.startX,
        event.clientY - cellDrag.startY,
      );
      if (distance >= 5 && !cellDrag.moved) {
        cellDrag = { ...cellDrag, moved: true };
      }
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

  async function onCellPointerUp(event, cancelled = false) {
    if (cellSelection.editMode) {
      return;
    }

    if (!cellDrag) {
      return;
    }

    const drag = cellDrag;
    cellDrag = null;

    if (cancelled) {
      cancelEditGestureHistory();
      return;
    }

    if (!drag.moved && normalizeSourceLayerMode(drag.layerMode) === "cycle") {
      const cell = session.kshState.sources[drag.source][drag.channel][drag.step];
      if (!cell.enabled) {
        cell.enabled = 1;
        await sendCell(drag.source, drag.channel, drag.step);
        await commitEditGestureHistory("Enable cell");
      } else {
        cancelEditGestureHistory();
      }

      await openCyclePopover(drag.channel, drag.step, drag.element ?? event?.currentTarget);
      return;
    }

    if (normalizeSourceLayerMode(drag.layerMode) === "cycle") {
      cancelEditGestureHistory();
      return;
    }

    if (!drag.moved) {
      toggleCellOnRelease(session.kshState, drag.source, drag);
      await sendCell(drag.source, drag.channel, drag.step);
    }

    await commitEditGestureHistory("Edit cell");
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
    if (shiftHeld && !shiftKey) {
      closeCyclePopover();
    }
    shiftHeld = Boolean(shiftKey);
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
      cancelEditGesture();
      cancelEditGestureHistory();
    } else if (event.key === "1") {
      closeCyclePopover();
      setSourceLayerMode("velocity");
    } else if (event.key === "2") {
      closeCyclePopover();
      setSourceLayerMode("cycle");
    } else if (event.key === "3") {
      closeCyclePopover();
      setSourceLayerMode("probability");
    } else if (event.key === "4") {
      closeCyclePopover();
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
      shiftHeld = false;
      closeCyclePopover();
      recordPadKeysHeld.clear();
    };

    window.addEventListener("keydown", onKeyDown);
    window.addEventListener("keyup", onKeyUp);
    window.addEventListener("blur", onBlur);
    window.addEventListener("resize", positionCyclePopover);
    window.addEventListener("scroll", positionCyclePopover, true);
    document.addEventListener("pointerdown", onCyclePopoverDocumentPointerDown, true);
    document.addEventListener("keydown", onCyclePopoverKeyDown, true);
    document.addEventListener("pointermove", onStepValueMenuPointerMove, true);
    return () => {
      removeModifierListener();
      window.removeEventListener("keydown", onKeyDown);
      window.removeEventListener("keyup", onKeyUp);
      window.removeEventListener("blur", onBlur);
      window.removeEventListener("resize", positionCyclePopover);
      window.removeEventListener("scroll", positionCyclePopover, true);
      document.removeEventListener("pointerdown", onCyclePopoverDocumentPointerDown, true);
      document.removeEventListener("keydown", onCyclePopoverKeyDown, true);
      document.removeEventListener("pointermove", onStepValueMenuPointerMove, true);
      cancelStepValueMenuClose();
      removeStepValueGestureListeners();
      if (patternClearBlinkTimer !== null) {
        clearTimeout(patternClearBlinkTimer);
      }
      if (rowClearBlinkTimer !== null) {
        clearTimeout(rowClearBlinkTimer);
      }
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
        brightLabel
        active={headerDrag?.id === "swing"}
        onBegin={beginHeaderDrag}
        onMove={moveHeaderDrag}
        onEnd={endHeaderDrag}
      />
      <HeaderDiscreteSelect
        id="swing_subdivision"
        label="Sub"
        ariaLabel="Swing subdivision"
        value={session.kshState.swingSubdivisionIndex}
        options={SWING_SUBDIVISION_OPTIONS}
        resetValue={DEFAULT_SWING_SUBDIVISION_INDEX}
        active={headerDrag?.id === "swing_subdivision"}
        onBegin={beginHeaderDrag}
        onEnd={endHeaderDrag}
        onValueChange={(value) => setHeaderValue("swing_subdivision", value)}
      />
      <HeaderValueDrag
        id="velocity_humanize"
        label="Vel %"
        value={session.kshState.velocityHumanize}
        brightLabel
        active={headerDrag?.id === "velocity_humanize"}
        onBegin={beginHeaderDrag}
        onMove={moveHeaderDrag}
        onEnd={endHeaderDrag}
      />
      <HeaderValueDrag
        id="timing_humanize"
        label="Time %"
        value={session.kshState.timingHumanize}
        brightLabel
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
        onclick={onLayerHeadingClick}
      >
        {patternHeading}: {layerHeading}
      </button>
    </div>

    <div class="header-section h-full items-center">
      <div class="flex items-center gap-2">
        <button
          type="button"
          class={`header-button h-[41px] min-w-[58px] border px-3 text-[12px] tracking-[0.12em] ${cellSelection.editMode ? "border-accent bg-accent/15 text-accent" : "border-border-subtle bg-control-secondary text-text-muted"}`}
          aria-label={cellSelection.editMode ? "Exit edit selection mode" : "Enter edit selection mode"}
          aria-pressed={cellSelection.editMode}
          title={cellSelection.editMode ? "Exit EDIT mode" : "Select cells · Option-drag to copy"}
          onclick={toggleEditMode}
        >
          <MoveAroundIcon />
        </button>
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
          class={`header-icon-button ${clearBlink.pattern ? "ksh-clear-blink" : ""}`}
          disabled={session.selectedSource === SILENT_SOURCE}
          aria-label="Clear pattern"
          title="Double-click to clear this pattern"
          onclick={() => triggerClearBlink("pattern")}
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
          bind:this={stepValueMenuRoot}
          role="group"
          aria-label="Step value selector"
          onfocusout={closeStepValueMenuOnFocusOut}
        >
          <button
            type="button"
            class="header-button flex h-[42px] w-[220px] shrink-0 items-center justify-between gap-3 border border-border-subtle bg-transparent px-3.5 text-[16px] font-semibold text-text"
            aria-haspopup="listbox"
            aria-expanded={stepValueMenuOpen}
            onpointerdown={beginStepValueGesture}
          >
            <span class="flex min-w-0 items-center gap-2">
              <span class="w-9 shrink-0 text-left text-[21px] leading-none text-accent-strong">{selectedStepValueOption.mark}</span>
              <span class="whitespace-nowrap leading-tight">{selectedStepValueOption.shortLabel}</span>
            </span>
            <svg
              class="h-3 w-3 shrink-0 text-text-muted"
              viewBox="0 0 12 8"
              fill="currentColor"
              aria-hidden="true"
            >
              <path d="M1 1.25 6 6.25 11 1.25Z" />
            </svg>
          </button>
          {#if stepValueMenuOpen}
            <div
              class="absolute left-0 top-full z-30 mt-1 w-[220px] overflow-hidden border border-border-strong bg-app shadow-[0_14px_34px_rgba(0,0,0,0.36)]"
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

  <div
    class="relative bg-grid-bg flex min-h-0 flex-1 flex-col overflow-hidden px-3"
    role="presentation"
    onpointerdown={onGridPointerDown}
  >
    {#if cellSelection.editMode && selectedCellCount > 0}
      <div class="pointer-events-none absolute left-3 top-2 z-30 rounded-sm border border-accent/40 bg-app/75 px-2 py-1 text-[10px] font-bold uppercase tracking-[0.12em] text-accent">
        {selectedCellCount} selected{editGesture?.kind === "drag" && editGesture.didMove ? ` · ${bulkDragLabel(
          selectedCellLocations(selectedCellKeys, session.kshState.channelCount, session.kshState.stepCount),
          { channel: editGesture.anchorChannel, step: editGesture.anchorStep },
          { channel: editGesture.currentChannel, step: editGesture.currentStep },
          session.kshState.channelCount,
          session.kshState.stepCount,
          editGesture.mode,
        )}` : " · shift-click add/remove"}
      </div>
    {/if}
    <div class="shrink-0" style={`height:${gridTopPad}px`} aria-hidden="true"></div>
    <div class="shrink-0">
    <div
      class="flex items-center"
      style={`padding-left:${GRID_SIDEBAR_W + GRID_ROW_CELL_LEFT_GAP + gridOffsetX}px;margin-top:${stepLabelMargin}px;margin-bottom:${STEP_LABEL_CELL_GAP}px`}
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
      {@const rowLoopRange = loopRangeForChannel(session.kshState, channel)}
      <div
        class="flex items-center"
        style={`padding-top:${gridRowPadY}px;padding-bottom:${gridRowPadY}px;`}
        data-channel-row={channel}
      >
        <div class="flex shrink-0 items-center pr-1 font-medium" style={`width:${GRID_SIDEBAR_W}px`}>
          <button
            type="button"
            class={`row-clear-button mr-[30px] ${clearBlink.rowChannel === channel ? "ksh-clear-blink" : ""}`}
            disabled={session.selectedSource === SILENT_SOURCE}
            aria-label="Clear channel row steps"
            title="Double-click to clear this row"
            onclick={() => triggerClearBlink("row", channel)}
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
              class="channel-label flex shrink-0 items-center truncate text-left font-semibold text-accent"
              style={`width:${CHANNEL_LABEL_W}px;height:${gridCellH}px;font-size:${channelLabelFontPx}px;line-height:1;`}
              onclick={() => onLabelClick(channel)}
            >
              {session.kshState.channels[channel]?.label ?? channel + 1}
            </button>
          {/if}
          <div class="ml-5 flex min-w-0 flex-1 items-center justify-between">
            <ChannelNoteControl {channel} />
            <PlaybackModeSelect
              distributed
              value={session.kshState.channels[channel]?.playbackMode ?? "normal"}
              onChange={(mode) => setChannelPlaybackMode(channel, mode)}
            />
            <button
              type="button"
              class={`channel-power-toggle flex h-[26px] w-[26px] shrink-0 items-center justify-center border-0 bg-transparent p-0 outline-none focus-visible:ring-1 focus-visible:ring-focus-ring ${session.selectedSource === SILENT_SOURCE ? "opacity-35" : session.kshState.sourceChannelMutes[session.selectedSource][channel] ? "text-text-faint" : "text-accent"}`}
              aria-label={session.selectedSource !== SILENT_SOURCE && session.kshState.sourceChannelMutes[session.selectedSource][channel] ? "Turn channel on" : "Turn channel off"}
              aria-pressed={session.selectedSource !== SILENT_SOURCE && session.kshState.sourceChannelMutes[session.selectedSource][channel] ? "false" : "true"}
              disabled={session.selectedSource === SILENT_SOURCE}
              title="Shift-click to solo channel"
              onpointerdown={(event) => onMutePointerDown(channel, event)}
              onpointermove={onMutePointerMove}
              onpointerup={endMuteDrag}
              onpointercancel={endMuteDrag}
            >
              <RowDisableIcon class="channel-power-toggle-icon h-[21px] w-[21px]" />
            </button>
          </div>
        </div>

        <div class="relative flex" style={`margin-left:${GRID_ROW_CELL_LEFT_GAP + gridOffsetX}px`}>
          <div
            class="loop-range-brace pointer-events-none absolute top-0 z-10"
            style={loopBraceStyle(channel)}
            aria-hidden="true"
          ></div>
          <button
            type="button"
            class="loop-range-handle loop-range-handle-start absolute top-0 z-20 flex -translate-x-full items-center justify-center outline-none"
            style={`left:${loopRangeForChannel(session.kshState, channel).start * gridCellW}px;width:${loopHandleW}px;height:${gridCellH}px;`}
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
          </button>
          <button
            type="button"
            class="loop-range-handle loop-range-handle-end absolute top-0 z-20 flex items-center justify-center outline-none"
            style={`left:${(loopRangeForChannel(session.kshState, channel).end + 1) * gridCellW}px;width:${loopHandleW}px;height:${gridCellH}px;`}
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
          </button>
          {#each allStepCols as step (step)}
            {#if step >= rowLoopRange.start && step <= rowLoopRange.end}
            <button
              type="button"
              class={cellClass(channel, step)}
              style={`width:${gridCellW}px;height:${gridCellH}px;font-size:${cellFontPx}px;${cellStyle(channel, step)}`}
              data-ksh-edit-cell
              data-channel={channel}
              data-step={step}
              data-selected={cellSelection.editMode && selectedCellKeys.has(cellSelectionKey(channel, step)) ? "true" : undefined}
              disabled={cellSelection.editMode ? !isEditCellInteractive(channel, step) : !isCellInteractive(channel, step)}
              onpointerdown={(event) => onCellPointerDown(event, channel, step)}
              onpointermove={onCellPointerMove}
              onpointerup={onCellPointerUp}
              onpointercancel={(event) => onCellPointerUp(event, true)}
            >
              {#if session.selectedSource !== SILENT_SOURCE && effectiveLayerMode === "cycle" && (cellSelection.editMode ? isEditCellInteractive(channel, step) : isCellInteractive(channel, step)) && session.kshState.sources[session.selectedSource][channel][step].enabled}
                {@const cyclePattern = cyclePatternForCell(session.kshState.sources[session.selectedSource][channel][step])}
                <div
                  class="pointer-events-none absolute grid overflow-hidden"
                  style={`left:${Math.max(2, Math.round(cellInsetPx * 0.5))}px;right:${Math.max(2, Math.round(cellInsetPx * 0.5))}px;bottom:${cellInsetPx}px;height:25%;grid-template-columns:repeat(${cyclePattern.cycle},minmax(0,1fr));gap:1px;`}
                  aria-label={`Cycle pattern: ${cyclePattern.cycle} steps`}
                >
                  {#each Array.from({ length: cyclePattern.cycle }, (_, cycleIndex) => cycleIndex) as cycleIndex (cycleIndex)}
                    <span class={isCyclePositionActive(cyclePattern.cycle, cyclePattern.mask, cycleIndex) ? "bg-text-inverse" : "bg-app/60"}></span>
                  {/each}
                </div>
              {:else}
                {cellLabel(channel, step)}
              {/if}
            </button>
            {:else if step < rowLoopRange.start}
              <div
                class="shrink-0"
                style={`width:${gridCellW}px;height:${gridCellH}px;`}
                aria-hidden="true"
              ></div>
            {/if}
          {/each}
          <div
            class="pointer-events-auto absolute top-0 z-20 flex items-center"
            style={`left:${nudgeLaneLeft}px;width:${GRID_NUDGE_LANE_W}px;height:${gridCellH}px;`}
            role="group"
            aria-label="Shift channel pattern"
          >
            <button
              type="button"
              class="flex shrink-0 items-center justify-center p-0 text-accent outline-none focus-visible:ring-1 focus-visible:ring-focus-ring"
              style={`width:${GRID_NUDGE_BUTTON_W}px;height:26px;`}
              disabled={session.selectedSource === SILENT_SOURCE}
              aria-label="Shift channel pattern left"
              onclick={() => shiftChannelRow(channel, -1)}
            >
              <NudgeTriangleIcon reversed />
            </button>
            <button
              type="button"
              class="flex shrink-0 items-center justify-center p-0 text-accent outline-none focus-visible:ring-1 focus-visible:ring-focus-ring"
              style={`width:${GRID_NUDGE_BUTTON_W}px;height:26px;`}
              disabled={session.selectedSource === SILENT_SOURCE}
              aria-label="Shift channel pattern right"
              onclick={() => shiftChannelRow(channel, 1)}
            >
              <NudgeTriangleIcon />
            </button>
          </div>
        </div>
      </div>
    {/each}
    </div>
    <div class="shrink-0" style={`height:${gridBottomPad}px`} aria-hidden="true"></div>
    {#if editGesture?.kind === "marquee"}
      <div
        class="pointer-events-none fixed z-[9999] rounded-sm border border-accent/70 bg-accent/10 shadow-[0_0_0_1px_color-mix(in_srgb,var(--color-accent)_18%,transparent),0_0_16px_color-mix(in_srgb,var(--color-accent)_18%,transparent)]"
        style={marqueeRectStyle}
        aria-hidden="true"
      ></div>
    {/if}
  </div>

  {#if cyclePopover && effectiveLayerMode === "cycle" && cyclePopoverCell}
    <div
      bind:this={cyclePopoverRoot}
      class="fixed z-[80] w-[280px] rounded-md p-2 text-text backdrop-blur-sm"
      style={`left:${cyclePopover.left}px;top:${cyclePopover.top}px;${cyclePopoverThemeStyle}`}
      role="dialog"
      tabindex="-1"
      aria-label={`Cycle editor for channel ${cyclePopover.channel + 1}, step ${cyclePopover.step + 1}`}
      onpointerdown={(event) => event.stopPropagation()}
    >
      <div
        class="pointer-events-none absolute h-0 w-0 border-x-[10px] border-x-transparent"
        style={cyclePopoverPointerStyle()}
        aria-hidden="true"
      ></div>
      <div class="mb-1 flex items-center justify-between px-1 text-[12px] font-semibold leading-none" style="color:var(--cycle-row-light);">
        <span>Step {cyclePopover.step + 1}</span>
        <button
          type="button"
          class="cycle-popover-focus flex h-4 w-4 items-center justify-center rounded-sm text-[14px] leading-none outline-none hover:text-text"
          style="color:var(--cycle-row-light);"
          aria-label="Close cycle editor"
          onclick={closeCyclePopover}
        >
          ×
        </button>
      </div>
      <KshCyclePatternEditor
        cycle={cyclePopoverCell.cycle}
        cycleMask={cyclePopoverCell.cycleMask}
        onGestureStart={beginCyclePatternGesture}
        onPatternPreview={previewCyclePattern}
        onPatternCommit={commitCyclePattern}
      />
    </div>
  {/if}

</div>
