import {
  COMPACT_HEIGHT,
  COMPACT_WIDTH,
  DEFAULT_CHANNEL_LABELS,
  MAX_LANES,
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
  cycleGenerationMode,
  cycleLayerMode,
  cycleRate,
  editorDimensions,
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
  viewMode: "compact",
  kshState: makeDefaultKshState(),
  previewData: null,
  compactNoteFlashes: {},
  editorNoteFlashes: {},
  playingStep: 0,
  selectedSource: 0,
  selectedLane: 0,
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
    lanes: session.kshState.lanes.map((lane) => ({ ...lane })),
    sources: session.kshState.sources.map((source) =>
      source.map((lane) => lane.map((cell) => ({ ...cell })))
    ),
    sourceChannelMutes: session.kshState.sourceChannelMutes.map((row) => [...row]),
  };
}

function compactFlashKey(lane, step) {
  return `${lane}:${step}`;
}

function editorFlashKey(source, lane, step) {
  return `${source}:${lane}:${step}`;
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

    const [source, lane, step] = key.split(":").map(Number);
    const sourceRow = session.editorNoteFlashes[source];
    if (!sourceRow?.[lane]?.[step]) {
      return;
    }

    const next = { ...session.editorNoteFlashes };
    next[source] = { ...next[source] };
    next[source][lane] = { ...next[source][lane] };
    delete next[source][lane][step];
    session.editorNoteFlashes = next;
  }, delayMs + 5);
}

function handleCompactNoteHit(payload) {
  const lane = clamp(Number(payload?.channel ?? 0), 1, MAX_LANES) - 1;
  const step = clamp(Number(payload?.generatedStep ?? 0), 1, MAX_STEPS) - 1;
  const key = compactFlashKey(lane, step);
  session.compactNoteFlashes = {
    ...session.compactNoteFlashes,
    [key]: Date.now() + NOTE_HIT_FLASH_MS,
  };
  scheduleFlashClear(key, "compact", NOTE_HIT_FLASH_MS);
}

function handleEditorNoteHit(payload) {
  const source = clamp(Number(payload?.source ?? 0), 1, SOURCE_COUNT) - 1;
  const lane = clamp(Number(payload?.channel ?? 0), 1, MAX_LANES) - 1;
  const step = clamp(Number(payload?.sourceStep ?? 0), 1, MAX_STEPS) - 1;
  const key = editorFlashKey(source, lane, step);
  const until = Date.now() + NOTE_HIT_FLASH_MS;

  session.editorNoteFlashes = {
    ...session.editorNoteFlashes,
    [source]: {
      ...(session.editorNoteFlashes[source] ?? {}),
      [lane]: {
        ...((session.editorNoteFlashes[source] ?? {})[lane] ?? {}),
        [step]: until,
      },
    },
  };
  scheduleFlashClear(key, "editor", NOTE_HIT_FLASH_MS);
}

function handleNoteHit(payload) {
  if (session.viewMode === "editor") {
    handleEditorNoteHit(payload);
  } else {
    handleCompactNoteHit(payload);
  }
}

export function isCompactFlashing(lane, step) {
  const until = session.compactNoteFlashes[compactFlashKey(lane, step)];
  return until !== undefined && until > Date.now();
}

export function isEditorFlashing(source, lane, step) {
  const until = session.editorNoteFlashes[source]?.[lane]?.[step];
  return until !== undefined && until > Date.now();
}

