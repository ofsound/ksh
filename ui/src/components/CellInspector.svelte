<script>
  import { onMount, tick } from "svelte";
  import StepNumberDragInput from "./StepNumberDragInput.svelte";
  import KshCyclePatternEditor from "./KshCyclePatternEditor.svelte";
  import { interfaceAccent } from "./rowAccentTheme.js";
  import {
    MAX_CYCLE_PATTERN_CELLS,
    MAX_ROLL,
    SILENT_SOURCE,
  } from "../lib/kshConstants.js";
  import {
    applyRelativeCellOffset,
    applySelectedCyclePattern,
    captureSelectedCellValues,
    commonSelectedCellValue,
    selectedStepsByChannel,
  } from "../lib/kshCellInspector.js";
  import {
    cellSelection,
    currentSelectedEnabledCellLocations,
  } from "../lib/kshCellSelection.svelte.js";
  import {
    channelToneColor,
    mutedChannelColor,
  } from "../lib/kshEditorUtils.js";
  import { positionFloatingPopover } from "../lib/floatingPopover.js";
  import {
    beginEditGestureHistory,
    commitEditGestureHistory,
    sendCellsForChannel,
    session,
    setSourceLayerMode,
  } from "../lib/kshSession.svelte.js";

  let velocityOffset = $state(0);
  let probabilityOffset = $state(0);
  /** @type {Map<string, number> | null} */
  let velocityStarts = $state(null);
  /** @type {Map<string, number> | null} */
  let probabilityStarts = $state(null);
  let rollDragging = $state(false);
  /** @type {Set<string> | null} */
  let rollKeys = $state(null);
  let cycleInspectorOpen = $state(false);
  let cycleInspectorRoot = $state(null);
  let cycleInspectorAnchor = null;
  let cycleInspectorPosition = $state({ left: 0, top: 0, pointerLeft: 10, placement: "above" });

  const inspectorLocations = $derived(currentSelectedEnabledCellLocations());
  const inspectorKeys = $derived(new Set(inspectorLocations.map(({ key }) => key)));
  const selectedCount = $derived(inspectorLocations.length);
  const cycleInspectorCell = $derived.by(() => {
    const location = inspectorLocations[0];
    return location
      ? session.kshState.sources[session.selectedSource]?.[location.channel]?.[location.step] ?? null
      : null;
  });
  const cycleInspectorThemeStyle = $derived.by(() => {
    const channel = inspectorLocations[0]?.channel ?? 0;
    const muted = session.selectedSource !== SILENT_SOURCE
      && session.kshState.sourceChannelMutes[session.selectedSource][channel];
    let light = channelToneColor(channel, "light", session.dcColors);
    let dark = channelToneColor(channel, "dark", session.dcColors);
    let divider = channelToneColor(channel, "divider", session.dcColors);

    if (muted) {
      light = mutedChannelColor(light);
      dark = mutedChannelColor(dark);
      divider = mutedChannelColor(divider);
    }

    const surface = `color-mix(in srgb, ${dark} 30%, var(--color-app))`;
    const raisedSurface = `color-mix(in srgb, ${light} 12%, ${surface})`;
    return `--cycle-row-light:${light};--cycle-row-dark:${dark};--cycle-row-divider:${divider};--cycle-row-surface:${surface};background:linear-gradient(to bottom, ${raisedSurface}, ${surface});box-shadow:0 10px 28px color-mix(in srgb, var(--color-app) 42%, rgba(0,0,0,0.45)),0 0 24px color-mix(in srgb, var(--color-text) 4%, transparent);`;
  });
  const inspectorActive = $derived(
    session.selectedSource !== SILENT_SOURCE && selectedCount > 0,
  );
  const selectionTitle = $derived(
    `${selectedCount} Cell${selectedCount === 1 ? "" : "s"} Selected`,
  );

  const commonCycle = $derived(
    inspectorActive
      ? commonSelectedCellValue(
          session.kshState,
          session.selectedSource,
          inspectorKeys,
          "cycle",
        )
      : null,
  );
  const commonRoll = $derived(
    inspectorActive
      ? commonSelectedCellValue(
          session.kshState,
          session.selectedSource,
          inspectorKeys,
          "roll",
        )
      : null,
  );

  const cycleValue = $derived(commonCycle ?? 1);
  const rollValue = $derived(commonRoll ?? 1);

  function focusInspectorLayer(mode) {
    cellSelection.inspectorLayerActive = true;
    setSourceLayerMode(mode === "cycle" ? "probability" : mode);
  }

  function formatBipolarOffset(value) {
    const rounded = Math.round(value);
    if (rounded > 0) {
      return `+${rounded}`;
    }
    return String(rounded);
  }

  function formatModeValue(value, common, dragging) {
    if (!inspectorActive) {
      return "—";
    }
    if (!dragging && common === null) {
      return "—";
    }
    return String(Math.round(value));
  }

  function closeCycleInspector() {
    cycleInspectorOpen = false;
  }

  function positionCycleInspector() {
    if (!cycleInspectorOpen || !cycleInspectorRoot || !cycleInspectorAnchor) {
      return;
    }

    cycleInspectorPosition = positionFloatingPopover(cycleInspectorAnchor, cycleInspectorRoot, {
      preferAbove: true,
    });
  }

  async function openCycleInspector() {
    if (!inspectorActive || inspectorLocations.length === 0) {
      return;
    }

    cycleInspectorOpen = true;
    await tick();
    positionCycleInspector();
  }

  function cycleInspectorPointerStyle() {
    const left = `${cycleInspectorPosition.pointerLeft}px`;
    return cycleInspectorPosition.placement === "above"
      ? `left:${left};bottom:-8px;border-top:8px solid var(--cycle-row-divider);`
      : `left:${left};top:-8px;border-bottom:8px solid var(--cycle-row-divider);`;
  }

  function beginCycleInspectorPatternGesture() {
    if (inspectorActive) {
      beginEditGestureHistory();
    }
  }

  function previewCycleInspectorPattern(cycle, cycleMask) {
    if (inspectorActive) {
      applySelectedCyclePattern(
        session.kshState,
        session.selectedSource,
        inspectorKeys,
        cycle,
        cycleMask,
      );
    }
  }

  async function commitCycleInspectorPattern(cycle, cycleMask) {
    if (!inspectorActive) {
      return;
    }

    const changed = applySelectedCyclePattern(
      session.kshState,
      session.selectedSource,
      inspectorKeys,
      cycle,
      cycleMask,
    );
    if (changed) {
      await flushSelectedCells();
    }
    await commitEditGestureHistory("Edit selected cycle pattern");
  }

  onMount(() => {
    const onPointerDown = (event) => {
      if (cycleInspectorRoot?.contains(event.target) || cycleInspectorAnchor?.contains(event.target)) {
        return;
      }
      closeCycleInspector();
    };
    const onKeyDown = (event) => {
      if (event.key === "Escape" && cycleInspectorOpen) {
        event.preventDefault();
        closeCycleInspector();
      }
    };

    document.addEventListener("pointerdown", onPointerDown, true);
    document.addEventListener("keydown", onKeyDown, true);
    window.addEventListener("resize", positionCycleInspector);
    window.addEventListener("scroll", positionCycleInspector, true);

    return () => {
      document.removeEventListener("pointerdown", onPointerDown, true);
      document.removeEventListener("keydown", onKeyDown, true);
      window.removeEventListener("resize", positionCycleInspector);
      window.removeEventListener("scroll", positionCycleInspector, true);
    };
  });

  async function flushSelectedCells(keys = inspectorKeys) {
    const byChannel = selectedStepsByChannel(
      keys,
      session.kshState.channelCount,
      session.kshState.stepCount,
    );

    for (const [channel, steps] of byChannel) {
      await sendCellsForChannel(session.selectedSource, channel, steps);
    }
  }

  function beginVelocityGesture() {
    if (!inspectorActive) {
      return;
    }

    focusInspectorLayer("velocity");
    beginEditGestureHistory();
    velocityStarts = captureSelectedCellValues(
      session.kshState,
      session.selectedSource,
      inspectorKeys,
      "velocity",
    );
    velocityOffset = 0;
  }

  function previewVelocityOffset(offset) {
    if (!inspectorActive || !velocityStarts) {
      return;
    }

    velocityOffset = offset;
    applyRelativeCellOffset(
      session.kshState,
      session.selectedSource,
      velocityStarts,
      "velocity",
      offset,
    );
  }

  async function commitVelocityOffset(offset) {
    if (!inspectorActive || !velocityStarts) {
      velocityOffset = 0;
      velocityStarts = null;
      return;
    }

    if (offset !== 0) {
      applyRelativeCellOffset(
        session.kshState,
        session.selectedSource,
        velocityStarts,
        "velocity",
        offset,
      );
      await flushSelectedCells();
    }
    await commitEditGestureHistory("Adjust selected velocity");
    velocityOffset = 0;
    velocityStarts = null;
  }

  function beginProbabilityGesture() {
    if (!inspectorActive) {
      return;
    }

    focusInspectorLayer("probability");
    beginEditGestureHistory();
    probabilityStarts = captureSelectedCellValues(
      session.kshState,
      session.selectedSource,
      inspectorKeys,
      "probability",
    );
    probabilityOffset = 0;
  }

  function previewProbabilityOffset(offset) {
    if (!inspectorActive || !probabilityStarts) {
      return;
    }

    probabilityOffset = offset;
    applyRelativeCellOffset(
      session.kshState,
      session.selectedSource,
      probabilityStarts,
      "probability",
      offset,
    );
  }

  async function commitProbabilityOffset(offset) {
    if (!inspectorActive || !probabilityStarts) {
      probabilityOffset = 0;
      probabilityStarts = null;
      return;
    }

    if (offset !== 0) {
      applyRelativeCellOffset(
        session.kshState,
        session.selectedSource,
        probabilityStarts,
        "probability",
        offset,
      );
      await flushSelectedCells();
    }
    await commitEditGestureHistory("Adjust selected probability");
    probabilityOffset = 0;
    probabilityStarts = null;
  }

  async function beginRollGesture() {
    if (!inspectorActive) {
      return;
    }

    focusInspectorLayer("roll");
    rollKeys = new Set(inspectorKeys);
    rollDragging = true;
    beginEditGestureHistory();
  }

  async function previewRollValue(value) {
    if (!inspectorActive || !rollKeys) {
      return;
    }

    applyAbsoluteCellMode(
      session.kshState,
      session.selectedSource,
      rollKeys,
      "roll",
      value,
    );
  }

  async function commitRollValue(value) {
    if (!inspectorActive || !rollKeys) {
      rollDragging = false;
      rollKeys = null;
      return;
    }

    applyAbsoluteCellMode(
      session.kshState,
      session.selectedSource,
      rollKeys,
      "roll",
      value,
    );
    await flushSelectedCells(rollKeys);
    await commitEditGestureHistory("Set selected roll");
    rollDragging = false;
    rollKeys = null;
  }

  function advanceRollValue() {
    if (!inspectorActive) {
      return;
    }

    const next = rollValue >= MAX_ROLL ? 1 : rollValue + 1;
    void commitRollValue(next);
  }
