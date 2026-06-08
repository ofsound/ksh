import {COMPACT_HEIGHT, MAX_CHANNELS, MAX_CYCLE, MAX_STEPS, RATES, SOURCE_COUNT, clamp, normalizePlaybackMode} from "./kshConstants.js";
import {cloneCell, defaultCell} from "./kshUiState.js";

export const GRID_CELL_W = 50;
export const GRID_CELL_H = 44;
export const GRID_ROW_GAP = 4;
export const GRID_GUTTER_PX = 12;
export const CHANNEL_LABEL_W = 64;
export const GRID_SIDEBAR_W = 242;
export const GRID_CELL_LEFT_GAP = 8;
export const STEP_LABEL_H = 18;
export const STEP_LABEL_CELL_GAP = 8;
export const STEP_LABEL_FONT_PX = 10;

export function stepLabelFontPx(scale = 1) {
  return Math.round(STEP_LABEL_FONT_PX * normalizePatternViewScale(scale));
}
export const EDITOR_MIN_WIDTH = 1328;
export const PLUGIN_MIN_HEIGHT = 756;
export const MAIN_TOP = 68;
export const HELPER_FOOTER_H = 24;

export function normalizePatternViewScale(scale) {
  return Number(scale) === 1.5 ? 1.5 : 1;
}

export function gridCellWidth(scale = 1) {
  return Math.round(GRID_CELL_W * normalizePatternViewScale(scale));
}

export function gridCellHeight(scale = 1) {
  return Math.round(GRID_CELL_H * normalizePatternViewScale(scale));
}

export function gridRowGap(scale = 1) {
  return Math.round(GRID_ROW_GAP * normalizePatternViewScale(scale));
}

export function gridRowHeight(scale = 1) {
  return gridCellHeight(scale) + gridRowGap(scale);
}

export function gridRowPaddingY(scale = 1) {
  return gridRowGap(scale) / 2;
}

export function gridBodyHeight(channelCount, scale = 1) {
  return STEP_LABEL_H + STEP_LABEL_CELL_GAP + channelCount * gridRowHeight(scale);
}

export function gridAreaHeight(editorHeight) {
  return editorHeight - MAIN_TOP;
}

/** Top spacer at 1x for a channel count; 1.5x keeps this gap below the header rule. */
export function referenceTopPadding(channelCount) {
  const minEditorHeight = PLUGIN_MIN_HEIGHT - compactPanelHeight();
  const oneXBodyHeight = gridBodyHeight(channelCount, 1);
  const editorHeight = Math.max(MAIN_TOP + oneXBodyHeight, minEditorHeight);
  const slack = gridAreaHeight(editorHeight) - oneXBodyHeight;
  return Math.max(0, Math.floor((slack - STEP_LABEL_H) / 2));
}

/** Bottom spacer at 1x for a channel count; 1.5x keeps this gap above the compact strip. */
export function referenceBottomPadding(channelCount) {
  const minEditorHeight = PLUGIN_MIN_HEIGHT - compactPanelHeight();
  const oneXBodyHeight = gridBodyHeight(channelCount, 1);
  const editorHeight = Math.max(MAIN_TOP + oneXBodyHeight, minEditorHeight);
  const slack = gridAreaHeight(editorHeight) - oneXBodyHeight;
  return Math.max(0, Math.floor((slack + STEP_LABEL_H) / 2));
}

/** Bottom spacer so first/last cell rows sit equidistant from the header rule and grid-area bottom. */
export function gridCellPadding(channelCount, editorHeight, scale = 1) {
  const slack = gridAreaHeight(editorHeight) - gridBodyHeight(channelCount, scale);
  if (normalizePatternViewScale(scale) === 1.5) {
    const top = gridTopPadding(channelCount, editorHeight, scale);
    const distributed = Math.max(0, slack - top);
    return Math.max(referenceBottomPadding(channelCount), distributed);
  }
  return Math.max(0, Math.floor((slack + STEP_LABEL_H) / 2));
}

/** Top spacer; step labels sit between this and the first cell row. */
export function gridTopPadding(channelCount, editorHeight, scale = 1) {
  const slack = gridAreaHeight(editorHeight) - gridBodyHeight(channelCount, scale);
  const distributed = Math.max(0, Math.floor((slack - STEP_LABEL_H) / 2));
  if (normalizePatternViewScale(scale) === 1.5) {
    return Math.max(referenceTopPadding(channelCount), distributed);
  }
  return distributed;
}

