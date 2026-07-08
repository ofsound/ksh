export const uiDesignWidthPx = 1328;
export const uiDesignHeightPluginPx = 828;
export const uiDesignHeightStandalonePx = 872;
export const uiMinScale = 0.5;
export const uiMaxScale = 1;
export const uiMinScalePercent = 50;
export const uiMaxScalePercent = 100;
export const uiBaseFontSizePx = 16;
export const uiScaleStorageKey = "kshUiScalePreset";

const uiScaleMinimumSizeAnchors = [
  { percent: 50, widthPx: 664, pluginHeightPx: 439, standaloneHeightPx: 463 },
  { percent: 70, widthPx: 930, pluginHeightPx: 598, standaloneHeightPx: 627 },
  { percent: 80, widthPx: 1062, pluginHeightPx: 670, standaloneHeightPx: 701 },
  { percent: 90, widthPx: 1195, pluginHeightPx: 749, standaloneHeightPx: 786 },
  { percent: 100, widthPx: uiDesignWidthPx, pluginHeightPx: uiDesignHeightPluginPx, standaloneHeightPx: uiDesignHeightStandalonePx },
];

export const uiScaleState = $state({
  scale: 1,
  percent: 100,
  widthPx: uiDesignWidthPx,
  heightPx: uiDesignHeightPluginPx,
  standaloneTransportAvailable: false,
});

/** @param {number} value @param {number} min @param {number} max */
function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

/** @param {string | number | null | undefined} value */
export function normalizeUiScalePercent(value) {
  if (value === null || value === undefined || value === "") return uiMaxScalePercent;

  const parsed = Number(value);

  if (!Number.isFinite(parsed)) return uiMaxScalePercent;

  return clamp(Math.round(parsed), uiMinScalePercent, uiMaxScalePercent);
}

/**
 * @param {{ widthPx: number, heightPx: number, standaloneTransportAvailable?: boolean }} params
 */
export function computeUiScale({ widthPx, heightPx, standaloneTransportAvailable = false }) {
  void widthPx;
  void heightPx;
  void standaloneTransportAvailable;

  return uiScaleState.scale;
}

/** @param {number} scale */
export function applyUiScaleToDocument(scale) {
  if (typeof document === "undefined") return;

  const root = document.documentElement;
  root.style.fontSize = `${uiBaseFontSizePx * scale}px`;
  root.style.setProperty("--mp-ui-scale", String(scale));
}

export function storedUiScalePercent() {
  if (typeof localStorage === "undefined") return uiMaxScalePercent;

  return normalizeUiScalePercent(localStorage.getItem(uiScaleStorageKey));
}

/**
 * Startup scale: last UI choice in localStorage, otherwise 100%.
 * Project files apply their own scale when loaded explicitly.
 * @param {unknown} projectScaleFromJuce
 */
export function resolveInitialUiScalePercent(projectScaleFromJuce) {
  if (typeof localStorage !== "undefined") {
    const stored = localStorage.getItem(uiScaleStorageKey);

    if (stored !== null && stored !== "") {
      return normalizeUiScalePercent(stored);
    }
  }

  void projectScaleFromJuce;

  return uiMaxScalePercent;
}

/**
 * @param {string | number} percentValue
 * @param {{ persist?: boolean }} [options]
 */
export function setUiScalePercent(percentValue, { persist = true } = {}) {
  const percent = normalizeUiScalePercent(percentValue);
  const scale = clamp(percent / 100, uiMinScale, uiMaxScale);

  uiScaleState.percent = percent;
  uiScaleState.scale = scale;
  applyUiScaleToDocument(scale);

  if (persist && typeof localStorage !== "undefined") {
    localStorage.setItem(uiScaleStorageKey, String(percent));
  }

  return percent;
}

/** @param {number} percent @param {'widthPx' | 'pluginHeightPx' | 'standaloneHeightPx'} key */
function interpolatedMinimumSize(percent, key) {
  const upperIndex = uiScaleMinimumSizeAnchors.findIndex((anchor) => anchor.percent >= percent);

  if (upperIndex <= 0) return uiScaleMinimumSizeAnchors[0][key];

  const lower = uiScaleMinimumSizeAnchors[upperIndex - 1];
  const upper = uiScaleMinimumSizeAnchors[upperIndex];
  const progress = (percent - lower.percent) / (upper.percent - lower.percent);

  return Math.round(lower[key] + (upper[key] - lower[key]) * progress);
}

/** @param {{ standaloneTransportAvailable?: boolean }} [params] */
export function currentUiScaleMinimumSize({ standaloneTransportAvailable = uiScaleState.standaloneTransportAvailable } = {}) {
  const percent = normalizeUiScalePercent(uiScaleState.percent);

  return {
    widthPx: interpolatedMinimumSize(percent, "widthPx"),
    heightPx: interpolatedMinimumSize(
      percent,
      standaloneTransportAvailable ? "standaloneHeightPx" : "pluginHeightPx",
    ),
  };
}

/**
 * @param {{ widthPx: number, heightPx: number, standaloneTransportAvailable?: boolean }} params
 */
export function setUiViewportSize({ widthPx, heightPx, standaloneTransportAvailable = false }) {
  uiScaleState.widthPx = widthPx;
  uiScaleState.heightPx = heightPx;
  uiScaleState.standaloneTransportAvailable = standaloneTransportAvailable;

  return uiScaleState.scale;
}

/** @param {number} basePx */
export function scaledPx(basePx) {
  return basePx * uiScaleState.scale;
}
