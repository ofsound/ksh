import {
  DEFAULT_CHANNEL_LABELS,
  MAX_CHANNELS,
  MAX_STEPS,
  NOTE_HIT_FLASH_MS,
  SILENT_SOURCE,
  SOURCE_COUNT,
  clamp,
  normalizePlaybackMode,
  normalizeRate,
} from "./kshConstants.js";
import {
  clampHeaderValue,
} from "./kshEditorInteractions.js";
import {
  clearSourcePattern,
  combinedDimensions,
  copySourcePattern,
  cycleGenerationMode,
  cycleLayerMode,
  cycleRate,
  loopRangeForChannel,
  normalizePatternViewScale,
  nextPlaybackMode,
  shiftSourceChannelRow,
} from "./kshEditorUtils.js";
import {
  applyRecomposedPreview,
  stackSourceForPreview,
} from "./kshPreviewUtils.js";
import {
  onBackendEvent,
  cycleProject as nativeCycleProject,
  getProjectState,
  initialisationValue,
  loadProject as nativeLoadProject,
  newProject as nativeNewProject,
  parseBackendJson,
  saveProject as nativeSaveProject,
  sendCommand,
  setEditorScaleMinimum,
  setProjectThemeMode as nativeSetProjectThemeMode,
  setProjectUiScalePercent as nativeSetProjectUiScalePercent,
  setViewSize,
  syncAll,
  triggerRow,
  waitForBackend,
} from "./kshBridge.js";
import {
  applyThemeMode,
  defaultThemeMode,
  storedThemeMode,
} from "./themeMode.js";
import {
  currentUiScaleMinimumSize,
  resolveInitialUiScalePercent,
  setUiScalePercent,
  setUiViewportSize,
  uiScaleState,
} from "./uiScale.svelte.js";
import {
  applyEngineState,
  applyPersistencePayload,
  applyStatusMessage,
  cloneCell,
  defaultCell,
  makeDefaultKshState,
  resizeChannelLoopLengths,
  serializePersistenceState,
} from "./kshUiState.js";

const historyLimit = 100;
export const undoStack = $state([]);
export const redoStack = $state([]);
/** @type {ReturnType<typeof createHistorySnapshot> | null} */
let gestureHistoryBefore = null;
let historyApplying = false;

export const session = $state({
  kshState: makeDefaultKshState(),
  previewData: null,
  compactNoteFlashes: {},
  editorNoteFlashes: {},
  playingStep: 0,
  selectedSource: 0,
  selectedChannel: 0,
  selectedStep: 0,
  dcColors: 1,
  patternViewScale: 1,
  channelNoteDisplayNames: true,
  patternRecordingEnabled: 0,
  sourceLayerMode: "velocity",
  patternCopySource: -1,
  sourceChannelSoloSource: -1,
  sourceChannelSoloChannel: -1,
  sourceChannelSoloRestoreMutes: null,
  projectName: "Untitled Project",
  projectDescription: "",
  projectCreatedAt: "",
  projectModifiedAt: "",
  projectFileName: "",
  themeMode: defaultThemeMode,
  hasPreviousProject: false,
  hasNextProject: false,
  projectOperationBusy: false,
  projectOperationError: "",
  bridgeError: "",
  ready: false,
});
let sessionStarted = false;
/** @type {Array<() => void>} */
let teardownHandlers = [];
let previewSuppressionDepth = 0;
let patternRecordHistoryBefore = null;
let lastStackPreviewSource = null;

function beginPreviewSuppression() {
  previewSuppressionDepth += 1;
}

function endPreviewSuppression() {
  previewSuppressionDepth = Math.max(0, previewSuppressionDepth - 1);
}

function bumpOptimisticPreview() {
  session.previewData = applyRecomposedPreview(session.kshState, session.previewData);
}

async function withPreviewSuppressed(task) {
  beginPreviewSuppression();

  try {
    await task();
  } finally {
    endPreviewSuppression();
  }
}

function bumpState() {
  session.kshState = {
    ...session.kshState,
    channels: session.kshState.channels.map((channel) => ({ ...channel })),
    sources: session.kshState.sources.map((source) =>
      source.map((channel) => channel.map((cell) => ({ ...cell })))
    ),
    sourceSettings: session.kshState.sourceSettings.map((settings) => ({ ...settings })),
    sourceChannelMutes: session.kshState.sourceChannelMutes.map((row) => [...row]),
  };
}

function syncActiveChannelLoopRanges() {
  const source = session.kshState.staticSource;
  if (source < 0 || source >= SOURCE_COUNT) {
    return;
  }

  const settings = session.kshState.sourceSettings[source];
  for (let channel = 0; channel < MAX_CHANNELS; channel += 1) {
    const range = settings.loopRanges[channel];
    session.kshState.channels[channel].loopStart = range.loopStart;
    session.kshState.channels[channel].loopLength = range.loopLength;
  }
}

