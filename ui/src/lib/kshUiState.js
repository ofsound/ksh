import {
  DEFAULT_CHANNEL_COUNT,
  DEFAULT_CHANNEL_LABELS,
  DEFAULT_CHANNEL_NOTES,
  DEFAULT_SWING_SUBDIVISION_INDEX,
  MAX_CHANNELS,
  MAX_STEPS,
  SILENT_SOURCE,
  SOURCE_COUNT,
  clamp,
  clampSwingSubdivisionIndex,
  normalizePlaybackMode,
  normalizeRate,
  toggleValue,
} from "./kshConstants.js";
import {
  cycleMaskFromLegacyOffset,
  normalizeCyclePattern,
} from "./cyclePattern.js";

export function defaultCell() {
  return {
    enabled: 0,
    velocity: 100,
    probability: 100,
    cycle: 1,
    cycleMask: 1,
    roll: 1,
    source: 0,
  };
}

export function cloneCell(cell) {
  return {
    enabled: cell?.enabled ? 1 : 0,
    velocity: clamp(cell?.velocity ?? 100, 1, 127),
    probability: clamp(cell?.probability ?? 100, 0, 100),
    ...normalizeCyclePattern(cell?.cycle ?? 1, cell?.cycleMask ?? cycleMaskFromLegacyOffset(
      cell?.cycleOffset ?? 0,
      cell?.cycle ?? 1,
      cell?.cycleInverted,
    )),
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

export function defaultSourceSettings() {
  return {
    stepCount: 16,
    rate: "16n",
    loopRanges: Array.from({ length: MAX_CHANNELS }, () => ({ loopStart: 0, loopLength: 16 })),
  };
}

export function makeSourceSettings() {
  return Array.from({ length: SOURCE_COUNT }, () => defaultSourceSettings());
}

export function makeDefaultKshState() {
  const channels = Array.from({ length: MAX_CHANNELS }, (_, index) => ({
    label: DEFAULT_CHANNEL_LABELS[index] ?? String(index + 1),
    note: DEFAULT_CHANNEL_NOTES[index] ?? 36 + index,
    lock: -1,
    loopStart: 0,
    loopLength: 16,
    playbackMode: "normal",
  }));

  return {
    stepCount: 16,
    channelCount: DEFAULT_CHANNEL_COUNT,
    refreshSteps: 16,
    generationMode: "static",
    staticSource: 0,
    rate: "16n",
    swing: 0,
    swingSubdivisionIndex: DEFAULT_SWING_SUBDIVISION_INDEX,
    velocityHumanize: 0,
    timingHumanize: 0,
    deviceActive: 1,
    tempo: 120,
    standaloneTransportAvailable: 0,
    standaloneTransportPlaying: 0,
    standaloneTempo: 120,
    channels,
    sources: makeEmptySources(),
    sourceSettings: makeSourceSettings(),
    sourceChannelMutes: makeSourceChannelMutes(),
  };
}

/** Alias kept for existing imports. */
export const makeDefaultCompactState = makeDefaultKshState;

export function resizeChannelLoopLengths(state, nextStepCount, previousStepCount = state.stepCount) {
  const source = state.staticSource >= 0 && state.staticSource < SOURCE_COUNT ? state.staticSource : 0;
  const settings = state.sourceSettings[source];
  for (let channel = 0; channel < MAX_CHANNELS; channel += 1) {
    const range = settings.loopRanges[channel];
    if ((range.loopStart ?? 0) === 0 && range.loopLength === previousStepCount) {
      range.loopLength = nextStepCount;
    }
    range.loopStart = clamp(range.loopStart ?? 0, 0, nextStepCount - 1);
    range.loopLength = clamp(range.loopLength, 1, nextStepCount - range.loopStart);
    state.channels[channel].loopStart = range.loopStart;
    state.channels[channel].loopLength = range.loopLength;
  }
}

export function serializePersistenceState(state) {
  const cells = [];

  for (let source = 0; source < SOURCE_COUNT; source += 1) {
    for (let channel = 0; channel < MAX_CHANNELS; channel += 1) {
      for (let step = 0; step < MAX_STEPS; step += 1) {
        const cell = state.sources[source][channel][step];
        if (
          !cell.enabled &&
          cell.velocity === 100 &&
          cell.probability === 100 &&
          cell.cycle === 1 &&
          cell.cycleMask === 1 &&
          cell.roll === 1
        ) {
          continue;
        }

        cells.push([
          source,
          channel,
          step,
          cell.enabled ? 1 : 0,
          cell.velocity,
          cell.probability,
          cell.cycle,
          cell.cycleMask,
          cell.roll,
        ]);
      }
    }
  }

  return {
    v: 1,
    stepCount: state.stepCount,
    channelCount: state.channelCount,
    refreshSteps: state.refreshSteps,
    generationMode: state.generationMode,
    staticSource: state.staticSource,
    rate: state.rate,
    tempo: state.tempo,
    swing: state.swing,
    swingSubdivisionIndex: state.swingSubdivisionIndex,
    velocityHumanize: state.velocityHumanize,
    timingHumanize: state.timingHumanize,
    deviceActive: state.deviceActive ? 1 : 0,
    channels: state.channels.map((channel) => [
      channel.label,
      channel.note,
      channel.lock,
      channel.loopLength,
      channel.playbackMode,
      channel.loopStart ?? 0,
    ]),
    sourceSettings: state.sourceSettings.map((settings) => {
      const stepCount = clamp(settings.stepCount, 1, MAX_STEPS);
      const loopRanges = settings.loopRanges.map((range) => {
        const loopStart = clamp(range.loopStart ?? 0, 0, stepCount - 1);
        const loopLength = clamp(range.loopLength ?? stepCount, 1, stepCount - loopStart);
        return [loopStart, loopLength];
      });
      return [stepCount, normalizeRate(settings.rate), loopRanges];
    }),
    sourceChannelMutes: state.sourceChannelMutes.map((row) => row.map((muted) => (muted ? 1 : 0))),
    cells,
  };
}

export function applyPersistencePayload(state, payload) {
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
  state.staticSource = clamp(payload.staticSource ?? 0, SILENT_SOURCE, SOURCE_COUNT - 1);
  state.rate = normalizeRate(payload.rate);
  state.swing = clamp(payload.swing, 0, 100);
  state.swingSubdivisionIndex = clampSwingSubdivisionIndex(
    payload.swingSubdivisionIndex ?? DEFAULT_SWING_SUBDIVISION_INDEX,
  );
  state.velocityHumanize = clamp(payload.velocityHumanize, 0, 100);
  state.timingHumanize = clamp(payload.timingHumanize, 0, 100);
  if (payload.deviceActive !== undefined) {
    state.deviceActive = toggleValue(payload.deviceActive);
  }
  state.tempo = Math.max(20, Math.min(300, Number.parseFloat(payload.tempo) || 120));
  if (payload.standaloneTransportAvailable !== undefined) {
    state.standaloneTransportAvailable = toggleValue(payload.standaloneTransportAvailable);
  }
  if (payload.standaloneTransportPlaying !== undefined) {
    state.standaloneTransportPlaying = toggleValue(payload.standaloneTransportPlaying);
  }
  if (payload.standaloneTempo !== undefined) {
    state.standaloneTempo = Math.max(
      20,
      Math.min(300, Number.parseFloat(payload.standaloneTempo) || state.tempo)
    );
  }

  if (!state.sources) {
    state.sources = makeEmptySources();
  }

  state.sourceSettings = makeSourceSettings();
  const settingsIn = payload.sourceSettings ?? [];
  for (let source = 0; source < SOURCE_COUNT; source += 1) {
    const row = settingsIn[source];
    let stepCount = state.stepCount;
    let rate = state.rate;

    if (Array.isArray(row)) {
      stepCount = row[0] ?? stepCount;
      rate = row[1] ?? rate;
    } else if (row && typeof row === "object") {
      stepCount = row.stepCount ?? stepCount;
      rate = row.rate ?? rate;
    }

    state.sourceSettings[source] = {
      stepCount: clamp(stepCount, 1, MAX_STEPS),
      rate: normalizeRate(rate),
      loopRanges: Array.from({ length: MAX_CHANNELS }, () => ({ loopStart: 0, loopLength: 16 })),
    };

    const loopRangesIn = Array.isArray(row) ? row[2] : row?.loopRanges;
    if (Array.isArray(loopRangesIn)) {
      for (let channel = 0; channel < MAX_CHANNELS; channel += 1) {
        const rangeIn = loopRangesIn[channel];
        if (!Array.isArray(rangeIn)) {
          continue;
        }
        const range = state.sourceSettings[source].loopRanges[channel];
        range.loopStart = clamp(rangeIn[0] ?? 0, 0, state.sourceSettings[source].stepCount - 1);
        range.loopLength = clamp(
          rangeIn[1] ?? state.sourceSettings[source].stepCount,
          1,
          state.sourceSettings[source].stepCount - range.loopStart
        );
      }
    }
  }

  if (state.staticSource >= 0 && state.staticSource < SOURCE_COUNT) {
    state.stepCount = state.sourceSettings[state.staticSource].stepCount;
    state.rate = state.sourceSettings[state.staticSource].rate;
    state.refreshSteps = clamp(state.refreshSteps, 1, state.stepCount);
  }

  // Only wipe/rebuild cells when the payload includes a real cells array.
  // Missing or malformed `cells` (e.g. lost in a bridge round-trip) must not
  // blank the grid — applyPersistencePayload always resets before repopulating.
  const cellsIn = Array.isArray(payload.cells) ? payload.cells : null;
  if (cellsIn) {
    for (let source = 0; source < SOURCE_COUNT; source += 1) {
      for (let channel = 0; channel < MAX_CHANNELS; channel += 1) {
        for (let step = 0; step < MAX_STEPS; step += 1) {
          state.sources[source][channel][step] = defaultCell();
        }
      }
    }
  }

  const channelsIn = payload.channels ?? [];
  for (let channel = 0; channel < MAX_CHANNELS; channel += 1) {
    if (channelsIn[channel]) {
      const row = channelsIn[channel];
      if (Array.isArray(row)) {
        state.channels[channel].label = String(row[0] ?? state.channels[channel].label);
        state.channels[channel].note = clamp(row[1], 0, 127);
        state.channels[channel].lock = clamp(row[2], -1, SOURCE_COUNT - 1);
        state.channels[channel].loopLength = clamp(row[3], 1, state.stepCount);
        if (row[4] !== undefined) {
          state.channels[channel].playbackMode = normalizePlaybackMode(row[4]);
        }
        state.channels[channel].loopStart = clamp(row[5] ?? 0, 0, state.stepCount - 1);
      } else if (row && typeof row === "object") {
        state.channels[channel].label = String(row.label ?? state.channels[channel].label);
        state.channels[channel].note = clamp(row.note, 0, 127);
        state.channels[channel].lock = clamp(row.lock, -1, SOURCE_COUNT - 1);
        state.channels[channel].loopStart = clamp(row.loopStart ?? 0, 0, state.stepCount - 1);
        state.channels[channel].loopLength = clamp(row.loopLength, 1, state.stepCount - state.channels[channel].loopStart);
        state.channels[channel].playbackMode = normalizePlaybackMode(row.playbackMode);
      }
    }
    state.channels[channel].loopStart = clamp(state.channels[channel].loopStart ?? 0, 0, state.stepCount - 1);
    state.channels[channel].loopLength = clamp(
      state.channels[channel].loopLength,
      1,
      state.stepCount - state.channels[channel].loopStart
    );

    const hasSourceRanges = Array.isArray(channelsIn[channel])
      ? state.sourceSettings.some((settings, source) => {
          const sourceRow = (payload.sourceSettings ?? [])[source];
          return Array.isArray(sourceRow) ? sourceRow.length > 2 : sourceRow?.loopRanges !== undefined;
        })
      : false;
    if (!hasSourceRanges) {
      for (const settings of state.sourceSettings) {
        settings.loopRanges[channel].loopStart = state.channels[channel].loopStart;
        settings.loopRanges[channel].loopLength = clamp(
          state.channels[channel].loopLength,
          1,
          settings.stepCount - settings.loopRanges[channel].loopStart
        );
      }
    }
  }

  state.sourceChannelMutes = makeSourceChannelMutes();
  const mutesIn = payload.sourceChannelMutes ?? [];
  for (let source = 0; source < SOURCE_COUNT; source += 1) {
    const muteRow = mutesIn[source] ?? [];
    for (let channel = 0; channel < MAX_CHANNELS; channel += 1) {
      state.sourceChannelMutes[source][channel] = muteRow[channel] ? 1 : 0;
    }
  }

  if (cellsIn) {
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

      const isLegacyEntry = entry.length >= 10;
      state.sources[source][channel][step] = cloneCell({
        enabled: entry[3],
        velocity: entry[4],
        probability: entry[5],
        cycle: entry[6],
        cycleMask: isLegacyEntry
          ? cycleMaskFromLegacyOffset(entry[7], entry[6], entry[8])
          : entry[7],
        roll: isLegacyEntry ? entry[9] : entry[8],
      });
    }
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
      if (state.staticSource >= 0 && state.staticSource < SOURCE_COUNT) {
        state.sourceSettings[state.staticSource].stepCount = state.stepCount;
      }
      state.refreshSteps = clamp(state.refreshSteps, 1, state.stepCount);
      resizeChannelLoopLengths(state, state.stepCount, previousStepCount);
      break;
    }
    case "source_steps":
    {
      const source = clamp(values[0] - 1, 0, SOURCE_COUNT - 1);
      const previousStepCount = state.stepCount;
      state.sourceSettings[source].stepCount = clamp(values[1], 1, MAX_STEPS);
      if (source === state.staticSource) {
        state.stepCount = state.sourceSettings[source].stepCount;
        state.refreshSteps = clamp(state.refreshSteps, 1, state.stepCount);
        resizeChannelLoopLengths(state, state.stepCount, previousStepCount);
      }
      break;
    }
    case "channels":
      state.channelCount = clamp(values[0], 1, MAX_CHANNELS);
      break;
    case "refresh_steps":
      state.refreshSteps = clamp(values[0], 1, state.stepCount);
      break;
    case "channel_loop_length":
    {
      const channel = clamp(values[0] - 1, 0, MAX_CHANNELS - 1);
      const source = state.staticSource >= 0 && state.staticSource < SOURCE_COUNT ? state.staticSource : 0;
      const range = state.sourceSettings[source].loopRanges[channel];
      range.loopStart = clamp(values[2] === undefined ? range.loopStart : values[2] - 1, 0, state.stepCount - 1);
      range.loopLength = clamp(values[1], 1, state.stepCount - range.loopStart);
      state.channels[channel].loopStart = range.loopStart;
      state.channels[channel].loopLength = range.loopLength;
      break;
    }
    case "mode":
      state.generationMode =
        String(values[0]) === "per_channel" || String(values[0]) === "static"
          ? String(values[0])
          : "stack";
      break;
    case "static_source":
      state.staticSource =
        String(values[0]).toLowerCase() === "m"
          ? SILENT_SOURCE
          : clamp(values[0] - 1, SILENT_SOURCE, SOURCE_COUNT - 1);
      if (state.staticSource >= 0 && state.staticSource < SOURCE_COUNT) {
        state.stepCount = state.sourceSettings[state.staticSource].stepCount;
        state.rate = state.sourceSettings[state.staticSource].rate;
        state.refreshSteps = clamp(state.refreshSteps, 1, state.stepCount);
        for (let channel = 0; channel < MAX_CHANNELS; channel += 1) {
          const range = state.sourceSettings[state.staticSource].loopRanges[channel];
          range.loopStart = clamp(range.loopStart, 0, state.stepCount - 1);
          range.loopLength = clamp(range.loopLength, 1, state.stepCount - range.loopStart);
          state.channels[channel].loopStart = range.loopStart;
          state.channels[channel].loopLength = range.loopLength;
        }
      }
      break;
    case "rate":
      state.rate = normalizeRate(values[0]);
      if (state.staticSource >= 0 && state.staticSource < SOURCE_COUNT) {
        state.sourceSettings[state.staticSource].rate = state.rate;
      }
      break;
    case "source_rate":
    {
      const source = clamp(values[0] - 1, 0, SOURCE_COUNT - 1);
      state.sourceSettings[source].rate = normalizeRate(values[1]);
      if (source === state.staticSource) {
        state.rate = state.sourceSettings[source].rate;
      }
      break;
    }
    case "swing":
      state.swing = clamp(values[0], 0, 100);
      break;
    case "swing_subdivision":
      state.swingSubdivisionIndex = clampSwingSubdivisionIndex(values[0]);
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
