export const themeModes = ["light", "dark", "alt"];
export const defaultThemeMode = "dark";

/** @type {Record<(typeof themeModes)[number], string>} */
export const themeModeTitles = {
  light: "Light theme",
  dark: "Dark theme",
  alt: "Alt theme",
};

const themeModeStorageKey = "ksh-theme-mode";

/** @param {unknown} value */
export function isThemeMode(value) {
  return typeof value === "string" && themeModes.includes(value);
}

/** @param {unknown} current */
export function nextThemeMode(current) {
  const mode = isThemeMode(current) ? current : defaultThemeMode;
  const index = themeModes.indexOf(mode);
  return themeModes[(index + 1) % themeModes.length];
}

export function storedThemeMode() {
  try {
    const stored = localStorage.getItem(themeModeStorageKey);
    return isThemeMode(stored) ? stored : defaultThemeMode;
  } catch {
    return defaultThemeMode;
  }
}

/**
 * @param {unknown} value
 * @param {{ persist?: boolean }} [options]
 */
export function applyThemeMode(value, options = {}) {
  const next = isThemeMode(value) ? value : defaultThemeMode;
  document.documentElement.setAttribute("data-theme", next);

  if (options.persist ?? true) {
    try {
      localStorage.setItem(themeModeStorageKey, next);
    } catch {
      // Local storage is optional in embedded WebViews.
    }
  }

  return next;
}