function createHistorySnapshot() {
  return serializePersistenceState(session.kshState);
}

function cloneHistorySnapshot(snapshot) {
  return JSON.parse(JSON.stringify(snapshot));
}

function snapshotsEqual(left, right) {
  return JSON.stringify(left) === JSON.stringify(right);
}

function assignHistorySnapshot(snapshot) {
  applyPersistencePayload(session.kshState, snapshot);
  session.selectedSource = session.kshState.staticSource;
  clearSourceChannelSolo();
  bumpState();
  bumpOptimisticPreview();
}

function nativeScalar(state, key, fallback) {
  if (state == null || state[key] === undefined || state[key] === null) {
    return fallback;
  }

  return state[key];
}

function assignProjectMetadata(state) {
  if (state == null) {
    return;
  }

  session.projectName = String(nativeScalar(state, "projectName", "Untitled Project"));
  session.projectDescription = String(nativeScalar(state, "projectDescription", ""));
  session.projectCreatedAt = String(nativeScalar(state, "projectCreatedAt", ""));
  session.projectModifiedAt = String(nativeScalar(state, "projectModifiedAt", ""));
  session.projectFileName = String(nativeScalar(state, "projectFileName", ""));
  if (state.projectThemeMode !== undefined) {
    session.themeMode = applyThemeMode(state.projectThemeMode, { persist: false });
  }
  session.hasPreviousProject = Boolean(Number.parseInt(String(nativeScalar(state, "hasPreviousProject", 0)), 10));
  session.hasNextProject = Boolean(Number.parseInt(String(nativeScalar(state, "hasNextProject", 0)), 10));
}

export async function syncEditorScaleMinimumToNative() {
  const minimumSize = currentUiScaleMinimumSize({
    standaloneTransportAvailable: session.kshState.standaloneTransportAvailable,
  });
  await setEditorScaleMinimum(minimumSize.widthPx, minimumSize.heightPx);
}

async function syncProjectUiScaleToNative() {
  await nativeSetProjectUiScalePercent(uiScaleState.percent);
}

export async function setExplicitUiScalePercent(next) {
  setUiScalePercent(next);
  await syncEditorScaleMinimumToNative();
  await syncProjectUiScaleToNative();
  await resizeForCurrentView();
}

async function applyProjectUiScalePercent(next) {
  setUiScalePercent(next, { persist: false });
  await syncEditorScaleMinimumToNative();
  await resizeForCurrentView();
}

export function initializeUiScaleFromNative() {
  const initialProjectScale = initialisationValue("projectUiScalePercent");
  const projectScaleScalar =
    initialProjectScale === null
      ? null
      : Array.isArray(initialProjectScale)
        ? initialProjectScale[0]
        : initialProjectScale;
  setUiScalePercent(resolveInitialUiScalePercent(projectScaleScalar), { persist: false });
}

export function initializeThemeModeFromNative() {
  const initialTheme = initialisationValue("projectThemeMode");
  session.themeMode = applyThemeMode(initialTheme === null ? storedThemeMode() : initialTheme, {
    persist: false,
  });
}

export async function setThemeMode(next) {
  session.themeMode = applyThemeMode(next);
  await nativeSetProjectThemeMode(session.themeMode);
}

export function setUiScaleViewportSize(widthPx, heightPx) {
  setUiViewportSize({
    widthPx,
    heightPx,
    standaloneTransportAvailable: session.kshState.standaloneTransportAvailable,
  });
}

export function clearEditHistory() {
  undoStack.length = 0;
  redoStack.length = 0;
  gestureHistoryBefore = null;
}

function pushHistoryEntry(label, before, after) {
  if (historyApplying || snapshotsEqual(before, after)) {
    return;
  }

  undoStack.push({
    label,
    before: cloneHistorySnapshot(before),
    after: cloneHistorySnapshot(after),
  });

  if (undoStack.length > historyLimit) {
    undoStack.splice(0, undoStack.length - historyLimit);
  }

  redoStack.length = 0;
}

export async function commitEditHistory(label, mutation) {
  const before = createHistorySnapshot();
  await mutation();
  pushHistoryEntry(label, before, createHistorySnapshot());
}

export function beginEditGestureHistory() {
  if (!gestureHistoryBefore && !historyApplying) {
    gestureHistoryBefore = createHistorySnapshot();
  }
}

export function cancelEditGestureHistory() {
  gestureHistoryBefore = null;
}

export async function commitEditGestureHistory(label) {
  const before = gestureHistoryBefore;
  gestureHistoryBefore = null;

  if (!before || historyApplying) {
    return;
  }

  pushHistoryEntry(label, before, createHistorySnapshot());
}

