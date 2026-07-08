/**
 * Pointer-only drag controls should not enter tab focus or show focus rings after
 * mouse/touch use. Call at the start of pointerdown handlers.
 * @param {PointerEvent} event
 */
export function absorbPointerDragFocus(event) {
  event.preventDefault();
}

/**
 * @param {PointerEvent} event
 */
export function releasePointerDragFocus(event) {
  if (event.currentTarget instanceof HTMLElement) {
    event.currentTarget.blur();
  }
}
