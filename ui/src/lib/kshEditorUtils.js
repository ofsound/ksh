import {COMPACT_HEIGHT, MAX_CHANNELS, MAX_CYCLE, MAX_STEPS, RATES, SOURCE_COUNT, clamp, normalizePlaybackMode} from "./kshConstants.js";
import {cloneCell, defaultCell} from "./kshUiState.js";

export const GRID_CELL_W = 50;
export const GRID_CELL_H = 50;
export const GRID_ROW_GAP = 4;
export const GRID_GUTTER_PX = 12;
export const CHANNEL_LABEL_W = 64;
export const GRID_SIDEBAR_W = 320;
export const GRID_CELL_LEFT_GAP = 34;
export const GRID_ROW_CELL_LEFT_GAP = GRID_CELL_LEFT_GAP + 32;
/** Extra breathing room after the step grid in the editor viewport. */
export const EDITOR_GRID_TRAILING_GAP = 64;
/** Sidebar, pre-grid gap, and trailing editor space east of the step grid. */
export const EDITOR_GRID_LEADING_CHROME = GRID_SIDEBAR_W + GRID_ROW_CELL_LEFT_GAP + EDITOR_GRID_TRAILING_GAP;
export const STEP_LABEL_H = 18;
export const STEP_LABEL_CELL_GAP = 8;
export const STEP_LABEL_FONT_PX = 10;
export const GRID_VERTICAL_PADDING = 32;

export function stepLabelFontPx(scale = 1) {
  return Math.round(STEP_LABEL_FONT_PX * normalizePatternViewScale(scale));
}
export const EDITOR_MIN_WIDTH = 1328;
export const PLUGIN_MIN_HEIGHT = 756;
export const MAIN_TOP = 68;
export const PROJECT_ROW_H = 86;
export const STANDALONE_TRANSPORT_ROW_H = 44;
export const APP_FOOTER_H = 40;

export function standaloneTransportRowHeight(state) {
  return state?.standaloneTransportAvailable ? STANDALONE_TRANSPORT_ROW_H : 0;
}

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

export function stepLabelOuterMargin(scale = 1) {
  return STEP_LABEL_CELL_GAP + gridRowPaddingY(scale);
}

export function stepLabelBandHeight(scale = 1) {
  return stepLabelOuterMargin(scale) + STEP_LABEL_H + STEP_LABEL_CELL_GAP;
}

export function gridBodyHeight(channelCount, scale = 1) {
  return stepLabelBandHeight(scale) + channelCount * gridRowHeight(scale);
}

export function gridAreaHeight(editorHeight, state) {
  return editorHeight - MAIN_TOP - PROJECT_ROW_H - standaloneTransportRowHeight(state);
}

/** Minimum balanced spacer above and below the main grid body. */
export function referenceTopPadding(channelCount, state = null) {
  return GRID_VERTICAL_PADDING;
}

/** Minimum balanced spacer above and below the main grid body. */
export function referenceBottomPadding(channelCount, state = null) {
  return GRID_VERTICAL_PADDING;
}

/** Bottom spacer matched to the area above the step-number labels. */
export function gridCellPadding(channelCount, editorHeight, scale = 1, state = null) {
  const slack = gridAreaHeight(editorHeight, state) - gridBodyHeight(channelCount, scale);
  return Math.max(GRID_VERTICAL_PADDING, Math.floor(slack / 2));
}

/** Top spacer; step labels sit between this and the first cell row. */
export function gridTopPadding(channelCount, editorHeight, scale = 1, state = null) {
  const slack = gridAreaHeight(editorHeight, state) - gridBodyHeight(channelCount, scale);
  return Math.max(GRID_VERTICAL_PADDING, Math.floor(slack / 2));
}

export const COMPACT_ROW_H = 18;

export function compactPanelHeight() {
  return previewPanelHeight() + APP_FOOTER_H;
}

export function previewPanelHeight() {
  return COMPACT_HEIGHT;
}

export function compactPreviewHeight(channelCount) {
  return channelCount * COMPACT_ROW_H;
}

/** Equal top/bottom padding for the generated preview grid in the compact strip. */
export function compactPreviewPadding(channelCount) {
  const slack = COMPACT_HEIGHT - compactPreviewHeight(channelCount);
  return Math.max(0, Math.floor(slack / 2));
}

function channelColorToken(channel, dcColors = true) {
  if (!dcColors) {
    return "var(--color-drum-kick)";
  }

  // Use --theme-channel-* (defined on :root) rather than --color-channel-* (@theme
  // aliases that Tailwind omits when no utility class references them).
  return `var(--theme-channel-${(channel % 8) + 1})`;
}

function channelLightColorToken(channel, dcColors = true) {
  if (!dcColors) {
    return "var(--color-drum-kick-light)";
  }

  return `var(--theme-channel-${(channel % 8) + 1}-light)`;
}

export function channelColor(channel, light = false, dcColors = true) {
  return light
    ? channelLightColorToken(channel, dcColors)
    : channelColorToken(channel, dcColors);
}

