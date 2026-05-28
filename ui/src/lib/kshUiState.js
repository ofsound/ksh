import {
  DEFAULT_CHANNEL_COUNT,
  DEFAULT_CHANNEL_LABELS,
  DEFAULT_CHANNEL_NOTES,
  MAX_LANES,
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
    Array.from({ length: MAX_LANES }, () =>
      Array.from({ length: MAX_STEPS }, () => defaultCell())
    )
  );
}

export function makeSourceChannelMutes() {
  return Array.from({ length: SOURCE_COUNT }, () => Array(MAX_LANES).fill(0));
}

export function makeDefaultKshState() {
  const lanes = Array.from({ length: MAX_LANES }, (_, index) => ({
    label: DEFAULT_CHANNEL_LABELS[index] ?? String(index + 1),
    note: DEFAULT_CHANNEL_NOTES[index] ?? 36 + index,
    lock: -1,
    loopLength: 16,
    playbackMode: "normal",
  }));

  return {
    stepCount: 16,
    laneCount: DEFAULT_CHANNEL_COUNT,
    refreshSteps: 1,
    generationMode: "static",
    staticSource: 0,
    rate: "16n",
    swing: 0,
    velocityHumanize: 0,
    timingHumanize: 0,
    deviceActive: 1,
    tempo: 120,
    phaseOffsetBeats: 0,
    lanes,
    sources: makeEmptySources(),
    sourceChannelMutes: makeSourceChannelMutes(),
  };
}

/** Alias kept for existing imports. */
export const makeDefaultCompactState = makeDefaultKshState;

