import {
  DEFAULT_CHANNEL_LABELS,
  MAX_CHANNELS,
  MAX_STEPS,
  NOTE_HIT_FLASH_MS,
  SOURCE_COUNT,
  clamp,
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
  normalizePatternViewScale,
  nextPlaybackMode,
  shiftSourceChannelRow,
} from "./kshEditorUtils.js";
import {
  applyRecomposedPreview,
} from "./kshPreviewUtils.js";
import {
  onBackendEvent,
  parseBackendJson,
  sendCommand,
  setViewSize,
  syncAll,
  waitForBackend,
} from "./kshBridge.js";
import {
  applyEngineState,
  applyStatusMessage,
  cloneCell,
  defaultCell,
  makeDefaultKshState,
  resizeChannelLoopLengths,
} from "./kshUiState.js";

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
  sourceLayerMode: "velocity",
  patternCopySource: -1,
  sourceChannelSoloSource: -1,
  sourceChannelSoloChannel: -1,
  sourceChannelSoloRestoreMutes: null,
  bridgeError: "",
  ready: false,
});
let sessionStarted = false;
/** @type {Array<() => void>} */
let teardownHandlers = [];
let previewSuppressionDepth = 0;

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
    sourceChannelMutes: session.kshState.sourceChannelMutes.map((row) => [...row]),
  };
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
  const loopLength = clamp(
    session.kshState.channels[session.selectedChannel]?.loopLength ?? session.kshState.stepCount,
    1,
    session.kshState.stepCount
  );
  session.selectedStep = clamp(step, 0, loopLength - 1);
}

export async function selectSource(source) {
  session.selectedSource = clamp(source, 0, SOURCE_COUNT - 1);
  session.kshState.staticSource = session.selectedSource;
  bumpState();
  await sendCommand("static_source", [session.selectedSource + 1]);
}

export function beginPatternCopy(source) {
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

  if (!copySourcePattern(session.kshState, source, destination)) {
    return;
  }

  session.patternCopySource = -1;
  session.selectedSource = destination;
  session.kshState.staticSource = destination;
  bumpState();
  bumpOptimisticPreview();

  await sendCommand("source_pattern_copy", [source + 1, destination + 1]);
  await sendCommand("static_source", [destination + 1]);
}

export async function sendChannelLoopLength(channel) {
  await sendCommand("channel_loop_length", [
    channel + 1,
    session.kshState.channels[channel].loopLength,
  ]);
}

export async function setHeaderValue(id, value) {
  const next = clampHeaderValue(session.kshState, id, value);
  const state = session.kshState;

  if (id === "steps" && state.stepCount !== next) {
    const previousStepCount = state.stepCount;
    state.stepCount = next;
    state.refreshSteps = clamp(state.refreshSteps, 1, next);
    resizeChannelLoopLengths(state, next, previousStepCount);
    bumpState();
    await sendCommand("steps", [next]);
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
  const next = clamp(value, 1, session.kshState.stepCount);
  if (session.kshState.channels[channel].loopLength === next) {
    return;
  }

  session.kshState.channels[channel].loopLength = next;
  if (session.selectedChannel === channel && session.selectedStep >= next) {
    session.selectedStep = next - 1;
  }
  bumpState();
  await sendChannelLoopLength(channel);
}

export async function setChannelLabel(channel, text) {
  const trimmed = String(text ?? "").trim();
  session.kshState.channels[channel].label = trimmed || DEFAULT_CHANNEL_LABELS[channel] || String(channel + 1);
  bumpState();
  await sendCommand("channel_label", [channel + 1, session.kshState.channels[channel].label]);
}

export async function resetSourceChannelRow(source, channel) {
  session.kshState.sourceChannelMutes[source][channel] = 0;
  session.kshState.channels[channel].loopLength = session.kshState.stepCount;

  for (let step = 0; step < MAX_STEPS; step += 1) {
    session.kshState.sources[source][channel][step] = defaultCell();
  }

  bumpState();
  await sendCommand("source_channel_reset", [source + 1, channel + 1]);
}

export async function sendCellsForChannel(source, channel, steps) {
  bumpState();
  for (const step of steps) {
    await sendCellCommand(source, channel, step);
  }
}

export async function adjustChannelNote(channel, delta) {
  session.kshState.channels[channel].note = clamp(session.kshState.channels[channel].note + delta, 0, 127);
  bumpState();
  await sendCommand("channel_note", [channel + 1, session.kshState.channels[channel].note]);
}

export async function toggleCell(source, channel, step) {
  const cell = session.kshState.sources[source][channel][step];
  cell.enabled = cell.enabled ? 0 : 1;
  bumpState();
  await sendCell(source, channel, step);
}

export async function cycleMode() {
  cycleGenerationMode(session.kshState);
  bumpState();
  await sendCommand("mode", [session.kshState.generationMode]);
}

export async function cycleRateCommand(direction = 1) {
  cycleRate(session.kshState, direction);
  bumpState();
  await sendCommand("rate", [session.kshState.rate]);
}

export async function toggleDeviceActive() {
  session.kshState.deviceActive = session.kshState.deviceActive ? 0 : 1;
  bumpState();
  await sendCommand("device_active", [session.kshState.deviceActive]);
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

export async function auditionChannel(channel) {
  await sendCommand("channel_audition", [channel + 1]);
}

export async function incrementChannelNote(channel) {
  await adjustChannelNote(channel, 1);
}

export async function cycleChannelLock(channel) {
  session.kshState.channels[channel].lock += 1;
  if (session.kshState.channels[channel].lock >= SOURCE_COUNT) {
    session.kshState.channels[channel].lock = -1;
  }
  bumpState();
  await sendChannel(channel);
}

export async function cycleChannelPlaybackMode(channel) {
  session.kshState.channels[channel].playbackMode = nextPlaybackMode(
    session.kshState.channels[channel].playbackMode
  );
  bumpState();
  await sendChannelPlaybackMode(channel);
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
}

export async function shiftChannelRow(channel, direction) {
  const source = session.selectedSource;
  const steps = shiftSourceChannelRow(session.kshState, source, channel, direction);
  bumpState();
  bumpOptimisticPreview();

  await withPreviewSuppressed(async () => {
    for (const step of steps) {
      await sendCellCommand(source, channel, step);
    }
  });
}

export async function shiftPattern(direction) {
  const source = session.selectedSource;
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
}

export async function clearPattern() {
  const source = session.selectedSource;
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
}

export async function resizeForCurrentView() {
  const { width, height } = combinedDimensions(session.kshState, session.patternViewScale);
  await setViewSize(width, height);
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

  teardownHandlers = [
    onBackendEvent("engine_state", (payload) => {
      const parsed = parseBackendJson(payload);
      applyEngineState(session.kshState, parsed);
      session.selectedSource = session.kshState.staticSource;
      clearSourceChannelSolo();
      bumpState();
    }),
    onBackendEvent("preview", (payload) => {
      if (previewSuppressionDepth > 0) {
        return;
      }

      session.previewData = parseBackendJson(payload);
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
      applyStatusMessage(session.kshState, payload.selector, args);
      if (payload.selector === "static_source") {
        session.selectedSource = session.kshState.staticSource;
      }
      bumpState();
    }),
  ];

  waitForBackend()
    .then(async () => {
      await syncAll();
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
