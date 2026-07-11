import { SvelteSet } from "svelte/reactivity";
import { selectedCellLocations } from "./kshBulkEdit.js";
import { session } from "./kshSession.svelte.js";

/**
 * Shared move-mode selection so the compact strip inspector can follow
 * the same cells the grid highlights.
 */
export const cellSelection = $state({
  editMode: false,
  /** When false, edit mode keeps the diagonal selection cell look until an inspector control is touched. */
  inspectorLayerActive: false,
});

/** @type {SvelteSet<string>} */
export const selectedCellKeys = new SvelteSet();

export function clearEditSelection() {
  selectedCellKeys.clear();
}

export function setEditMode(next) {
  if (!next) {
    clearEditSelection();
  }
  cellSelection.editMode = Boolean(next);
  cellSelection.inspectorLayerActive = false;
}

export function toggleEditMode() {
  setEditMode(!cellSelection.editMode);
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