async function applyHistorySnapshot(snapshot) {
  historyApplying = true;

  try {
    const previousStepCount = session.kshState.stepCount;
    assignHistorySnapshot(snapshot);
    await sendCommand("apply_persistence", [snapshot]);

    if (session.kshState.stepCount !== previousStepCount) {
      await resizeForCurrentView();
    }
  } finally {
    historyApplying = false;
  }
}

export async function undoEdit() {
  const entry = undoStack[undoStack.length - 1];
  if (!entry) {
    return;
  }

  undoStack.pop();
  redoStack.push(entry);
  await applyHistorySnapshot(entry.before);
}

export async function redoEdit() {
  const entry = redoStack[redoStack.length - 1];
  if (!entry) {
    return;
  }

  redoStack.pop();
  undoStack.push(entry);
  await applyHistorySnapshot(entry.after);
}

function compactFlashKey(channel, step) {
  return `${channel}:${step}`;
}

function editorFlashKey(source, channel, step) {
  return `${source}:${channel}:${step}`;
}

function scheduleFlashClear(key, store, delayMs) {
  window.setTimeout(() => {
    if (store === "compact") {
      if (session.compactNoteFlashes[key] !== undefined) {
        const next = { ...session.compactNoteFlashes };
        delete next[key];
        session.compactNoteFlashes = next;
      }
      return;
    }

    const [source, channel, step] = key.split(":").map(Number);
    const sourceRow = session.editorNoteFlashes[source];
    if (!sourceRow?.[channel]?.[step]) {
      return;
    }

    const next = { ...session.editorNoteFlashes };
    next[source] = { ...next[source] };
    next[source][channel] = { ...next[source][channel] };
    delete next[source][channel][step];
    session.editorNoteFlashes = next;
  }, delayMs + 5);
}

function handleCompactNoteHit(payload) {
  const channel = clamp(Number(payload?.channel ?? 0), 1, MAX_CHANNELS) - 1;
  const step = clamp(Number(payload?.generatedStep ?? 0), 1, MAX_STEPS) - 1;
  const key = compactFlashKey(channel, step);
  session.compactNoteFlashes = {
    ...session.compactNoteFlashes,
    [key]: Date.now() + NOTE_HIT_FLASH_MS,
  };
  scheduleFlashClear(key, "compact", NOTE_HIT_FLASH_MS);
}

function handleEditorNoteHit(payload) {
  const source = clamp(Number(payload?.source ?? 0), 1, SOURCE_COUNT) - 1;
  const channel = clamp(Number(payload?.channel ?? 0), 1, MAX_CHANNELS) - 1;
  const step = clamp(Number(payload?.sourceStep ?? 0), 1, MAX_STEPS) - 1;
  const key = editorFlashKey(source, channel, step);
  const until = Date.now() + NOTE_HIT_FLASH_MS;

  session.editorNoteFlashes = {
    ...session.editorNoteFlashes,
    [source]: {
      ...(session.editorNoteFlashes[source] ?? {}),
      [channel]: {
        ...((session.editorNoteFlashes[source] ?? {})[channel] ?? {}),
        [step]: until,
      },
    },
  };
  scheduleFlashClear(key, "editor", NOTE_HIT_FLASH_MS);
}

function handleNoteHit(payload) {
  // Both the editor grid and the compact preview strip are always visible, so
  // flash both simultaneously.
  handleEditorNoteHit(payload);
  handleCompactNoteHit(payload);
}

export function isCompactFlashing(channel, step) {
  const until = session.compactNoteFlashes[compactFlashKey(channel, step)];
  return until !== undefined && until > Date.now();
}

export function isEditorFlashing(source, channel, step) {
  const until = session.editorNoteFlashes[source]?.[channel]?.[step];
  return until !== undefined && until > Date.now();
}

function cellCommandArgs(source, channel, step) {
  const cell = session.kshState.sources[source][channel][step];
  return [
    source + 1,
    channel + 1,
    step + 1,
    cell.enabled,
    cell.velocity,
    cell.probability,
    cell.cycle,
    cell.cycleOffset,
    cell.cycleInverted,
    cell.roll,
  ];
}

function sendCellCommand(source, channel, step) {
  return sendCommand("cell", cellCommandArgs(source, channel, step));
}

export async function sendCell(source, channel, step) {
  bumpState();
  await sendCellCommand(source, channel, step);
}

export async function sendChannel(channel) {
  const row = session.kshState.channels[channel];
  await sendCommand("channel_label", [channel + 1, row.label]);
  await sendCommand("channel_note", [channel + 1, row.note]);
  if (row.lock < 0) {
    await sendCommand("channel_lock", [channel + 1, "random"]);
  } else {
    await sendCommand("channel_lock", [channel + 1, row.lock + 1]);
  }
}

export async function sendChannelPlaybackMode(channel) {
  await sendCommand("channel_playback_mode", [channel + 1, session.kshState.channels[channel].playbackMode]);
}

