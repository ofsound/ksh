import {
  DEFAULT_CHANNEL_COUNT,
  DEFAULT_CHANNEL_LABELS,
  DEFAULT_CHANNEL_NOTES,
  MAX_CHANNELS,
  MAX_STEPS,
  SOURCE_COUNT,
  clamp,
  normalizePlaybackMode,
  normalizeRate,
  toggleValue,
} from "./kshConstants.js";

export function defaultCell() {
  return {
    enabled: 0,
    velocity: 100,
    probability: 100,
    cycle: 1,
    cycleOffset: 0,
    cycleInverted: 0,
    roll: 1,
    source: 0,
  };
}

export function cloneCell(cell) {
  return {
    enabled: cell?.enabled ? 1 : 0,
    velocity: clamp(cell?.velocity ?? 100, 1, 127),
    probability: clamp(cell?.probability ?? 100, 0, 100),
    cycle: clamp(cell?.cycle ?? 1, 1, 32),
    cycleOffset: clamp(cell?.cycleOffset ?? 0, 0, 31),
    cycleInverted: cell?.cycleInverted ? 1 : 0,
    roll: clamp(cell?.roll ?? 1, 1, 16),
    source: clamp(cell?.source ?? 0, 0, SOURCE_COUNT - 1),
  };
}

export function makeEmptySources() {
  return Array.from({ length: SOURCE_COUNT }, () =>
    Array.from({ length: MAX_CHANNELS }, () =>
      Array.from({ length: MAX_STEPS }, () => defaultCell())
    )
  );
}

export function makeSourceChannelMutes() {
  return Array.from({ length: SOURCE_COUNT }, () => Array(MAX_CHANNELS).fill(0));
}

export function makeDefaultKshState() {
  const channels = Array.from({ length: MAX_CHANNELS }, (_, index) => ({
    label: DEFAULT_CHANNEL_LABELS[index] ?? String(index + 1),
    note: DEFAULT_CHANNEL_NOTES[index] ?? 36 + index,
    lock: -1,
    loopLength: 16,
    playbackMode: "normal",
  }));

  return {
    stepCount: 16,
    channelCount: DEFAULT_CHANNEL_COUNT,
    refreshSteps: 1,
    generationMode: "static",
    staticSource: 0,
    rate: "16n",
    swing: 0,
    velocityHumanize: 0,
    timingHumanize: 0,
    deviceActive: 1,
    tempo: 120,
    standaloneTransportAvailable: 0,
    standaloneTransportPlaying: 0,
    standaloneTempo: 120,
    channels,
    sources: makeEmptySources(),
    sourceChannelMutes: makeSourceChannelMutes(),
  };
}

/** Alias kept for existing imports. */
export const makeDefaultCompactState = makeDefaultKshState;

export function resizeChannelLoopLengths(state, nextStepCount, previousStepCount = state.stepCount) {
  for (let channel = 0; channel < MAX_CHANNELS; channel += 1) {
    const loopLength = state.channels[channel].loopLength;
    state.channels[channel].loopLength =
      loopLength === previousStepCount ? nextStepCount : clamp(loopLength, 1, nextStepCount);
  }
}

