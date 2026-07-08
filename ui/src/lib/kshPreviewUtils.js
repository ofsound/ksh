import { SILENT_SOURCE, SOURCE_COUNT, clamp } from "./kshConstants.js";
import { cloneCell, defaultCell } from "./kshUiState.js";

function mod(value, divisor) {
  return ((value % divisor) + divisor) % divisor;
}

function previewSourceIndex(cell) {
  const source = Number(cell?.source ?? 1);
  if (source <= 0) {
    return SILENT_SOURCE;
  }
  return clamp(source - 1, 0, SOURCE_COUNT - 1);
}

function isSourceEmpty(state, source) {
  for (let channel = 0; channel < state.channelCount; channel += 1) {
    if (state.sourceChannelMutes[source][channel]) {
      continue;
    }

    const loopLength = clamp(state.channels[channel].loopLength, 1, state.stepCount);
    for (let step = 0; step < Math.min(state.stepCount, loopLength); step += 1) {
      if (state.sources[source][channel][step].enabled) {
        return false;
      }
    }
  }

  return true;
}

function activeSourceIndices(state) {
  const indices = [];

  for (let source = 0; source < SOURCE_COUNT; source += 1) {
    if (!isSourceEmpty(state, source)) {
      indices.push(source);
    }
  }

  return indices;
}

function pickFirstActiveSource(activeSources) {
  return activeSources.length > 0 ? activeSources[0] : 0;
}

function generatedCellFromSource(state, source, channel, step) {
  if (source === SILENT_SOURCE) {
    return {
      ...defaultCell(),
      source: SILENT_SOURCE,
    };
  }

  const loopLength = clamp(state.channels[channel].loopLength, 1, state.stepCount);
  const sourceStep = mod(step, loopLength);

  if (state.sourceChannelMutes[source][channel]) {
    return {
      ...defaultCell(),
      source: source + 1,
    };
  }

  return {
    ...cloneCell(state.sources[source][channel][sourceStep]),
    source: source + 1,
  };
}

export function recomposePreviewGenerated(state, previousGenerated) {
  const { channelCount, stepCount, generationMode, staticSource } = state;
  const activeSources = generationMode === "static" ? [] : activeSourceIndices(state);
  let stackSource = -1;
  const generated = [];

  for (let channel = 0; channel < channelCount; channel += 1) {
    const row = [];

    for (let step = 0; step < stepCount; step += 1) {
      let source = 0;

      if (generationMode === "static") {
        source = staticSource;
      } else if (state.channels[channel].lock >= 0) {
        source = state.channels[channel].lock;
      } else {
        const existing = previewSourceIndex(previousGenerated?.[channel]?.[step]);

        if (existing >= 0 && existing < SOURCE_COUNT) {
          source = existing;
        } else if (generationMode === "per_channel") {
          source = pickFirstActiveSource(activeSources);
        } else {
          if (stackSource < 0) {
            stackSource = pickFirstActiveSource(activeSources);
          }

          source = stackSource;
        }
      }

      row.push(generatedCellFromSource(state, source, channel, step));
    }

    generated.push(row);
  }

  return generated;
}

export function applyRecomposedPreview(state, previewData) {
  if (!previewData) {
    return previewData;
  }

  return {
    ...previewData,
    generated: recomposePreviewGenerated(state, previewData.generated),
  };
}