export async function sendSourceChannelMute(source, channel) {
  await sendCommand("source_channel_mute", [
    source + 1,
    channel + 1,
    session.kshState.sourceChannelMutes[source][channel],
  ]);
}

export function setSelectedChannel(channel) {
  setSelectedCell(channel, session.selectedStep);
}

export function setSelectedCell(channel, step) {
  session.selectedChannel = clamp(channel, 0, MAX_CHANNELS - 1);
  const range = loopRangeForChannel(session.kshState, session.selectedChannel);
  session.selectedStep = clamp(step, range.start, range.end);
}

export async function selectSource(source) {
  const previousStepCount = session.kshState.stepCount;
  session.selectedSource = clamp(source, SILENT_SOURCE, SOURCE_COUNT - 1);
  session.kshState.staticSource = session.selectedSource;
  if (session.selectedSource !== SILENT_SOURCE) {
    const settings = session.kshState.sourceSettings[session.selectedSource];
    session.kshState.stepCount = settings.stepCount;
    session.kshState.rate = settings.rate;
    session.kshState.refreshSteps = clamp(session.kshState.refreshSteps, 1, session.kshState.stepCount);
    syncActiveChannelLoopRanges();
    session.selectedStep = clamp(
      session.selectedStep,
      loopRangeForChannel(session.kshState, session.selectedChannel).start,
      loopRangeForChannel(session.kshState, session.selectedChannel).end
    );
  }
  bumpState();
  await sendCommand("static_source", [session.selectedSource === SILENT_SOURCE ? "M" : session.selectedSource + 1]);
  if (session.kshState.stepCount !== previousStepCount) {
    await resizeForCurrentView();
  }

  if (session.patternRecordingEnabled && session.selectedSource !== SILENT_SOURCE) {
    await sendCommand("pattern_record_enabled", [1, session.selectedSource + 1]);
  } else if (session.patternRecordingEnabled) {
    await setPatternRecordingEnabled(false);
  }
}

export function beginPatternCopy(source) {
  if (source === SILENT_SOURCE) {
    return;
  }
  session.patternCopySource = clamp(source, 0, SOURCE_COUNT - 1);
}

export function cancelPatternCopy() {
  session.patternCopySource = -1;
}

export async function copyPatternToSource(destination) {
  const source = session.patternCopySource;
  destination = clamp(destination, 0, SOURCE_COUNT - 1);

  if (source < 0 || source === destination) {
    return;
  }

  await commitEditHistory("Copy pattern", async () => {
    const previousStepCount = session.kshState.stepCount;
    if (!copySourcePattern(session.kshState, source, destination)) {
      return;
    }

    session.patternCopySource = -1;
    session.selectedSource = destination;
    session.kshState.staticSource = destination;
    session.kshState.stepCount = session.kshState.sourceSettings[destination].stepCount;
    session.kshState.rate = session.kshState.sourceSettings[destination].rate;
    session.kshState.refreshSteps = clamp(session.kshState.refreshSteps, 1, session.kshState.stepCount);
    syncActiveChannelLoopRanges();
    bumpState();
    bumpOptimisticPreview();

    await sendCommand("source_pattern_copy", [source + 1, destination + 1]);
    await sendCommand("static_source", [destination + 1]);
    if (session.kshState.stepCount !== previousStepCount) {
      await resizeForCurrentView();
    }
  });
}

export async function sendChannelLoopLength(channel) {
  const row = session.kshState.channels[channel];
  await sendCommand("channel_loop_length", [
    channel + 1,
    row.loopLength,
    (row.loopStart ?? 0) + 1,
  ]);
}

export async function setHeaderValue(id, value) {
  const next = clampHeaderValue(session.kshState, id, value);
  const state = session.kshState;

  if (id === "steps" && state.stepCount !== next) {
    if (session.selectedSource === SILENT_SOURCE) {
      return;
    }
    const previousStepCount = state.stepCount;
    state.stepCount = next;
    state.sourceSettings[session.selectedSource].stepCount = next;
    state.refreshSteps = clamp(state.refreshSteps, 1, next);
    resizeChannelLoopLengths(state, next, previousStepCount);
    bumpState();
    await sendCommand("source_steps", [session.selectedSource + 1, next]);
    await sendCommand("refresh_steps", [state.refreshSteps]);
    await resizeForCurrentView();
    return;
  }

  if (id === "refresh" && state.refreshSteps !== next) {
    state.refreshSteps = next;
    bumpState();
    await sendCommand("refresh_steps", [next]);
    return;
  }

  if (id === "swing" && state.swing !== next) {
    state.swing = next;
    bumpState();
    await sendCommand("swing", [next]);
    return;
  }

  if (id === "velocity_humanize" && state.velocityHumanize !== next) {
    state.velocityHumanize = next;
    bumpState();
    await sendCommand("velocity_humanize", [next]);
    return;
  }

  if (id === "timing_humanize" && state.timingHumanize !== next) {
    state.timingHumanize = next;
    bumpState();
    await sendCommand("timing_humanize", [next]);
  }
}