</script>

<div
  class="compact-cell-inspector-panel flex shrink-0 flex-col gap-3 self-stretch"
  class:compact-cell-inspector-panel-inactive={!inspectorActive}
  aria-label="Cell inspector"
>
  <span class="header-label text-text">{selectionTitle}</span>

  <div class="flex items-end gap-5">
    <div class="flex flex-col items-start gap-1">
      <span class="compact-inspector-field-label">Vel</span>
      <StepNumberDragInput
        boxed
        compact
        boxChars={4}
        accent={interfaceAccent}
        value={velocityOffset}
        min={-126}
        max={126}
        resetValue={0}
        disabled={!inspectorActive}
        deferCommit
        formatValue={formatBipolarOffset}
        ariaLabel="Relative velocity offset for selected cells"
        onGestureStart={beginVelocityGesture}
        onValuePreview={previewVelocityOffset}
        onValueCommit={commitVelocityOffset}
      />
    </div>
    <div class="flex flex-col items-start gap-1">
      <span class="compact-inspector-field-label">Prob</span>
      <StepNumberDragInput
        boxed
        compact
        boxChars={4}
        accent={interfaceAccent}
        value={probabilityOffset}
        min={-100}
        max={100}
        resetValue={0}
        disabled={!inspectorActive}
        deferCommit
        formatValue={formatBipolarOffset}
        ariaLabel="Relative probability offset for selected cells"
        onGestureStart={beginProbabilityGesture}
        onValuePreview={previewProbabilityOffset}
        onValueCommit={commitProbabilityOffset}
      />
    </div>
  </div>

  <div class="flex items-end gap-5">
    <div bind:this={cycleInspectorAnchor} class="relative flex flex-col items-start gap-1">
      <span class="compact-inspector-field-label">Cycle</span>
      <StepNumberDragInput
        boxed
        compact
        boxChars={3}
        accent={interfaceAccent}
        value={cycleValue}
        min={1}
        max={MAX_CYCLE_PATTERN_CELLS}
        disabled={!inspectorActive}
        deferCommit
        formatValue={(value) => formatModeValue(value, commonCycle, false)}
        ariaLabel="Set cycle mode for selected cells"
        onClick={openCycleInspector}
      />
    </div>
    <div class="flex flex-col items-start gap-1">
      <span class="compact-inspector-field-label">Roll</span>
      <StepNumberDragInput
        boxed
        compact
        boxChars={3}
        accent={interfaceAccent}
        value={rollValue}
        min={1}
        max={MAX_ROLL}
        disabled={!inspectorActive}
        deferCommit
        formatValue={(value) => formatModeValue(value, commonRoll, rollDragging)}
        ariaLabel="Set roll mode for selected cells"
        onGestureStart={beginRollGesture}
        onValuePreview={previewRollValue}
        onValueCommit={commitRollValue}
        onClick={advanceRollValue}
      />
    </div>
  </div>

  {#if cycleInspectorOpen && inspectorActive && cycleInspectorCell}
    <div
      bind:this={cycleInspectorRoot}
      class="fixed z-[80] w-[280px] rounded-md p-2 text-text backdrop-blur-sm"
      style={`left:${cycleInspectorPosition.left}px;top:${cycleInspectorPosition.top}px;${cycleInspectorThemeStyle}`}
      role="dialog"
      tabindex="-1"
      aria-label="Cycle editor for selected cells"
      onpointerdown={(event) => event.stopPropagation()}
    >
      <div
        class="pointer-events-none absolute h-0 w-0 border-x-[10px] border-x-transparent"
        style={cycleInspectorPointerStyle()}
        aria-hidden="true"
      ></div>
      <div class="mb-1 flex items-center justify-between px-1 text-[12px] font-semibold leading-none" style="color:var(--cycle-row-light);">
        <span>Selected cells</span>
        <button
          type="button"
          class="cycle-popover-focus flex h-4 w-4 items-center justify-center rounded-sm text-[14px] leading-none outline-none hover:text-text"
          style="color:var(--cycle-row-light);"
          aria-label="Close cycle editor"
          onclick={closeCycleInspector}
        >
          ×
        </button>
      </div>
      <KshCyclePatternEditor
        cycle={cycleInspectorCell.cycle}
        cycleMask={cycleInspectorCell.cycleMask}
        onGestureStart={beginCycleInspectorPatternGesture}
        onPatternPreview={previewCycleInspectorPattern}
        onPatternCommit={commitCycleInspectorPattern}
      />
    </div>
  {/if}
</div>