function applyPersistencePayload(state, payload) {
  if (!payload || payload.v !== 1) {
    return false;
  }

  state.stepCount = clamp(payload.stepCount, 1, MAX_STEPS);
  state.laneCount = clamp(payload.channelCount, 1, MAX_LANES);
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
  state.phaseOffsetBeats = Number.parseFloat(payload.phaseOffsetBeats) || 0;

  if (!state.sources) {
    state.sources = makeEmptySources();
  }

  for (let source = 0; source < SOURCE_COUNT; source += 1) {
    for (let channel = 0; channel < MAX_LANES; channel += 1) {
      for (let step = 0; step < MAX_STEPS; step += 1) {
        state.sources[source][channel][step] = defaultCell();
      }
    }
  }

  const channelsIn = payload.channels ?? [];
  for (let channel = 0; channel < MAX_LANES; channel += 1) {
    if (channelsIn[channel]) {
      const row = channelsIn[channel];
      state.lanes[channel].label = String(row[0] ?? state.lanes[channel].label);
      state.lanes[channel].note = clamp(row[1], 0, 127);
      state.lanes[channel].lock = clamp(row[2], -1, SOURCE_COUNT - 1);
      state.lanes[channel].loopLength = clamp(row[3], 1, state.stepCount);
      if (row[4] !== undefined) {
        state.lanes[channel].playbackMode = normalizePlaybackMode(row[4]);
      }
    }
    state.lanes[channel].loopLength = clamp(state.lanes[channel].loopLength, 1, state.stepCount);
  }

  state.sourceChannelMutes = makeSourceChannelMutes();
  const mutesIn = payload.sourceChannelMutes ?? [];
  for (let source = 0; source < SOURCE_COUNT; source += 1) {
    const muteRow = mutesIn[source] ?? [];
    for (let channel = 0; channel < MAX_LANES; channel += 1) {
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
      channel >= MAX_LANES ||
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

/** Mirror M4L ksh_ui_shared.applyEngineState for compact UI. */
export function applyEngineState(state, engineState) {
  if (!engineState) {
    return;
  }

  if (engineState.v === 1 && applyPersistencePayload(state, engineState)) {
    return;
  }

  if (!state.sources) {
    state.sources = makeEmptySources();
  }

  state.stepCount = clamp(engineState.stepCount, 1, MAX_STEPS);
  state.laneCount = clamp(engineState.channelCount, 1, MAX_LANES);
  state.refreshSteps = clamp(engineState.refreshSteps, 1, state.stepCount);
  state.generationMode =
    engineState.generationMode === "per_channel" || engineState.generationMode === "static"
      ? engineState.generationMode
      : "stack";
  state.staticSource = clamp(engineState.staticSource ?? 0, 0, SOURCE_COUNT - 1);
  state.rate = normalizeRate(engineState.rate);
  state.swing = clamp(engineState.swing, 0, 100);
  state.velocityHumanize = clamp(engineState.velocityHumanize, 0, 100);
  state.timingHumanize = clamp(engineState.timingHumanize, 0, 100);
  if (engineState.deviceActive !== undefined) {
    state.deviceActive = toggleValue(engineState.deviceActive);
  }
  state.tempo = Math.max(20, Math.min(300, Number.parseFloat(engineState.tempo) || 120));
  state.phaseOffsetBeats = Number.parseFloat(engineState.phaseOffsetBeats) || 0;

  const channels = engineState.channels ?? engineState.lanes ?? [];
  for (let lane = 0; lane < Math.min(MAX_LANES, channels.length); lane += 1) {
    const row = channels[lane];
    if (Array.isArray(row)) {
      state.lanes[lane].label = String(row[0] ?? state.lanes[lane].label);
      state.lanes[lane].note = clamp(row[1], 0, 127);
      state.lanes[lane].lock = clamp(row[2], -1, SOURCE_COUNT - 1);
      state.lanes[lane].loopLength = clamp(row[3], 1, state.stepCount);
      if (row[4] !== undefined) {
        state.lanes[lane].playbackMode = normalizePlaybackMode(row[4]);
      }
    } else if (row && typeof row === "object") {
      if (row.label !== undefined) state.lanes[lane].label = String(row.label);
      if (row.note !== undefined) state.lanes[lane].note = clamp(row.note, 0, 127);
      if (row.lock !== undefined) state.lanes[lane].lock = clamp(row.lock, -1, SOURCE_COUNT - 1);
      if (row.loopLength !== undefined) {
        state.lanes[lane].loopLength = clamp(row.loopLength, 1, state.stepCount);
      }
      if (row.playbackMode !== undefined) {
        state.lanes[lane].playbackMode = normalizePlaybackMode(row.playbackMode);
      }
    }
    state.lanes[lane].loopLength = clamp(state.lanes[lane].loopLength, 1, state.stepCount);
  }

  const sourcesIn = engineState.sources;
  if (Array.isArray(sourcesIn)) {
    for (let source = 0; source < Math.min(SOURCE_COUNT, sourcesIn.length); source += 1) {
      const sourceLanes = sourcesIn[source] ?? [];
      for (let lane = 0; lane < Math.min(MAX_LANES, sourceLanes.length); lane += 1) {
        const laneSteps = sourceLanes[lane] ?? [];
        for (let step = 0; step < Math.min(MAX_STEPS, laneSteps.length); step += 1) {
          state.sources[source][lane][step] = cloneCell(laneSteps[step]);
        }
      }
    }
  }
}

export function applyStatusMessage(state, selector, args = []) {
  const values = [...args];

  switch (selector) {
    case "steps":
      state.stepCount = clamp(values[0], 1, MAX_STEPS);
      state.refreshSteps = clamp(state.refreshSteps, 1, state.stepCount);
      for (let lane = 0; lane < MAX_LANES; lane += 1) {
        state.lanes[lane].loopLength = clamp(state.lanes[lane].loopLength, 1, state.stepCount);
      }
      break;
    case "channels":
      state.laneCount = clamp(values[0], 1, MAX_LANES);
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
    case "phase_offset_beats":
      state.phaseOffsetBeats = Number.parseFloat(values[0]) || 0;
      break;
    case "tempo":
      state.tempo = Math.max(20, Math.min(300, Number.parseFloat(values[0]) || 120));
      break;
    default:
      break;
  }
}
