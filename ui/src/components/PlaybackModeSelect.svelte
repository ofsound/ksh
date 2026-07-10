<script>
  import { onDestroy } from "svelte";
  import { normalizePlaybackMode } from "../lib/kshConstants.js";
  import { PLAYBACK_MODE_OPTIONS, playbackModeOption } from "../lib/kshEditorUtils.js";
  import { absorbPointerDragFocus, releasePointerDragFocus } from "./pointerDragFocus.js";

  /** @type {{ value?: string, accentClass?: string, onChange?: (mode: string) => void | Promise<void> }} */
  let { value = "normal", accentClass = "text-info", onChange } = $props();

  let rootEl = $state(null);
  let open = $state(false);
  let highlightIndex = $state(-1);
  let gesturePointerId = $state(null);
  let menuStyle = $state("");

  const normalizedValue = $derived(normalizePlaybackMode(value));
  const selectedOption = $derived(playbackModeOption(normalizedValue));

  function updateMenuPosition() {
    const trigger = rootEl?.querySelector("[data-playback-mode-trigger]");

    if (!(trigger instanceof HTMLElement)) {
      return;
    }

    const rect = trigger.getBoundingClientRect();
    menuStyle = `left:${rect.left}px;top:${rect.bottom + 2}px;`;
  }

  function removeGestureListeners() {
    window.removeEventListener("pointermove", onWindowPointerMove);
    window.removeEventListener("pointerup", onWindowPointerUp);
    window.removeEventListener("pointercancel", onWindowPointerUp);
  }

  function closeMenu() {
    open = false;
    highlightIndex = -1;
    gesturePointerId = null;
    removeGestureListeners();
  }

  function optionIndexFromPoint(x, y) {
    const target = document.elementFromPoint(x, y);
    const optionEl = target?.closest?.("[data-playback-mode-option]");

    if (!optionEl) {
      return -1;
    }

    return Number.parseInt(optionEl.getAttribute("data-index") ?? "-1", 10);
  }

  async function selectIndex(index) {
    const option = PLAYBACK_MODE_OPTIONS[index];

    if (!option) {
      closeMenu();
      return;
    }

    if (option.value !== normalizedValue) {
      await onChange?.(option.value);
    }

    closeMenu();
  }

  function onWindowPointerMove(event) {
    if (gesturePointerId !== event.pointerId) {
      return;
    }

    const index = optionIndexFromPoint(event.clientX, event.clientY);

    if (index >= 0) {
      highlightIndex = index;
    }
  }

  async function onWindowPointerUp(event) {
    if (gesturePointerId !== event.pointerId) {
      return;
    }

    const index = highlightIndex >= 0 ? highlightIndex : optionIndexFromPoint(event.clientX, event.clientY);

    if (index >= 0) {
      await selectIndex(index);
    } else {
      gesturePointerId = null;
      removeGestureListeners();
    }

    releasePointerDragFocus(event);
  }

  function beginGesture(event) {
    event.stopPropagation();
    absorbPointerDragFocus(event);

    if (!open) {
      open = true;
      updateMenuPosition();
    }

    gesturePointerId = event.pointerId;

    const index = optionIndexFromPoint(event.clientX, event.clientY);

    if (index >= 0) {
      highlightIndex = index;
    }

    removeGestureListeners();
    window.addEventListener("pointermove", onWindowPointerMove);
    window.addEventListener("pointerup", onWindowPointerUp);
    window.addEventListener("pointercancel", onWindowPointerUp);
  }

  function onTriggerPointerDown(event) {
    beginGesture(event);
  }

  function onOptionPointerDown(event) {
    beginGesture(event);

    const index = Number.parseInt(event.currentTarget.getAttribute("data-index") ?? "-1", 10);

    if (index >= 0) {
      highlightIndex = index;
    }
  }

  $effect(() => {
    if (!open) {
      return;
    }

    updateMenuPosition();

    function onDocPointerDown(event) {
      if (gesturePointerId !== null) {
        return;
      }

      if (rootEl?.contains(event.target)) {
        return;
      }

      closeMenu();
    }

    const timer = setTimeout(() => {
      document.addEventListener("pointerdown", onDocPointerDown, true);
    }, 0);

    return () => {
      clearTimeout(timer);
      document.removeEventListener("pointerdown", onDocPointerDown, true);
    };
  });

  onDestroy(() => {
    removeGestureListeners();
  });
</script>

<div class="relative shrink-0" bind:this={rootEl}>
  <button
    type="button"
    data-playback-mode-trigger
    class={`flex h-5 w-[4.5rem] shrink-0 items-center justify-start text-left text-[13px] leading-none outline-none focus-visible:ring-1 focus-visible:ring-focus-ring ${accentClass}`}
    aria-haspopup="listbox"
    aria-expanded={open}
    aria-label={`Playback mode: ${selectedOption.label}`}
    onpointerdown={onTriggerPointerDown}
  >
    {selectedOption.label}
  </button>

  {#if open}
    <div
      class="fixed z-50 min-w-[7.75rem] overflow-hidden border border-border-strong bg-app shadow-[0_10px_28px_rgba(0,0,0,0.36)]"
      style={menuStyle}
      role="listbox"
      aria-label="Playback mode"
    >
      {#each PLAYBACK_MODE_OPTIONS as option, index (option.value)}
        <button
          type="button"
          data-playback-mode-option
          data-index={index}
          class={`flex h-7 w-full items-center justify-between gap-2 px-2 text-left text-[11px] font-semibold outline-none ${highlightIndex === index ? "bg-control-secondary text-accent" : option.value === normalizedValue ? "text-accent" : "text-text"} hover:bg-control-secondary focus-visible:bg-control-secondary`}
          role="option"
          aria-selected={option.value === normalizedValue}
          onpointerdown={onOptionPointerDown}
        >
          <span>{option.label}</span>
          <span class="w-3 text-right text-[10px]">{option.value === normalizedValue ? "✓" : ""}</span>
        </button>
      {/each}
    </div>
  {/if}
</div>
