import {
  MAX_LANES,
  MAX_STEPS,
  RATES,
  SOURCE_COUNT,
  clamp,
  normalizePlaybackMode,
} from "./kshConstants.js";
import { cloneCell, defaultCell } from "./kshUiState.js";

export const GRID_CELL_W = 50;
export const GRID_CELL_H = 44;
export const EDITOR_MIN_WIDTH = 1160;
export const MAIN_TOP = 68;
export const FOOTER_H = 28;

export const DC_LANE_COLORS = [
  [0.86, 0.25, 0.28],
  [0.93, 0.55, 0.36],
  [0.82, 0.66, 0.4],
  [0.25, 0.68, 0.82],
  [0.32, 0.72, 0.61],
  [0.55, 0.72, 0.32],
  [0.92, 0.68, 0.14],
  [0.65, 0.43, 0.23],
];

export const DC_LANE_COLORS_LIGHT = [
  [0.96, 0.35, 0.38],
  [1.0, 0.65, 0.46],
  [0.92, 0.76, 0.5],
  [0.35, 0.78, 0.92],
  [0.42, 0.82, 0.71],
  [0.65, 0.82, 0.42],
  [1.0, 0.78, 0.24],
  [0.75, 0.53, 0.33],
];

export function rgbaToCss([r, g, b], alpha = 1) {
  const red = Math.round(r * 255);
  const green = Math.round(g * 255);
  const blue = Math.round(b * 255);
  return `rgba(${red}, ${green}, ${blue}, ${alpha})`;
}

export function laneColor(lane, light = false, dcColors = true) {
  if (!dcColors) {
    return light ? "#94c9f7" : "#f59e38";
  }

  const palette = light ? DC_LANE_COLORS_LIGHT : DC_LANE_COLORS;
  return rgbaToCss(palette[lane % palette.length]);
}

export function mutedLaneColor(colorCss) {
  return colorCss.replace(/rgba\((\d+),\s*(\d+),\s*(\d+),/, (_, r, g, b) => {
    const gray = (Number(r) + Number(g) + Number(b)) / 3;
    const mix = (channel) => Math.round(gray * 0.56 + Number(channel) * 0.12);
    return `rgba(${mix(r)}, ${mix(g)}, ${mix(b)},`;
  });
}

export function editorDimensions(state) {
  const gridW = state.stepCount * GRID_CELL_W;
  const width = Math.max(EDITOR_MIN_WIDTH, 280 + gridW);
  const sourceGridY0 = MAIN_TOP + 12 + 22 + 32;
  const height = sourceGridY0 + state.laneCount * GRID_CELL_H + 18 + 8 + FOOTER_H;
  return { width, height };
}

export function generationModeLabel(mode) {
  if (mode === "per_channel") {
    return "Per Lane";
  }
  if (mode === "static") {
    return "Static";
  }
  return "Stack";
}

export function cycleGenerationMode(state) {
  if (state.generationMode === "stack") {
    state.generationMode = "per_channel";
  } else if (state.generationMode === "per_channel") {
    state.generationMode = "static";
  } else {
    state.generationMode = "stack";
  }
}

export function cycleRate(state, direction = 1) {
  let index = RATES.indexOf(state.rate);
  if (index < 0) {
    index = 0;
  }

  index += direction;
  if (index < 0) {
    index = RATES.length - 1;
  } else if (index >= RATES.length) {
    index = 0;
  }

  state.rate = RATES[index];
}

export function playbackModeLabel(mode) {
  const normalized = normalizePlaybackMode(mode);
  if (normalized === "reverse") {
    return "R";
  }
  if (normalized === "boomerang") {
    return "B";
  }
  return "N";
}

export function nextPlaybackMode(mode) {
  const normalized = normalizePlaybackMode(mode);
  if (normalized === "normal") {
    return "reverse";
  }
  if (normalized === "reverse") {
    return "boomerang";
  }
  return "normal";
}

export function normalizeSourceLayerMode(mode) {
  const lower = String(mode ?? "velocity").toLowerCase();
  if (lower === "cycle" || lower === "probability" || lower === "roll") {
    return lower;
  }
  return "velocity";
}

export function sourceLayerValue(cell, mode) {
  const layer = normalizeSourceLayerMode(mode);
  if (layer === "cycle") {
    return cell.cycle;
  }
  if (layer === "probability") {
    return cell.probability;
  }
  if (layer === "roll") {
    return cell.roll;
  }
  return cell.velocity;
}

export function sourceLayerLabel(mode) {
  const layer = normalizeSourceLayerMode(mode);
  if (layer === "cycle") {
    return "Cycle";
  }
  if (layer === "probability") {
    return "Prob";
  }
  if (layer === "roll") {
    return "Roll";
  }
  return "Vel";
}

export function lockLabel(lock) {
  if (lock < 0) {
    return "R";
  }
  return String(lock + 1);
}

export function shiftSourceChannelRow(state, source, lane, direction) {
  if (state.stepCount <= 1) {
    return [];
  }

  const row = state.sources[source][lane];
  const shifted = [];

  for (let step = 0; step < state.stepCount; step += 1) {
    const fromStep =
      direction < 0
        ? (step + 1) % state.stepCount
        : (step - 1 + state.stepCount) % state.stepCount;
    shifted[step] = cloneCell(row[fromStep]);
  }

  for (let step = 0; step < state.stepCount; step += 1) {
    row[step] = shifted[step];
  }

  return Array.from({ length: state.stepCount }, (_, step) => step);
}

export function clearSourcePattern(state, source) {
  const affected = [];

  for (let lane = 0; lane < state.laneCount; lane += 1) {
    state.sourceChannelMutes[source][lane] = 0;
    state.lanes[lane].loopLength = state.stepCount;
    state.lanes[lane].lock = -1;
    state.lanes[lane].playbackMode = "normal";

    for (let step = 0; step < MAX_STEPS; step += 1) {
      state.sources[source][lane][step] = defaultCell();
    }

    affected.push(lane);
  }

  return affected;
}

export function cycleLayerMode(current) {
  const order = ["velocity", "probability", "cycle", "roll"];
  const index = order.indexOf(normalizeSourceLayerMode(current));
  return order[(index + 1) % order.length];
}

export function stepCountOptions() {
  return [4, 8, 12, 16, 24, 32];
}

export function nextStepCount(current) {
  const options = stepCountOptions();
  const index = options.indexOf(current);
  if (index < 0 || index >= options.length - 1) {
    return options[0];
  }
  return options[index + 1];
}

export function phaseOffsetMs(phaseOffsetBeats, tempo) {
  let bpm = Number.parseFloat(String(tempo));
  if (Number.isNaN(bpm) || bpm <= 0) {
    bpm = 120;
  }
  return Math.round(-phaseOffsetBeats * 60000 / bpm);
}

export function phaseOffsetBeatsFromMs(msEarly, tempo) {
  let bpm = Number.parseFloat(String(tempo));
  if (Number.isNaN(bpm) || bpm <= 0) {
    bpm = 120;
  }
  const clamped = clamp(msEarly, -80, 80);
  return -clamped * bpm / 60000;
}

export function modifierLayerMode(metaKey, shiftKey, altKey) {
  if (metaKey) {
    return "roll";
  }
  if (altKey) {
    return "probability";
  }
  if (shiftKey) {
    return "cycle";
  }
  return null;
}
