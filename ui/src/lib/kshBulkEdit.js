/**
 * Selection keys are deliberately position based. KSH has a fixed grid, so a
 * cell remains addressable while a bulk gesture is in progress.
 */
export function cellSelectionKey(channel, step) {
  return `${channel}:${step}`;
}

/**
 * @param {string} key
 * @returns {{ channel: number, step: number } | null}
 */
export function cellLocationFromSelectionKey(key) {
  const [channelText, stepText] = String(key).split(":");
  const channel = Number.parseInt(channelText ?? "-1", 10);
  const step = Number.parseInt(stepText ?? "-1", 10);

  if (!Number.isInteger(channel) || !Number.isInteger(step) || channel < 0 || step < 0) {
    return null;
  }

  return { channel, step };
}

/**
 * @param {Iterable<string>} keys
 * @param {number} channelCount
 * @param {number} stepCount
 * @returns {{ channel: number, step: number, key: string }[]}
 */
export function selectedCellLocations(keys, channelCount, stepCount) {
  return [...keys]
    .map((key) => {
      const location = cellLocationFromSelectionKey(key);

      if (!location || location.channel >= channelCount || location.step >= stepCount) {
        return null;
      }

      return { ...location, key: cellSelectionKey(location.channel, location.step) };
    })
    .filter(Boolean)
    .sort((left, right) => left.channel - right.channel || left.step - right.step);
}

/**
 * Move a selected pattern as a wrapped stencil. The clicked selected cell is
 * the anchor; row and time offsets wrap independently, which makes drops at
 * the edge resolve into useful cyclic drum variations instead of clipping.
 *
 * @param {{ channel: number, step: number }[]} locations
 * @param {{ channel: number, step: number }} anchor
 * @param {{ channel: number, step: number }} target
 * @param {number} channelCount
 * @param {number} stepCount
 * @returns {{ source: { channel: number, step: number, key: string }, destination: { channel: number, step: number, key: string } }[]}
 */
export function wrappedCellDestinations(locations, anchor, target, channelCount, stepCount) {
  if (locations.length === 0 || channelCount < 1 || stepCount < 1) return [];

  return locations.map((source) => {
    const channel = (target.channel + source.channel - anchor.channel + channelCount) % channelCount;
    const step = (target.step + source.step - anchor.step + stepCount) % stepCount;

    return {
      source: { ...source, key: cellSelectionKey(source.channel, source.step) },
      destination: { channel, step, key: cellSelectionKey(channel, step) },
    };
  });
}

/**
 * @param {{ channel: number, step: number, key: string }[]} locations
 * @param {{ channel: number, step: number }} anchor
 * @param {{ channel: number, step: number }} target
 * @param {number} channelCount
 * @param {number} stepCount
 * @param {"move" | "copy"} [mode]
 */
export function bulkDragLabel(locations, anchor, target, channelCount, stepCount, mode = "move") {
  if (locations.length === 0) return "Drag cells";

  const destinations = wrappedCellDestinations(
    locations,
    anchor,
    target,
    channelCount,
    stepCount,
  );
  const rows = new Set(destinations.map(({ destination }) => destination.channel));

  return `${locations.length} cell${locations.length === 1 ? "" : "s"} · ${rows.size} row${rows.size === 1 ? "" : "s"} · wrap ${mode}`;
}
