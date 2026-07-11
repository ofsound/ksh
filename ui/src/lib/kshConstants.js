export const MAX_STEPS = 32;
export const MAX_CHANNELS = 8;
export const MAX_CYCLE_MASK_BITS = 30;
export const MAX_CYCLE_PATTERN_CELLS = 8;
export const SILENT_SOURCE = -1;
export const SOURCE_COUNT = 16;

export const COMPACT_HEIGHT = 200;

export const EXPORT_BAR_VALUES = [1, 2, 4, 8, 16, 32];
export const NOTE_HIT_FLASH_MS = 80;
export const CHANNEL_RENAME_MS = 450;
/** Max gap between mute-button taps to reset a channel row (tighter than rename). */
export const SOURCE_ROW_RESET_MS = 220;

export const MAX_ROLL = 8;
export const MAX_CYCLE = 64;

export const SWING_SUBDIVISION_VALUES = [0.25, 0.5, 1];
export const SWING_SUBDIVISION_OPTIONS = [
  { index: 0, label: ".25" },
  { index: 1, label: ".5" },
  { index: 2, label: "1" },
];
export const DEFAULT_SWING_SUBDIVISION_INDEX = 1;

export const VELOCITY_DRAG_THRESHOLD = 4;
export const SOURCE_PAINT_DRAG_THRESHOLD = 4;
export const HEADER_VALUE_DRAG_SCALE = 4;
export const VELOCITY_DRAG_SCALE = 2;
export const PROBABILITY_DRAG_SCALE = 2;
export const CYCLE_DRAG_SCALE = 8;
export const ROLL_DRAG_SCALE = 8;

export const DEFAULT_CHANNEL_COUNT = 8;
export const DEFAULT_CHANNEL_LABELS = ["1", "2", "3", "4", "5", "6", "7", "8"];
export const DEFAULT_CHANNEL_NOTES = [36, 37, 38, 39, 40, 41, 42, 43];

export const RATES = ["4n", "4nt", "8n", "8nt", "16n", "16nt", "32n", "32nt"];

export function clamp(value, min, max) {
  const n = Number.parseInt(String(value), 10);
  if (Number.isNaN(n)) {
    return min;
  }
  return Math.max(min, Math.min(max, n));
}

export function clampSwingSubdivisionIndex(value) {
  return clamp(value, 0, SWING_SUBDIVISION_VALUES.length - 1);
}

export function normalizeRate(rate) {
  const text = String(rate ?? "16n");
  return RATES.includes(text) ? text : "16n";
}

export function normalizePlaybackMode(mode) {
  const lower = String(mode ?? "normal").toLowerCase();
  if (lower === "r" || lower === "rev" || lower === "reverse") {
    return "reverse";
  }
  if (
    lower === "rp" ||
    lower === "reverse_ping_pong" ||
    lower === "reverse-ping-pong" ||
    lower === "reverse_ping-pong" ||
    lower === "rev_ping_pong" ||
    lower === "rev-ping-pong"
  ) {
    return "reverse_ping_pong";
  }
  if (
    lower === "p" ||
    lower === "ping" ||
    lower === "ping-pong" ||
    lower === "ping_pong" ||
    lower === "b" ||
    lower === "boom" ||
    lower === "boomerang"
  ) {
    return "ping_pong";
  }
  return "normal";
}

export function toggleValue(value) {
  const text = String(value).toLowerCase();
  return text === "0" || text === "false" || text === "off" ? 0 : 1;
}
