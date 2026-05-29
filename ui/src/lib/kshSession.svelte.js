import {
  DEFAULT_CHANNEL_LABELS,
  MAX_CHANNELS,
  MAX_STEPS,
  NOTE_HIT_FLASH_MS,
  SOURCE_COUNT,
  clamp,
} from "./kshConstants.js";
import {
  applyPhaseMsToState,
  clampHeaderValue,
} from "./kshEditorInteractions.js";
import {
  clearSourcePattern,
  combinedDimensions,
  cycleGenerationMode,
  cycleLayerMode,
  cycleRate,
  nextPlaybackMode,
  shiftSourceChannelRow,
} from "./kshEditorUtils.js";
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
  sourceLayerMode: "velocity",
  bridgeError: "",
  ready: false,
});
let sessionStarted = false;
/** @type {Array<() => void>} */
let teardownHandlers = [];

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

export async function sendCell(source, channel, step) {
  const cell = session.kshState.sources[source][channel][step];
  bumpState();
  await sendCommand("cell", [
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
  ]);
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
    state.stepCount = next;
    state.refreshSteps = clamp(state.refreshSteps, 1, next);
    for (let channel = 0; channel < MAX_CHANNELS; channel += 1) {
      state.channels[channel].loopLength = clamp(state.channels[channel].loopLength, 1, next);
    }
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

  if (id === "phase_early_ms") {
    applyPhaseMsToState(state, next);
    bumpState();
    await sendCommand("phase_offset_beats", [state.phaseOffsetBeats]);
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
  for (const step of steps) {
    await sendCell(source, channel, step);
  }
  bumpState();
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

export async function shiftChannelRow(channel, direction) {
  const steps = shiftSourceChannelRow(session.kshState, session.selectedSource, channel, direction);
  bumpState();
  for (const step of steps) {
    await sendCell(session.selectedSource, channel, step);
  }
}

export async function shiftPattern(direction) {
  for (let channel = 0; channel < session.kshState.channelCount; channel += 1) {
    await shiftChannelRow(channel, direction);
  }
}

export async function clearPattern() {
  clearSourcePattern(session.kshState, session.selectedSource);
  bumpState();
  for (let channel = 0; channel < session.kshState.channelCount; channel += 1) {
    await sendCommand("source_channel_reset", [session.selectedSource + 1, channel + 1]);
    await sendCommand("channel_lock", [channel + 1, "random"]);
    await sendChannelPlaybackMode(channel);
  }
}

export async function resizeForCurrentView() {
  const { width, height } = combinedDimensions(session.kshState);
  await setViewSize(width, height);
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
      bumpState();
    }),
    onBackendEvent("preview", (payload) => {
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