export async function setRowLoopLength(channel, value) {
  const start = session.kshState.channels[channel].loopStart ?? 0;
  await setRowLoopRange(channel, start, value);
}

export async function setRowLoopRange(channel, start, length) {
  const nextStart = clamp(start, 0, session.kshState.stepCount - 1);
  const nextLength = clamp(length, 1, session.kshState.stepCount - nextStart);
  const row = session.kshState.channels[channel];
  const source = session.selectedSource;
  const range = source >= 0 && source < SOURCE_COUNT
    ? session.kshState.sourceSettings[source].loopRanges[channel]
    : row;
  if ((range.loopStart ?? 0) === nextStart && range.loopLength === nextLength) {
    return;
  }

  range.loopStart = nextStart;
  range.loopLength = nextLength;
  row.loopStart = nextStart;
  row.loopLength = nextLength;
  if (session.selectedChannel === channel) {
    const range = loopRangeForChannel(session.kshState, channel);
    session.selectedStep = clamp(session.selectedStep, range.start, range.end);
  }
  bumpState();
  await sendChannelLoopLength(channel);
}

export async function setChannelLabel(channel, text) {
  await commitEditHistory("Rename channel", async () => {
    const trimmed = String(text ?? "").trim();
    session.kshState.channels[channel].label = trimmed || DEFAULT_CHANNEL_LABELS[channel] || String(channel + 1);
    bumpState();
    await sendCommand("channel_label", [channel + 1, session.kshState.channels[channel].label]);
  });
}

export async function resetSourceChannelRow(source, channel) {
  await commitEditHistory("Reset channel row", async () => {
    session.kshState.sourceChannelMutes[source][channel] = 0;
    session.kshState.sourceSettings[source].loopRanges[channel].loopStart = 0;
    session.kshState.sourceSettings[source].loopRanges[channel].loopLength = session.kshState.sourceSettings[source].stepCount;
    if (session.kshState.staticSource === source) {
      session.kshState.channels[channel].loopStart = 0;
      session.kshState.channels[channel].loopLength = session.kshState.stepCount;
    }

    for (let step = 0; step < MAX_STEPS; step += 1) {
      session.kshState.sources[source][channel][step] = defaultCell();
    }

    bumpState();
    await sendCommand("source_channel_reset", [source + 1, channel + 1]);
  });
}

export async function clearSourceChannelSteps(source, channel) {
  await commitEditHistory("Clear channel row steps", async () => {
    const steps = Array.from({ length: MAX_STEPS }, (_, step) => step);

    for (const step of steps) {
      session.kshState.sources[source][channel][step] = defaultCell();
    }

    bumpState();
    bumpOptimisticPreview();

    await withPreviewSuppressed(async () => {
      for (const step of steps) {
        await sendCellCommand(source, channel, step);
      }
    });
  });
}

export async function sendCellsForChannel(source, channel, steps) {
  bumpState();
  for (const step of steps) {
    await sendCellCommand(source, channel, step);
  }
}

export async function adjustChannelNote(channel, delta) {
  await commitEditHistory("Change channel note", async () => {
    session.kshState.channels[channel].note = clamp(session.kshState.channels[channel].note + delta, 0, 127);
    bumpState();
    await sendCommand("channel_note", [channel + 1, session.kshState.channels[channel].note]);
  });
}

export async function setChannelNote(channel, note) {
  const next = clamp(Math.round(note), 0, 127);
  const row = session.kshState.channels[channel];

  if (row.note === next) {
    return;
  }

  row.note = next;
  bumpState();
  await sendCommand("channel_note", [channel + 1, next]);
}

export function toggleChannelNoteDisplayNames() {
  session.channelNoteDisplayNames = !session.channelNoteDisplayNames;
}

export async function toggleCell(source, channel, step) {
  const cell = session.kshState.sources[source][channel][step];
  cell.enabled = cell.enabled ? 0 : 1;
  bumpState();
  await sendCell(source, channel, step);
}

export async function cycleMode() {
  await commitEditHistory("Change generation mode", async () => {
    cycleGenerationMode(session.kshState);
    bumpState();
    await sendCommand("mode", [session.kshState.generationMode]);
  });
}

export async function cycleRateCommand(direction = 1) {
  await commitEditHistory("Change step value", async () => {
    if (session.selectedSource === SILENT_SOURCE) {
      return;
    }
    cycleRate(session.kshState, direction);
    session.kshState.sourceSettings[session.selectedSource].rate = session.kshState.rate;
    bumpState();
    await sendCommand("source_rate", [session.selectedSource + 1, session.kshState.rate]);
  });
}