export function channelToneColor(channel, tone = "light", dcColors = true) {
  const base = channelColorToken(channel, dcColors);

  if (tone === "dark") {
    return `color-mix(in srgb, ${base} 82%, var(--color-app))`;
  }
  if (tone === "divider") {
    return `color-mix(in srgb, ${base} 72%, transparent)`;
  }
  return channelLightColorToken(channel, dcColors);
}

export function mutedChannelColor(colorCss) {
  return `color-mix(in srgb, ${colorCss} 42%, var(--color-grid-off))`;
}

export function editorDimensions(state, scale = 1) {
  const gridW = state.stepCount * gridCellWidth(scale);
  const width = Math.max(EDITOR_MIN_WIDTH, EDITOR_GRID_LEADING_CHROME + gridW);
  const minEditorHeight = PLUGIN_MIN_HEIGHT - compactPanelHeight();
  const normalizedScale = normalizePatternViewScale(scale);
  const bodyHeight = gridBodyHeight(state.channelCount, normalizedScale);
  const standaloneRow = standaloneTransportRowHeight(state);
  const paddedGridHeight = MAIN_TOP + PROJECT_ROW_H + bodyHeight + GRID_VERTICAL_PADDING * 2;
  const height = Math.max(paddedGridHeight, minEditorHeight) + standaloneRow;
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
    return "Off";
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

export const PLAYBACK_MODE_OPTIONS = [
  { value: "normal", label: "Forward" },
  { value: "reverse", label: "Reverse" },
  { value: "ping_pong", label: "Ping-Pong" },
  { value: "reverse_ping_pong", label: "Reverse Ping-Pong" },
];

export function playbackModeOption(mode) {
  const normalized = normalizePlaybackMode(mode);
  return PLAYBACK_MODE_OPTIONS.find((option) => option.value === normalized) ?? PLAYBACK_MODE_OPTIONS[0];
}

export function playbackModeLabel(mode) {
  return playbackModeOption(mode).label;
}

export function playbackModeIsReversed(mode) {
  const normalized = normalizePlaybackMode(mode);
  return normalized === "reverse" || normalized === "reverse_ping_pong";
}

export function playbackModeIsPingPong(mode) {
  const normalized = normalizePlaybackMode(mode);
  return normalized === "ping_pong" || normalized === "reverse_ping_pong";
}

export function playbackModeFromFlags(reversed, pingPong) {
  if (pingPong) {
    return reversed ? "reverse_ping_pong" : "ping_pong";
  }
  return reversed ? "reverse" : "normal";
}

export function nextPlaybackMode(mode) {
  const normalized = normalizePlaybackMode(mode);
  if (normalized === "normal") {
    return "reverse";
  }
  if (normalized === "reverse") {
    return "ping_pong";
  }
  if (normalized === "ping_pong") {
    return "reverse_ping_pong";
  }
  return "normal";
}

const MIDI_NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];

export function midiNoteLabel(note) {
  const clamped = clamp(Math.round(note), 0, 127);
  const name = MIDI_NOTE_NAMES[clamped % 12];
  const octave = Math.floor(clamped / 12) - 1;
  return `${name}${octave}`;
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
  const start = loopStartForChannel(state, channel);
  const source = state.staticSource >= 0 && state.staticSource < SOURCE_COUNT ? state.staticSource : 0;
  const range = state.sourceSettings?.[source]?.loopRanges?.[channel];
  return clamp(range?.loopLength ?? state.channels[channel]?.loopLength ?? state.stepCount, 1, state.stepCount - start);
}

export function loopStartForChannel(state, channel) {
  const source = state.staticSource >= 0 && state.staticSource < SOURCE_COUNT ? state.staticSource : 0;
  const range = state.sourceSettings?.[source]?.loopRanges?.[channel];
  return clamp(range?.loopStart ?? state.channels[channel]?.loopStart ?? 0, 0, state.stepCount - 1);
}

export function loopEndForChannel(state, channel) {
  return loopStartForChannel(state, channel) + loopLengthForChannel(state, channel) - 1;
}

export function loopRangeForChannel(state, channel) {
  const start = loopStartForChannel(state, channel);
  const source = state.staticSource >= 0 && state.staticSource < SOURCE_COUNT ? state.staticSource : 0;
  const range = state.sourceSettings?.[source]?.loopRanges?.[channel];
  const length = clamp(range?.loopLength ?? state.channels[channel]?.loopLength ?? state.stepCount, 1, state.stepCount - start);
  return { start, length, end: start + length - 1 };
}

export function isStepBeyondLoopLength(state, channel, step) {
  const range = loopRangeForChannel(state, channel);
  return step < range.start || step > range.end;
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
    return "OFF";
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
    state.sourceSettings[source].loopRanges[channel].loopStart = 0;
    state.sourceSettings[source].loopRanges[channel].loopLength = state.sourceSettings[source].stepCount;
    if (state.staticSource === source) {
      state.channels[channel].loopStart = 0;
      state.channels[channel].loopLength = state.stepCount;
    }
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

  state.sourceSettings[destination] = {
    ...state.sourceSettings[source],
    loopRanges: state.sourceSettings[source].loopRanges.map((range) => ({ ...range })),
  };

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

export function modifierLayerMode(shiftKey, altKey) {
  if (altKey && shiftKey) {
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
