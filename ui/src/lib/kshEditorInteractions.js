import {
  CYCLE_DRAG_SCALE,
  HEADER_VALUE_DRAG_SCALE,
  MAX_CYCLE,
  MAX_ROLL,
  MAX_STEPS,
  PROBABILITY_DRAG_SCALE,
  ROLL_DRAG_SCALE,
  SOURCE_PAINT_DRAG_THRESHOLD,
  VELOCITY_DRAG_SCALE,
  VELOCITY_DRAG_THRESHOLD,
  clamp,
} from "./kshConstants.js";
import {
  GRID_CELL_W,
  normalizeSourceLayerMode,
  normalizeSourceValueMode,
} from "./kshEditorUtils.js";
import { cloneCell } from "./kshUiState.js";

export function quantizedDragOffset(delta, scale, deadZone = VELOCITY_DRAG_THRESHOLD) {
  if (Math.abs(delta) <= deadZone) {
    return 0;
  }

  const sign = delta < 0 ? -1 : 1;
  const activeDelta = Math.abs(delta) - deadZone;
  return sign * Math.round(activeDelta / scale);
}

export function stepFromGridX(clientX, gridLeft, stepCount, cellWidth = GRID_CELL_W) {
  return clamp(Math.floor((clientX - gridLeft) / cellWidth), 0, stepCount - 1);
}

export function headerValueForState(state, id) {
  if (id === "steps") {
    return state.stepCount;
  }
  if (id === "refresh") {
    return state.refreshSteps;
  }
  if (id === "swing") {
    return state.swing;
  }
  if (id === "velocity_humanize") {
    return state.velocityHumanize;
  }
  if (id === "timing_humanize") {
    return state.timingHumanize;
  }
  return 0;
}

export function headerValueMin(id) {
  if (id === "swing" || id === "velocity_humanize" || id === "timing_humanize") {
    return 0;
  }
  return 1;
}

export function headerValueMax(state, id) {
  if (id === "steps") {
    return MAX_STEPS;
  }
  if (id === "refresh") {
    return state.stepCount;
  }
  return 100;
}

export function clampHeaderValue(state, id, value) {
  return clamp(value, headerValueMin(id), headerValueMax(state, id));
}

export function headerDragNextValue(drag, clientY) {
  const delta = drag.startY - clientY;
  return drag.startValue + quantizedDragOffset(delta, HEADER_VALUE_DRAG_SCALE);
}

export function loopDragNextValue(drag, clientY) {
  const delta = drag.startY - clientY;
  return drag.startValue + quantizedDragOffset(delta, HEADER_VALUE_DRAG_SCALE);
}

export function createCellDrag(source, channel, step, cell, layerMode, valueMode, clientX, clientY) {
  return {
    source,
    channel,
    step,
    startX: clientX,
    startY: clientY,
    startVelocity: cell.velocity,
    startProbability: cell.probability,
    startCycle: cell.cycle,
    startCycleOffset: cell.cycleOffset,
    startRoll: cell.roll,
    paintCell: cloneCell(cell),
    layerMode: normalizeSourceLayerMode(layerMode),
    valueMode: normalizeSourceValueMode(valueMode ?? layerMode),
    paintEnabled: cell.enabled ? 0 : 1,
    mode: null,
    moved: false,
  };
}

export function resolveCellDragMode(drag, clientX, clientY) {
  const dx = clientX - drag.startX;
  const dy = clientY - drag.startY;

  if (
    Math.abs(dx) >= SOURCE_PAINT_DRAG_THRESHOLD &&
    Math.abs(dx) > Math.abs(dy)
  ) {
    return "paint";
  }

  if (Math.abs(dy) >= VELOCITY_DRAG_THRESHOLD) {
    return "value";
  }

  return null;
}

export function applyPaintCellProperties(cell, paintCell) {
  let changed = false;

  if (cell.enabled !== 1) {
    cell.enabled = 1;
    changed = true;
  }
  if (cell.velocity !== paintCell.velocity) {
    cell.velocity = paintCell.velocity;
    changed = true;
  }
  if (cell.probability !== paintCell.probability) {
    cell.probability = paintCell.probability;
    changed = true;
  }
  if (cell.cycle !== paintCell.cycle) {
    cell.cycle = paintCell.cycle;
    changed = true;
  }
  if (cell.cycleOffset !== paintCell.cycleOffset) {
    cell.cycleOffset = paintCell.cycleOffset;
    changed = true;
  }
  if (cell.cycleInverted !== paintCell.cycleInverted) {
    cell.cycleInverted = paintCell.cycleInverted;
    changed = true;
  }
  if (cell.roll !== paintCell.roll) {
    cell.roll = paintCell.roll;
    changed = true;
  }

  return changed;
}