export async function setRateCommand(rate) {
  const nextRate = normalizeRate(rate);
  if (session.selectedSource === SILENT_SOURCE || session.kshState.rate === nextRate) {
    return;
  }

  await commitEditHistory("Change step value", async () => {
    session.kshState.rate = nextRate;
    session.kshState.sourceSettings[session.selectedSource].rate = nextRate;
    bumpState();
    await sendCommand("source_rate", [session.selectedSource + 1, nextRate]);
  });
}


export async function setStandaloneTransportPlaying(playing) {
  session.kshState.standaloneTransportPlaying = playing ? 1 : 0;
  if (!session.kshState.standaloneTransportPlaying) {
    session.playingStep = 0;
  }
  bumpState();
  await sendCommand("standalone_transport_playing", [session.kshState.standaloneTransportPlaying]);
}

export async function adjustStandaloneTempo(delta) {
  const next = clamp(Math.round((session.kshState.standaloneTempo ?? session.kshState.tempo) + delta), 20, 300);
  session.kshState.standaloneTempo = next;
  session.kshState.tempo = next;
  bumpState();
  await sendCommand("standalone_tempo", [next]);
}

export async function setStandaloneTempoFromInput(event) {
  const nextTempo = Number.parseFloat(event.currentTarget.value);
  if (Number.isNaN(nextTempo)) {
    return;
  }

  const next = clamp(Math.round(nextTempo), 20, 300);
  session.kshState.standaloneTempo = next;
  session.kshState.tempo = next;
  bumpState();
  await sendCommand("standalone_tempo", [next]);
}

export function auditionChannel(channel) {
  // Fire-and-forget: do not await — chords must not serialize on the native Promise.
  void triggerRow(channel, 100);
}

export async function incrementChannelNote(channel) {
  await adjustChannelNote(channel, 1);
}

export async function cycleChannelLock(channel) {
  await commitEditHistory("Change channel lock", async () => {
    session.kshState.channels[channel].lock += 1;
    if (session.kshState.channels[channel].lock >= SOURCE_COUNT) {
      session.kshState.channels[channel].lock = -1;
    }
    bumpState();
    await sendChannel(channel);
  });
}

export async function setChannelLock(channel, lock) {
  const nextLock = Math.max(-1, Math.min(SOURCE_COUNT - 1, Math.round(lock)));
  session.kshState.channels[channel].lock = nextLock;
  bumpState();
  await sendChannel(channel);
}

export async function setChannelPlaybackMode(channel, mode) {
  const normalized = normalizePlaybackMode(mode);
  if (session.kshState.channels[channel].playbackMode === normalized) {
    return;
  }

  await commitEditHistory("Change playback mode", async () => {
    session.kshState.channels[channel].playbackMode = normalized;
    bumpState();
    await sendChannelPlaybackMode(channel);
  });
}

export async function cycleChannelPlaybackMode(channel) {
  await commitEditHistory("Change playback mode", async () => {
    session.kshState.channels[channel].playbackMode = nextPlaybackMode(
      session.kshState.channels[channel].playbackMode
    );
    bumpState();
    await sendChannelPlaybackMode(channel);
  });
}

export async function setSourceChannelMute(source, channel, muted) {
  session.kshState.sourceChannelMutes[source][channel] = muted ? 1 : 0;
  bumpState();
  await sendSourceChannelMute(source, channel);
}

export async function toggleChannelMute(source, channel) {
  await setSourceChannelMute(source, channel, !session.kshState.sourceChannelMutes[source][channel]);
}

function clearSourceChannelSolo() {
  session.sourceChannelSoloSource = -1;
  session.sourceChannelSoloChannel = -1;
  session.sourceChannelSoloRestoreMutes = null;
}

async function applySourceChannelMuteState(source, nextMutes) {
  const previousMutes = session.kshState.sourceChannelMutes[source];
  session.kshState.sourceChannelMutes[source] = nextMutes.map((muted) => muted ? 1 : 0);
  bumpState();

  for (let channel = 0; channel < nextMutes.length; channel += 1) {
    if (previousMutes[channel] !== session.kshState.sourceChannelMutes[source][channel]) {
      await sendSourceChannelMute(source, channel);
    }
  }
}

