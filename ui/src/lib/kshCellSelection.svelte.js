import { SvelteSet } from "svelte/reactivity";
import { selectedCellLocations } from "./kshBulkEdit.js";
import { session } from "./kshSession.svelte.js";

/** @type {SvelteSet<string>} */
export const selectedCellKeys = new SvelteSet();

export function clearCellSelection() {
  selectedCellKeys.clear();
}

/**
 * @returns {{ channel: number, step: number, key: string }[]}
 */
export function currentSelectedCellLocations() {
  return selectedCellLocations(
    selectedCellKeys,
    session.kshState.channelCount,
    session.kshState.stepCount,
  );
}

/**
 * The inspector operates only on selected cells that are currently enabled.
 * Selection itself remains position-based so MOVE mode can still include empty
 * destinations and preserve its existing behavior.
 */
export function currentSelectedEnabledCellLocations() {
  if (session.selectedSource < 0) {
    return [];
  }

  return currentSelectedCellLocations().filter(({ channel, step }) =>
    Boolean(session.kshState.sources[session.selectedSource]?.[channel]?.[step]?.enabled)
  );
}
