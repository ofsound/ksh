import { clamp, MAX_ROLL } from "./kshConstants.js";
import { normalizeCyclePattern } from "./cyclePattern.js";
import { selectedCellLocations } from "./kshBulkEdit.js";

/**
 * @param {object} state
 * @param {number} source
 * @param {Iterable<string>} keys
 * @param {"velocity" | "probability" | "cycle" | "roll"} property
 * @returns {Map<string, number>}
 */
export function captureSelectedCellValues(state, source, keys, property) {
  const startValues = new Map();

  for (const location of selectedCellLocations(keys, state.channelCount, state.stepCount)) {
    const cell = state.sources[source][location.channel][location.step];
    startValues.set(location.key, cell[property]);
  }

  return startValues;
}

/**
 * Relative bipolar adjust: each selected cell becomes startValue + offset.
 *
 * @param {object} state
 * @param {number} source
 * @param {Map<string, number>} startValues
 * @param {"velocity" | "probability"} property
 * @param {number} offset
 * @returns {boolean}
 */
export function applyRelativeCellOffset(state, source, startValues, property, offset) {
  const minValue = property === "probability" ? 0 : 1;
  const maxValue = property === "probability" ? 100 : 127;
  let changed = false;

  for (const [key, startValue] of startValues) {
    const [channelText, stepText] = key.split(":");
    const channel = Number.parseInt(channelText ?? "-1", 10);
    const step = Number.parseInt(stepText ?? "-1", 10);
    if (channel < 0 || step < 0) {
      continue;
    }

    const cell = state.sources[source]?.[channel]?.[step];
    if (!cell) {
      continue;
    }

    const nextValue = clamp(startValue + offset, minValue, maxValue);
    if (cell[property] !== nextValue) {
      cell[property] = nextValue;
      changed = true;
    }
  }

  return changed;
}

/**
 * Destructive absolute write for cycle or roll on every selected cell.
 *
 * @param {object} state
 * @param {number} source
 * @param {Iterable<string>} keys
 * @param {"cycle" | "roll"} property
 * @param {number} value
 * @returns {boolean}
 */
export function applyAbsoluteCellMode(state, source, keys, property, value) {
  let changed = false;

  for (const location of selectedCellLocations(keys, state.channelCount, state.stepCount)) {
    const cell = state.sources[source][location.channel][location.step];

    if (property === "cycle") {
      const next = normalizeCyclePattern(value, 1);
      if (cell.cycle !== next.cycle || cell.cycleMask !== next.mask) {
        cell.cycle = next.cycle;
        cell.cycleMask = next.mask;
        changed = true;
      }
      continue;
    }

    const nextRoll = clamp(value, 1, MAX_ROLL);
    if (cell.roll !== nextRoll) {
      cell.roll = nextRoll;
      changed = true;
    }
  }

  return changed;
}

/**
 * @param {object} state
 * @param {number} source
 * @param {Iterable<string>} keys
 * @param {"velocity" | "probability" | "cycle" | "roll"} property
 * @returns {number | null} Shared value, or null when mixed / empty.
 */
export function commonSelectedCellValue(state, source, keys, property) {
  const locations = selectedCellLocations(keys, state.channelCount, state.stepCount);
  if (locations.length === 0) {
    return null;
  }

  const first = state.sources[source][locations[0].channel][locations[0].step][property];
  for (let index = 1; index < locations.length; index += 1) {
    const location = locations[index];
    if (state.sources[source][location.channel][location.step][property] !== first) {
      return null;
    }
  }

  return first;
}

/**
 * Group selected locations by channel for batched `sendCellsForChannel`.
 *
 * @param {Iterable<string>} keys
 * @param {number} channelCount
 * @param {number} stepCount
 * @returns {Map<number, number[]>}
 */
export function selectedStepsByChannel(keys, channelCount, stepCount) {
  /** @type {Map<number, number[]>} */
  const byChannel = new Map();

  for (const location of selectedCellLocations(keys, channelCount, stepCount)) {
    const steps = byChannel.get(location.channel);
    if (steps) {
      steps.push(location.step);
    } else {
      byChannel.set(location.channel, [location.step]);
    }
  }

  for (const steps of byChannel.values()) {
    steps.sort((left, right) => left - right);
  }

  return byChannel;
}