export async function toggleSourceChannelSolo(source, channel) {
  source = clamp(source, 0, SOURCE_COUNT - 1);
  channel = clamp(channel, 0, MAX_CHANNELS - 1);

  await commitEditHistory("Toggle channel solo", async () => {
    if (
      session.sourceChannelSoloSource === source &&
      session.sourceChannelSoloChannel === channel &&
      session.sourceChannelSoloRestoreMutes
    ) {
      const restoreMutes = [...session.sourceChannelSoloRestoreMutes];
      clearSourceChannelSolo();
      await applySourceChannelMuteState(source, restoreMutes);
      return;
    }

    if (session.sourceChannelSoloSource >= 0 && session.sourceChannelSoloSource !== source) {
      const restoreSource = session.sourceChannelSoloSource;
      const restoreMutes = session.sourceChannelSoloRestoreMutes
        ? [...session.sourceChannelSoloRestoreMutes]
        : null;
      clearSourceChannelSolo();

      if (restoreMutes) {
        await applySourceChannelMuteState(restoreSource, restoreMutes);
      }
    }

    if (session.sourceChannelSoloSource !== source || !session.sourceChannelSoloRestoreMutes) {
      session.sourceChannelSoloRestoreMutes = [...session.kshState.sourceChannelMutes[source]];
    }

    session.sourceChannelSoloSource = source;
    session.sourceChannelSoloChannel = channel;

    const channelCount = session.kshState.channelCount;
    const nextMutes = session.kshState.sourceChannelMutes[source].map((muted, index) =>
      index < channelCount ? (index === channel ? 0 : 1) : muted
    );
    await applySourceChannelMuteState(source, nextMutes);
  });
}

export async function shiftChannelRow(channel, direction) {
  await commitEditHistory("Shift channel row", async () => {
    const source = session.selectedSource;
    if (source === SILENT_SOURCE) {
      return;
    }
    const steps = shiftSourceChannelRow(
      session.kshState,
      source,
      channel,
      direction,
      loopRangeForChannel(session.kshState, channel),
    );
    bumpState();
    bumpOptimisticPreview();

    await withPreviewSuppressed(async () => {
      for (const step of steps) {
        await sendCellCommand(source, channel, step);
      }
    });
  });
}

export async function shiftPattern(direction) {
  await commitEditHistory("Shift pattern", async () => {
    const source = session.selectedSource;
    if (source === SILENT_SOURCE) {
      return;
    }
    const shifted = [];

    for (let channel = 0; channel < session.kshState.channelCount; channel += 1) {
      shifted.push({
        channel,
        steps: shiftSourceChannelRow(session.kshState, source, channel, direction),
      });
    }

    bumpState();
    bumpOptimisticPreview();

    await withPreviewSuppressed(async () => {
      for (const row of shifted) {
        for (const step of row.steps) {
          await sendCellCommand(source, row.channel, step);
        }
      }
    });
  });
}

export async function clearPattern() {
  await commitEditHistory("Clear pattern", async () => {
    const source = session.selectedSource;
    if (source === SILENT_SOURCE) {
      return;
    }
    const { channelCount } = session.kshState;

    clearSourcePattern(session.kshState, source);
    bumpState();
    bumpOptimisticPreview();

    await withPreviewSuppressed(async () => {
      for (let channel = 0; channel < channelCount; channel += 1) {
        await sendCommand("source_channel_reset", [source + 1, channel + 1]);
        await sendCommand("channel_lock", [channel + 1, "random"]);
        await sendChannelPlaybackMode(channel);
      }
    });
  });
}

export async function setPatternRecordingEnabled(enabled) {
  const next = enabled ? 1 : 0;

  if (next && session.selectedSource === SILENT_SOURCE) {
    return;
  }

  if (session.patternRecordingEnabled === next) {
    return;
  }

  if (next) {
    patternRecordHistoryBefore = createHistorySnapshot();
  }

  session.patternRecordingEnabled = next;
  await sendCommand("pattern_record_enabled", [next, session.selectedSource + 1]);

  if (!next) {
    if (patternRecordHistoryBefore) {
      pushHistoryEntry("Record pattern", patternRecordHistoryBefore, createHistorySnapshot());
    }
    patternRecordHistoryBefore = null;
  }
}

export async function togglePatternRecording() {
  await setPatternRecordingEnabled(!session.patternRecordingEnabled);
}

export function recordPatternRow(channel, velocity = 100) {
  if (!session.patternRecordingEnabled) {
    return;
  }

  const row = clamp(channel, 0, session.kshState.channelCount - 1);
  // Fire-and-forget so recording hits stay as snappy as audition.
  void triggerRow(row, clamp(velocity, 1, 127));
}

export async function resizeForCurrentView() {
  const { width, height } = combinedDimensions(session.kshState, session.patternViewScale);
  await setViewSize(
    Math.round(width * uiScaleState.scale),
    Math.round(height * uiScaleState.scale),
    session.patternViewScale
  );
}

export function formatProjectDate(value) {
  if (!value) {
    return "";
  }

  const date = new Date(value);
  if (Number.isNaN(date.getTime())) {
    return "";
  }

  return date.toLocaleDateString("en-US", {
    month: "short",
    day: "numeric",
    year: "numeric",
  }).toUpperCase();
}

export async function refreshProjectState() {
  const state = await getProjectState();
  assignProjectMetadata(state);
}

