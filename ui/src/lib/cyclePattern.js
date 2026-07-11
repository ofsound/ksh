import { MAX_CYCLE, MAX_CYCLE_MASK_BITS, MAX_CYCLE_PATTERN_CELLS } from "./kshConstants.js";

export const minStepCycle = 1;
export const maxStepCycle = MAX_CYCLE;
export const maxCyclePatternCells = MAX_CYCLE_PATTERN_CELLS;
export const defaultStepCycle = 1;
export const defaultStepCycleMask = 1;

export function clampStepCycle(cycle) {
  return Math.min(maxStepCycle, Math.max(minStepCycle, Math.round(Number(cycle) || minStepCycle)));
}

export function clampEditorStepCycle(cycle) {
  return Math.min(maxCyclePatternCells, Math.max(minStepCycle, Math.round(Number(cycle) || minStepCycle)));
}

export function maskForCycleLength(cycle) {
  const bits = Math.min(clampStepCycle(cycle), MAX_CYCLE_MASK_BITS);
  return bits <= 0 ? defaultStepCycleMask : (2 ** bits) - 1;
}

export function clampStepCycleMask(mask, cycle) {
  const parsed = Math.round(Number(mask) || 0);
  return Math.max(0, parsed) & maskForCycleLength(cycle);
}

export function cycleMaskFromLegacyOffset(offset, cycle, inverted = false) {
  const length = clampStepCycle(cycle);
  const phase = Math.min(Math.max(0, Math.round(Number(offset) || 0)), length - 1);
  const oneHot = 2 ** Math.min(phase, MAX_CYCLE_MASK_BITS - 1);

  if (!inverted) {
    return oneHot;
  }

  return maskForCycleLength(length) & ~oneHot;
}

export function cycleGatePasses(triggerCount, cycle, mask) {
  const length = clampStepCycle(cycle);
  const pattern = clampStepCycleMask(mask, length) || defaultStepCycleMask;
  if (length <= 1) return (pattern & 1) !== 0;

  const phase = ((Math.round(triggerCount) % length) + length) % length;
  return (pattern & (2 ** Math.min(phase, MAX_CYCLE_MASK_BITS - 1))) !== 0;
}

export function normalizeCyclePattern(cycle, mask) {
  const nextCycle = clampStepCycle(cycle);
  let nextMask = clampStepCycleMask(mask, nextCycle);
  if (nextMask === 0) nextMask = defaultStepCycleMask;
  return { cycle: nextCycle, mask: nextMask };
}

export function normalizeEditorCyclePattern(cycle, mask) {
  const nextCycle = clampEditorStepCycle(cycle);
  let nextMask = clampStepCycleMask(mask, nextCycle);
  if (nextMask === 0) nextMask = defaultStepCycleMask;
  return { cycle: nextCycle, mask: nextMask };
}

export function isCycleCellActive(cycle, mask, index) {
  if (index < 0 || index >= clampEditorStepCycle(cycle)) return false;
  return (clampStepCycleMask(mask, cycle) & (2 ** index)) !== 0;
}

export function isCyclePositionActive(cycle, mask, index) {
  const length = clampStepCycle(cycle);
  if (index < 0 || index >= length || index >= MAX_CYCLE_MASK_BITS) return false;
  return (clampStepCycleMask(mask, length) & (2 ** index)) !== 0;
}

export function toggleCycleCell(cycle, mask, index) {
  const nextCycle = clampEditorStepCycle(cycle);
  if (index < 0 || index >= nextCycle) {
    return normalizeEditorCyclePattern(nextCycle, mask);
  }

  const nextMask = clampStepCycleMask(mask, nextCycle) ^ (2 ** index);
  return normalizeEditorCyclePattern(nextCycle, nextMask);
}

export function resizeCyclePattern(cycle, mask, nextCycle) {
  const normalized = normalizeEditorCyclePattern(cycle, mask);
  const length = clampEditorStepCycle(nextCycle);
  let nextMask = clampStepCycleMask(normalized.mask, length);
  if (nextMask === 0) nextMask = defaultStepCycleMask;
  return { cycle: length, mask: nextMask };
}

export function cycleLengthFromCellElements(clientX, cellElements) {
  for (let index = cellElements.length - 1; index >= 0; index -= 1) {
    const cell = cellElements[index];
    if (!cell) continue;
    if (clientX >= cell.getBoundingClientRect().left) {
      return clampEditorStepCycle(index + 1);
    }
  }
  return 1;
}