export const COMPACT_ROW_H = 18;

export function compactPanelHeight() {
  return COMPACT_HEIGHT + HELPER_FOOTER_H;
}

export function compactPreviewHeight(channelCount) {
  return channelCount * COMPACT_ROW_H;
}

/** Equal top/bottom padding for the generated preview grid above the helper footer. */
export function compactPreviewPadding(channelCount) {
  const slack = COMPACT_HEIGHT - compactPreviewHeight(channelCount);
  return Math.max(0, Math.floor(slack / 2));
}

export const DC_CHANNEL_COLORS = [
  [0.86, 0.25, 0.28],
  [0.93, 0.55, 0.36],
  [0.82, 0.66, 0.4],
  [0.25, 0.68, 0.82],
  [0.32, 0.72, 0.61],
  [0.55, 0.72, 0.32],
  [0.92, 0.68, 0.14],
  [0.65, 0.43, 0.23],
];

export const DC_CHANNEL_COLORS_LIGHT = [
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

function mixRgb([r, g, b], [targetR, targetG, targetB], amount) {
  const keep = 1 - amount;
  return [r * keep + targetR * amount, g * keep + targetG * amount, b * keep + targetB * amount];
}

function channelColorRgb(channel, light = false, dcColors = true) {
  if (!dcColors) {
    return light ? [1.0, 0.66, 0.46] : [0.93, 0.55, 0.22];
  }

  const palette = light ? DC_CHANNEL_COLORS_LIGHT : DC_CHANNEL_COLORS;
  return palette[channel % palette.length];
}

export function channelColor(channel, light = false, dcColors = true) {
  return rgbaToCss(channelColorRgb(channel, light, dcColors));
}

export function channelToneColor(channel, tone = "light", dcColors = true) {
  if (tone === "dark") {
    return rgbaToCss(mixRgb(channelColorRgb(channel, false, dcColors), [0, 0, 0], 0.18));
  }
  if (tone === "divider") {
    return rgbaToCss(mixRgb(channelColorRgb(channel, false, dcColors), [0, 0, 0], 0.08), 0.72);
  }
  return rgbaToCss(channelColorRgb(channel, true, dcColors));
}

export function mutedChannelColor(colorCss) {
  return colorCss.replace(/rgba\((\d+),\s*(\d+),\s*(\d+),/, (_, r, g, b) => {
    const gray = (Number(r) + Number(g) + Number(b)) / 3;
    const mix = (channel) => Math.round(gray * 0.56 + Number(channel) * 0.12);
    return `rgba(${mix(r)}, ${mix(g)}, ${mix(b)},`;
  });
}

export function editorDimensions(state, scale = 1) {
  const gridW = state.stepCount * gridCellWidth(scale);
  const width = Math.max(EDITOR_MIN_WIDTH, 312 + gridW);
  const minEditorHeight = PLUGIN_MIN_HEIGHT - compactPanelHeight();
  const normalizedScale = normalizePatternViewScale(scale);
  const bodyHeight = gridBodyHeight(state.channelCount, normalizedScale);
  let height = Math.max(MAIN_TOP + bodyHeight, minEditorHeight);
  if (normalizedScale === 1.5) {
    height = Math.max(height, MAIN_TOP + bodyHeight + referenceTopPadding(state.channelCount) + referenceBottomPadding(state.channelCount));
  }
  return {width, height};
}

// The editor and the compact preview strip are stacked vertically and always
// visible, so the plugin window must accommodate both plus the gap between them.
export function combinedDimensions(state, scale = 1) {
  const editor = editorDimensions(state, scale);
  return {
    width: editor.width,
    height: editor.height + compactPanelHeight(),
  };
}

export function generationModeLabel(mode) {
  if (mode === "per_channel") {
    return "Per Channel";
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

export function normalizeSourceValueMode(mode) {
  if (mode === "cycle_offset") {
    return "cycle_offset";
  }
  return normalizeSourceLayerMode(mode);
}

export function cycleOffsetLabel(value) {
  const parsed = Number.parseInt(String(value), 10);
  if (Number.isNaN(parsed) || parsed < 0) {
    return "1";
  }
  return String(parsed + 1);
}

/** @returns {number} signed index: negative = inverted (!N), positive = normal (N); zero is unused */
export function cycleScrollIndex(cycle, cycleInverted) {
  const magnitude = clamp(Number(cycle) || 1, 1, MAX_CYCLE);
  return cycleInverted ? -magnitude : magnitude;
}

/** @returns {{ cycle: number, cycleInverted: number }} */
export function cycleStateFromScrollIndex(index) {
  const clamped = clamp(index, -MAX_CYCLE, MAX_CYCLE);
  if (clamped <= -1) {
    return { cycle: -clamped, cycleInverted: 1 };
  }
  if (clamped >= 1) {
    return { cycle: clamped, cycleInverted: 0 };
  }
  return { cycle: 1, cycleInverted: 0 };
}

/** Step along ... !3, !2, !1, 1, 2, 3 ... skipping the unused zero slot. */
export function stepCycleScrollIndex(index, steps) {
  if (steps === 0) {
    const state = cycleStateFromScrollIndex(index);
    return cycleScrollIndex(state.cycle, state.cycleInverted);
  }

  let current = index;
  const direction = steps > 0 ? 1 : -1;
  let remaining = Math.abs(steps);

  while (remaining > 0) {
    if (direction > 0) {
      if (current === -1) {
        current = 1;
      } else if (current >= MAX_CYCLE) {
        break;
      } else {
        current += 1;
      }
    } else if (current === 1) {
      current = -1;
    } else if (current <= -MAX_CYCLE) {
      break;
    } else {
      current -= 1;
    }
    remaining -= 1;
  }

  return current;
}

export function loopLengthForChannel(state, channel) {
  return clamp(state.channels[channel]?.loopLength ?? state.stepCount, 1, state.stepCount);
}

export function isStepBeyondLoopLength(state, channel, step) {
  return step >= loopLengthForChannel(state, channel);
}

/** @returns {"top_left"|"bottom_right"} */
export function resolveCellTriangle(localX, localY, cellWidth, cellHeight) {
  const lineY = cellHeight - (cellHeight * localX) / cellWidth;
  return localY <= lineY ? "top_left" : "bottom_right";
}

export function valueModeForCellInteraction(layerMode, triangle) {
  const normalizedLayer = normalizeSourceLayerMode(layerMode);
  if (normalizedLayer !== "cycle") {
    return normalizedLayer;
  }
  return triangle === "bottom_right" ? "cycle_offset" : "cycle";
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
  return "Velocity";
}

export function lockLabel(lock) {
  if (lock < 0) {
    return "R";
  }
  return String(lock + 1);
}

export function shiftSourceChannelRow(state, source, channel, direction) {
  if (state.stepCount <= 1) {
    return [];
  }

  const row = state.sources[source][channel];
  const shifted = [];

  for (let step = 0; step < state.stepCount; step += 1) {
    const fromStep = direction < 0 ? (step + 1) % state.stepCount : (step - 1 + state.stepCount) % state.stepCount;
    shifted[step] = cloneCell(row[fromStep]);
  }

  for (let step = 0; step < state.stepCount; step += 1) {
    row[step] = shifted[step];
  }

  return Array.from({length: state.stepCount}, (_, step) => step);
}

export function clearSourcePattern(state, source) {
  const affected = [];

  for (let channel = 0; channel < state.channelCount; channel += 1) {
    state.sourceChannelMutes[source][channel] = 0;
    state.channels[channel].loopLength = state.stepCount;
    state.channels[channel].lock = -1;
    state.channels[channel].playbackMode = "normal";

    for (let step = 0; step < MAX_STEPS; step += 1) {
      state.sources[source][channel][step] = defaultCell();
    }

    affected.push(channel);
  }

  return affected;
}

export function copySourcePattern(state, source, destination) {
  if (source === destination) {
    return false;
  }

  for (let channel = 0; channel < MAX_CHANNELS; channel += 1) {
    state.sourceChannelMutes[destination][channel] = state.sourceChannelMutes[source][channel] ? 1 : 0;

    for (let step = 0; step < MAX_STEPS; step += 1) {
      state.sources[destination][channel][step] = cloneCell(state.sources[source][channel][step]);
    }
  }

  return true;
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
