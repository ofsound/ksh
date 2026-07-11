/**
 * Calculate fixed-position coordinates inside the app's transformed UI root.
 * getBoundingClientRect() returns scaled viewport pixels, while fixed children
 * of a transformed ancestor use that ancestor's unscaled coordinate space.
 *
 * @param {HTMLElement} anchor
 * @param {HTMLElement} popover
 * @param {{ preferAbove?: boolean }} [options]
 * @returns {{ left: number, top: number, pointerLeft: number, placement: "above" | "below" }}
 */
export function positionFloatingPopover(anchor, popover, { preferAbove = false } = {}) {
  const anchorRect = anchor.getBoundingClientRect();
  const popoverRect = popover.getBoundingClientRect();
  const containingBlock = transformedContainingBlock(anchor);
  const blockRect = containingBlock?.getBoundingClientRect();
  const scaleX = containingBlock?.offsetWidth && blockRect?.width
    ? blockRect.width / containingBlock.offsetWidth
    : 1;
  const scaleY = containingBlock?.offsetHeight && blockRect?.height
    ? blockRect.height / containingBlock.offsetHeight
    : scaleX;
  const originLeft = blockRect?.left ?? 0;
  const originTop = blockRect?.top ?? 0;
  const blockWidth = containingBlock?.offsetWidth ?? document.documentElement.clientWidth / scaleX;
  const blockHeight = containingBlock?.offsetHeight ?? document.documentElement.clientHeight / scaleY;
  const anchorLeft = (anchorRect.left - originLeft) / scaleX;
  const anchorTop = (anchorRect.top - originTop) / scaleY;
  const anchorWidth = anchorRect.width / scaleX;
  const anchorHeight = anchorRect.height / scaleY;
  const width = popoverRect.width / scaleX;
  const height = popoverRect.height / scaleY;
  const margin = 10;
  const gap = 8;
  const left = Math.min(
    Math.max(margin, anchorLeft + (anchorWidth - width) / 2),
    Math.max(margin, blockWidth - width - margin),
  );
  const aboveTop = anchorTop - height - gap;
  const belowTop = anchorTop + anchorHeight + gap;
  const canPlaceAbove = aboveTop >= margin;
  const placedAbove = preferAbove ? canPlaceAbove : belowTop + height > blockHeight - margin && canPlaceAbove;
  const top = placedAbove
    ? aboveTop
    : Math.min(Math.max(margin, belowTop), Math.max(margin, blockHeight - height - margin));
  const pointerLeft = Math.min(
    Math.max(10, anchorLeft + anchorWidth / 2 - left - 10),
    Math.max(10, width - 30),
  );

  return { left, top, pointerLeft, placement: placedAbove ? "above" : "below" };
}

/** @param {HTMLElement} element */
function transformedContainingBlock(element) {
  let parent = element.parentElement;
  while (parent) {
    if (getComputedStyle(parent).transform !== "none") {
      return parent;
    }
    parent = parent.parentElement;
  }

  return null;
}