/** @returns {number[]} steps that changed */
export function applySourcePaintRange(state, source, drag, fromStep, toStep) {
  if (!drag || drag.mode !== "paint") {
    return [];
  }

  const lo = Math.min(fromStep, toStep);
  let hi = Math.max(fromStep, toStep);
  const loopLength = clamp(state.channels[drag.channel].loopLength, 1, state.stepCount);
  hi = Math.min(hi, loopLength - 1);
  const changedSteps = [];

  for (let step = lo; step <= hi; step += 1) {
    const cell = state.sources[source][drag.channel][step];
    if (drag.paintEnabled) {
      if (applyPaintCellProperties(cell, drag.paintCell)) {
        changedSteps.push(step);
      }
    } else if (cell.enabled !== 0) {
      cell.enabled = 0;
      changedSteps.push(step);
    }
  }

  return changedSteps;
}

export function applySourceValueDrag(state, source, drag, clientY) {
  if (!drag || drag.mode === "paint") {
    return false;
  }

  drag.moved = true;
  const cell = state.sources[source][drag.channel][drag.step];
  const enabledChanged = !cell.enabled;
  if (!cell.enabled) {
    cell.enabled = 1;
  }

  const valueMode = normalizeSourceValueMode(drag.valueMode);
  let startValue;
  let scale;
  let minValue;
  let maxValue;

  if (valueMode === "probability") {
    startValue = drag.startProbability;
    scale = PROBABILITY_DRAG_SCALE;
    minValue = 0;
    maxValue = 100;
  } else if (valueMode === "cycle_offset") {
    startValue = drag.startCycleOffset;
    scale = CYCLE_DRAG_SCALE;
    minValue = 0;
    maxValue = Math.max(0, cell.cycle - 1);
  } else if (valueMode === "cycle") {
    startValue = drag.startCycle;
    scale = CYCLE_DRAG_SCALE;
    minValue = 1;
    maxValue = MAX_CYCLE;
  } else if (valueMode === "roll") {
    startValue = drag.startRoll;
    scale = ROLL_DRAG_SCALE;
    minValue = 1;
    maxValue = MAX_ROLL;
  } else {
    startValue = drag.startVelocity;
    scale = VELOCITY_DRAG_SCALE;
    minValue = 1;
    maxValue = 127;
  }

  const delta = drag.startY - clientY;
  let nextValue = clamp(
    startValue + quantizedDragOffset(delta, scale),
    minValue,
    maxValue
  );

  if (valueMode === "cycle" && clientY - drag.startY > VELOCITY_DRAG_THRESHOLD) {
    nextValue = clamp(
      1 + quantizedDragOffset(clientY - drag.startY, scale),
      minValue,
      maxValue
    );

    let changed = false;

    if (cell.cycle !== nextValue) {
      cell.cycle = nextValue;
      changed = true;
    }
    if (cell.cycleInverted !== 1) {
      cell.cycleInverted = 1;
      changed = true;
    }
    if (cell.cycleOffset > cell.cycle - 1) {
      cell.cycleOffset = cell.cycle - 1;
      changed = true;
    }

    return changed || enabledChanged;
  }

  if (valueMode === "probability" && cell.probability !== nextValue) {
    cell.probability = nextValue;
    return true;
  }
  if (valueMode === "cycle_offset" && cell.cycleOffset !== nextValue) {
    cell.cycleOffset = nextValue;
    return true;
  }
  if (valueMode === "cycle" && cell.cycle !== nextValue) {
    cell.cycle = nextValue;
    if (cell.cycleOffset > cell.cycle - 1) {
      cell.cycleOffset = cell.cycle - 1;
    }
    if (delta > VELOCITY_DRAG_THRESHOLD) {
      cell.cycleInverted = 0;
    }
    return true;
  }
  if (valueMode === "cycle" && delta > VELOCITY_DRAG_THRESHOLD && cell.cycleInverted !== 0) {
    cell.cycleInverted = 0;
    return true;
  }
  if (valueMode === "roll" && cell.roll !== nextValue) {
    cell.roll = nextValue;
    return true;
  }
  if (valueMode === "velocity" && cell.velocity !== nextValue) {
    cell.velocity = nextValue;
    return true;
  }

  return enabledChanged;
}

export function toggleCellOnRelease(state, source, drag) {
  const cell = state.sources[source][drag.channel][drag.step];
  cell.enabled = cell.enabled ? 0 : 1;
  return cell.enabled === 0 ? null : drag;
}