function applyPersistencePayload(state, payload) {
  if (!payload || payload.v !== 1) {
    return false;
  }

  state.stepCount = clamp(payload.stepCount, 1, MAX_STEPS);
  state.channelCount = clamp(payload.channelCount, 1, MAX_CHANNELS);
  state.refreshSteps = clamp(payload.refreshSteps, 1, state.stepCount);
  state.generationMode =
    payload.generationMode === "per_channel" || payload.generationMode === "static"
      ? payload.generationMode
      : "stack";
  state.staticSource = clamp(payload.staticSource ?? 0, 0, SOURCE_COUNT - 1);
  state.rate = normalizeRate(payload.rate);
  state.swing = clamp(payload.swing, 0, 100);
  state.velocityHumanize = clamp(payload.velocityHumanize, 0, 100);
  state.timingHumanize = clamp(payload.timingHumanize, 0, 100);
  if (payload.deviceActive !== undefined) {
    state.deviceActive = toggleValue(payload.deviceActive);
  }
  state.tempo = Math.max(20, Math.min(300, Number.parseFloat(payload.tempo) || 120));
  state.standaloneTransportAvailable = payload.standaloneTransportAvailable ? 1 : 0;
  state.standaloneTransportPlaying = payload.standaloneTransportPlaying ? 1 : 0;
  state.standaloneTempo = Math.max(
    20,
    Math.min(300, Number.parseFloat(payload.standaloneTempo ?? state.tempo) || state.tempo)
  );

  if (!state.sources) {
    state.sources = makeEmptySources();
  }

  for (let source = 0; source < SOURCE_COUNT; source += 1) {
    for (let channel = 0; channel < MAX_CHANNELS; channel += 1) {
      for (let step = 0; step < MAX_STEPS; step += 1) {
        state.sources[source][channel][step] = defaultCell();
      }
    }
  }

  const channelsIn = payload.channels ?? [];
  for (let channel = 0; channel < MAX_CHANNELS; channel += 1) {
    if (channelsIn[channel]) {
      const row = channelsIn[channel];
      state.channels[channel].label = String(row[0] ?? state.channels[channel].label);
      state.channels[channel].note = clamp(row[1], 0, 127);
      state.channels[channel].lock = clamp(row[2], -1, SOURCE_COUNT - 1);
      state.channels[channel].loopLength = clamp(row[3], 1, state.stepCount);
      if (row[4] !== undefined) {
        state.channels[channel].playbackMode = normalizePlaybackMode(row[4]);
      }
    }
    state.channels[channel].loopLength = clamp(state.channels[channel].loopLength, 1, state.stepCount);
  }

  state.sourceChannelMutes = makeSourceChannelMutes();
  const mutesIn = payload.sourceChannelMutes ?? [];
  for (let source = 0; source < SOURCE_COUNT; source += 1) {
    const muteRow = mutesIn[source] ?? [];
    for (let channel = 0; channel < MAX_CHANNELS; channel += 1) {
      state.sourceChannelMutes[source][channel] = muteRow[channel] ? 1 : 0;
    }
  }

  const cellsIn = payload.cells ?? [];
  for (let cellIndex = 0; cellIndex < cellsIn.length; cellIndex += 1) {
    const entry = cellsIn[cellIndex];
    if (!entry || entry.length < 7) {
      continue;
    }

    const source = entry[0];
    const channel = entry[1];
    const step = entry[2];
    if (
      source < 0 ||
      source >= SOURCE_COUNT ||
      channel < 0 ||
      channel >= MAX_CHANNELS ||
      step < 0 ||
      step >= MAX_STEPS
    ) {
      continue;
    }

    state.sources[source][channel][step] = cloneCell({
      enabled: entry[3],
      velocity: entry[4],
      probability: entry[5],
      cycle: entry[6],
      cycleOffset: entry[7],
      cycleInverted: entry[8],
      roll: entry[9],
    });
  }

  return true;
}

export function applyEngineState(state, engineState) {
  if (!engineState) {
    return;
  }

  applyPersistencePayload(state, engineState);
}

export function applyStatusMessage(state, selector, args = []) {
  const values = [...args];

  switch (selector) {
    case "steps":
    {
      const previousStepCount = state.stepCount;
      state.stepCount = clamp(values[0], 1, MAX_STEPS);
      state.refreshSteps = clamp(state.refreshSteps, 1, state.stepCount);
      resizeChannelLoopLengths(state, state.stepCount, previousStepCount);
      break;
    }
    case "channels":
      state.channelCount = clamp(values[0], 1, MAX_CHANNELS);
      break;
    case "refresh_steps":
      state.refreshSteps = clamp(values[0], 1, state.stepCount);
      break;
    case "mode":
      state.generationMode =
        String(values[0]) === "per_channel" || String(values[0]) === "static"
          ? String(values[0])
          : "stack";
      break;
    case "static_source":
      state.staticSource = clamp(values[0] - 1, 0, SOURCE_COUNT - 1);
      break;
    case "rate":
      state.rate = normalizeRate(values[0]);
      break;
    case "swing":
      state.swing = clamp(values[0], 0, 100);
      break;
    case "velocity_humanize":
      state.velocityHumanize = clamp(values[0], 0, 100);
      break;
    case "timing_humanize":
      state.timingHumanize = clamp(values[0], 0, 100);
      break;
    case "device_active":
      state.deviceActive = toggleValue(values[0]);
      break;
    case "tempo":
      state.tempo = Math.max(20, Math.min(300, Number.parseFloat(values[0]) || 120));
      state.standaloneTempo = state.tempo;
      break;
    case "standalone_transport_playing":
      state.standaloneTransportPlaying = toggleValue(values[0]);
      break;
    case "standalone_tempo":
      state.standaloneTempo = Math.max(20, Math.min(300, Number.parseFloat(values[0]) || 120));
      state.tempo = state.standaloneTempo;
      break;
    default:
      break;
  }
}