async function runProjectOperation(task, { loadProjectContent = false } = {}) {
  if (session.projectOperationBusy) {
    return false;
  }

  session.projectOperationBusy = true;
  session.projectOperationError = "";

  try {
    const result = await task();
    const success = Boolean(Number.parseInt(String(result?.success ?? 0), 10));
    const error = String(result?.error ?? "");

    if (error) {
      session.projectOperationError = error;
    }

    if (!success) {
      return false;
    }

    assignProjectMetadata(result);

    if (loadProjectContent) {
      clearEditHistory();
      clearSourceChannelSolo();
      await syncAll();
      await resizeForCurrentView();
    } else {
      await refreshProjectState();
    }

    return true;
  } catch {
    session.projectOperationError = "The project operation could not be completed.";
    return false;
  } finally {
    session.projectOperationBusy = false;
  }
}

export function saveProject() {
  void runProjectOperation(() => nativeSaveProject(session.projectName, session.projectDescription));
}

export function loadProject() {
  void runProjectOperation(() => nativeLoadProject(), { loadProjectContent: true });
}

export function createNewProject() {
  void runProjectOperation(() => nativeNewProject(), { loadProjectContent: true });
}

export function cycleProject(direction) {
  void runProjectOperation(() => nativeCycleProject(direction), { loadProjectContent: true });
}

export async function togglePatternViewScale() {
  session.patternViewScale = normalizePatternViewScale(session.patternViewScale === 1.5 ? 1 : 1.5);
  await resizeForCurrentView();
}

export function setSourceLayerMode(mode) {
  session.sourceLayerMode = mode;
}

export function cycleSourceLayerMode() {
  session.sourceLayerMode = cycleLayerMode(session.sourceLayerMode);
}

export function toggleDcColors() {
  session.dcColors = session.dcColors ? 0 : 1;
}

export function initKshSession() {
  if (sessionStarted) {
    return () => {};
  }

  sessionStarted = true;
  lastStackPreviewSource = null;

  teardownHandlers = [
    onBackendEvent("engine_state", (payload) => {
      if (historyApplying) {
        return;
      }

      const parsed = parseBackendJson(payload);
      lastStackPreviewSource = null;
      const previousStepCount = session.kshState.stepCount;
      applyEngineState(session.kshState, parsed);
      if (parsed?.patternViewScale !== undefined) {
        session.patternViewScale = normalizePatternViewScale(parsed.patternViewScale);
      }
      if (parsed?.projectUiScalePercent !== undefined && session.ready) {
        void applyProjectUiScalePercent(parsed.projectUiScalePercent);
      }
      if (parsed?.patternRecordingEnabled !== undefined) {
        session.patternRecordingEnabled = parsed.patternRecordingEnabled ? 1 : 0;
      }
      assignProjectMetadata(parsed);
      session.selectedSource = session.kshState.staticSource;
      clearSourceChannelSolo();
      bumpState();
      if (session.kshState.stepCount !== previousStepCount) {
        void resizeForCurrentView();
      }
    }),
    onBackendEvent("preview", (payload) => {
      if (previewSuppressionDepth > 0) {
        return;
      }

      const parsed = parseBackendJson(payload);
      const stackSource = stackSourceForPreview(session.kshState, parsed);
      if (stackSource === null) {
        lastStackPreviewSource = null;
      } else if (stackSource !== lastStackPreviewSource) {
        session.selectedSource = stackSource;
        lastStackPreviewSource = stackSource;
      }
      session.previewData = parsed;
    }),
    onBackendEvent("note_hit", handleNoteHit),
    onBackendEvent("current_step", (payload) => {
      const step = Number(payload);
      session.playingStep = Number.isFinite(step) ? clamp(step, 0, MAX_STEPS) : 0;
    }),
    onBackendEvent("status", (payload) => {
      if (!payload?.selector) {
        return;
      }
      const args = Array.isArray(payload.args)
        ? payload.args
        : payload.args !== undefined
          ? [payload.args]
          : [];
      const previousStepCount = session.kshState.stepCount;
      applyStatusMessage(session.kshState, payload.selector, args);
      if (payload.selector === "static_source") {
        session.selectedSource = session.kshState.staticSource;
      }
      bumpState();
      if (session.kshState.stepCount !== previousStepCount) {
        void resizeForCurrentView();
      }
    }),
  ];

  waitForBackend()
    .then(async () => {
      await syncAll();
      await refreshProjectState();
      await syncEditorScaleMinimumToNative();
      await resizeForCurrentView();
      session.ready = true;
    })
    .catch((error) => {
      session.bridgeError = String(error?.message ?? error);
    });

  return () => {
    for (const remove of teardownHandlers) {
      remove?.();
    }
    teardownHandlers = [];
    sessionStarted = false;
  };
}

export { cloneCell, defaultCell };