export async function sendCell(source, lane, step) {
  const cell = session.kshState.sources[source][lane][step];
  bumpState();
  await sendCommand("cell", [
    source + 1,
    lane + 1,
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

export async function sendLane(lane) {
  const row = session.kshState.lanes[lane];
  await sendCommand("channel_label", [lane + 1, row.label]);
  await sendCommand("channel_note", [lane + 1, row.note]);
  if (row.lock < 0) {
    await sendCommand("channel_lock", [lane + 1, "random"]);
  } else {
    await sendCommand("channel_lock", [lane + 1, row.lock + 1]);
  }
}

export async function sendChannelPlaybackMode(lane) {
  await sendCommand("channel_playback_mode", [lane + 1, session.kshState.lanes[lane].playbackMode]);
}

export async function sendSourceChannelMute(source, lane) {
  await sendCommand("source_channel_mute", [
    source + 1,
    lane + 1,
    session.kshState.sourceChannelMutes[source][lane],
  ]);
}

export function setSelectedLane(lane) {
  setSelectedCell(lane, session.selectedStep);
}

export function setSelectedCell(lane, step) {
  session.selectedLane = clamp(lane, 0, MAX_LANES - 1);
  const loopLength = clamp(
    session.kshState.lanes[session.selectedLane]?.loopLength ?? session.kshState.stepCount,
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

export async function sendChannelLoopLength(lane) {
  await sendCommand("channel_loop_length", [
    lane + 1,
    session.kshState.lanes[lane].loopLength,
  ]);
}

export async function setHeaderValue(id, value) {
  const next = clampHeaderValue(session.kshState, id, value);
  const state = session.kshState;

  if (id === "steps" && state.stepCount !== next) {
    state.stepCount = next;
    state.refreshSteps = clamp(state.refreshSteps, 1, next);
    for (let lane = 0; lane < MAX_LANES; lane += 1) {
      state.lanes[lane].loopLength = clamp(state.lanes[lane].loopLength, 1, next);
    }
    bumpState();
    await sendCommand("steps", [next]);
    await sendCommand("refresh_steps", [state.refreshSteps]);
    if (session.viewMode === "editor") {
      await resizeForCurrentView();
    }
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

export async function setRowLoopLength(lane, value) {
  const next = clamp(value, 1, session.kshState.stepCount);
  if (session.kshState.lanes[lane].loopLength === next) {
    return;
  }

  session.kshState.lanes[lane].loopLength = next;
  if (session.selectedLane === lane && session.selectedStep >= next) {
    session.selectedStep = next - 1;
  }
  bumpState();
  await sendChannelLoopLength(lane);
}

export async function setLaneLabel(lane, text) {
  const trimmed = String(text ?? "").trim();
  session.kshState.lanes[lane].label = trimmed || DEFAULT_CHANNEL_LABELS[lane] || String(lane + 1);
  bumpState();
  await sendCommand("channel_label", [lane + 1, session.kshState.lanes[lane].label]);
}

export async function resetSourceChannelRow(source, lane) {
  session.kshState.sourceChannelMutes[source][lane] = 0;
  session.kshState.lanes[lane].loopLength = session.kshState.stepCount;

  for (let step = 0; step < MAX_STEPS; step += 1) {
    session.kshState.sources[source][lane][step] = defaultCell();
  }

  bumpState();
  await sendCommand("source_channel_reset", [source + 1, lane + 1]);
}

export async function sendCellsForLane(source, lane, steps) {
  for (const step of steps) {
    await sendCell(source, lane, step);
  }
  bumpState();
}

export async function adjustLaneNote(lane, delta) {
  session.kshState.lanes[lane].note = clamp(session.kshState.lanes[lane].note + delta, 0, 127);
  bumpState();
  await sendCommand("channel_note", [lane + 1, session.kshState.lanes[lane].note]);
}

export async function toggleCell(source, lane, step) {
  const cell = session.kshState.sources[source][lane][step];
  cell.enabled = cell.enabled ? 0 : 1;
  bumpState();
  await sendCell(source, lane, step);
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

export async function auditionLane(lane) {
  await sendCommand("channel_audition", [lane + 1]);
}

export async function incrementLaneNote(lane) {
  await adjustLaneNote(lane, 1);
}

export async function cycleLaneLock(lane) {
  session.kshState.lanes[lane].lock += 1;
  if (session.kshState.lanes[lane].lock >= SOURCE_COUNT) {
    session.kshState.lanes[lane].lock = -1;
  }
  bumpState();
  await sendLane(lane);
}

export async function cycleLanePlaybackMode(lane) {
  session.kshState.lanes[lane].playbackMode = nextPlaybackMode(
    session.kshState.lanes[lane].playbackMode
  );
  bumpState();
  await sendChannelPlaybackMode(lane);
}

export async function setSourceChannelMute(source, lane, muted) {
  session.kshState.sourceChannelMutes[source][lane] = muted ? 1 : 0;
  bumpState();
  await sendSourceChannelMute(source, lane);
}

export async function toggleLaneMute(source, lane) {
  await setSourceChannelMute(source, lane, !session.kshState.sourceChannelMutes[source][lane]);
}

export async function shiftLaneRow(lane, direction) {
  const steps = shiftSourceChannelRow(session.kshState, session.selectedSource, lane, direction);
  bumpState();
  for (const step of steps) {
    await sendCell(session.selectedSource, lane, step);
  }
}

export async function shiftPattern(direction) {
  for (let lane = 0; lane < session.kshState.laneCount; lane += 1) {
    await shiftLaneRow(lane, direction);
  }
}

export async function clearPattern() {
  clearSourcePattern(session.kshState, session.selectedSource);
  bumpState();
  for (let lane = 0; lane < session.kshState.laneCount; lane += 1) {
    await sendCommand("source_channel_reset", [session.selectedSource + 1, lane + 1]);
    await sendCommand("channel_lock", [lane + 1, "random"]);
    await sendChannelPlaybackMode(lane);
  }
}

export async function resizeForCurrentView() {
  if (session.viewMode === "editor") {
    const { width, height } = editorDimensions(session.kshState);
    await setViewSize(width, height);
    return;
  }

  await setViewSize(COMPACT_WIDTH, COMPACT_HEIGHT);
}

export async function openEditor() {
  session.viewMode = "editor";
  session.selectedSource = session.kshState.staticSource;
  await sendCommand("open_editor");
  await resizeForCurrentView();
  await syncAll();
}

export async function closeEditor() {
  session.viewMode = "compact";
  session.playingStep = 0;
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
