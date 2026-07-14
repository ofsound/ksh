<script>
  import { onMount } from "svelte";
  import StepNumberDragInput from "./StepNumberDragInput.svelte";
  import KshCyclePatternEditor from "./KshCyclePatternEditor.svelte";
  import { interfaceAccent } from "./rowAccentTheme.js";
  import {
    MAX_ROLL,
    SILENT_SOURCE,
  } from "../lib/kshConstants.js";
  import {
    applyAbsoluteCellMode,
    applyRelativeCellOffset,
    applySelectedCyclePattern,
    captureSelectedCellValues,
    commonSelectedCellValue,
    selectedStepsByChannel,
  } from "../lib/kshCellInspector.js";
  import { currentSelectedEnabledCellLocations } from "../lib/kshCellSelection.svelte.js";
  import { defaultCell } from "../lib/kshUiState.js";
  import {
    beginEditGestureHistory,
    commitEditGestureHistory,
    queueCellsForChannelLive,
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
  /** @type {number} */
  let liveFlushRaf = 0;
  /** @type {Set<string> | null} */
  let liveFlushKeys = null;

  const inspectorLocations = $derived(currentSelectedEnabledCellLocations());
  const inspectorKeys = $derived(new Set(inspectorLocations.map(({ key }) => key)));
  const selectedCount = $derived(inspectorLocations.length);
  const cycleInspectorCell = $derived.by(() => {
    const location = inspectorLocations[0];
    return location
      ? session.kshState.sources[session.selectedSource]?.[location.channel]?.[location.step] ?? null
      : null;
  });
  const inspectorActive = $derived(
    session.selectedSource !== SILENT_SOURCE && selectedCount > 0,
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

  const rollValue = $derived(commonRoll ?? 1);

  function focusInspectorLayer(mode) {
    setSourceLayerMode(mode);
  }

  function restoreDefaultInspectorLayer() {
    setSourceLayerMode("velocity");
  }

  function currentInspectorKeys() {
    return new Set(currentSelectedEnabledCellLocations().map(({ key }) => key));
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

  function beginCyclePatternGesture() {
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
    restoreDefaultInspectorLayer();
    if (!inspectorActive) {
      return;
    }

    // The editor applies the draft through onPatternPreview before this
    // callback runs, so this second application is normally unchanged. The
    // commit still must flush the already-previewed values to the engine.
    applySelectedCyclePattern(
      session.kshState,
      session.selectedSource,
      inspectorKeys,
      cycle,
      cycleMask,
    );
    await flushSelectedCells();
    await commitEditGestureHistory("Edit selected cycle pattern");
  }

  onMount(() => () => {
    if (liveFlushRaf !== 0) {
      cancelAnimationFrame(liveFlushRaf);
      liveFlushRaf = 0;
    }
    liveFlushKeys = null;
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

  /**
   * Live-preview flush: bump grid paint every call; coalesce native cell writes to one frame.
   * @param {Iterable<string>} keys
   */
  function queueSelectedCellsLive(keys) {
    // Immediate visual nudge so fills update even when native flush is rAF-coalesced.
    session.gridVisualEpoch += 1;
    liveFlushKeys = keys instanceof Set ? keys : new Set(keys);
    if (liveFlushRaf !== 0) {
      return;
    }

    liveFlushRaf = requestAnimationFrame(() => {
      liveFlushRaf = 0;
      const keysToFlush = liveFlushKeys;
      liveFlushKeys = null;
      if (!keysToFlush) {
        return;
      }

      const byChannel = selectedStepsByChannel(
        keysToFlush,
        session.kshState.channelCount,
        session.kshState.stepCount,
      );

      for (const [channel, steps] of byChannel) {
        queueCellsForChannelLive(session.selectedSource, channel, steps);
      }
    });
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
    const changed = applyRelativeCellOffset(
      session.kshState,
      session.selectedSource,
      velocityStarts,
      "velocity",
      offset,
    );
    if (changed) {
      queueSelectedCellsLive(inspectorKeys);
    }
  }

  async function commitVelocityOffset(offset) {
    restoreDefaultInspectorLayer();
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
    const changed = applyRelativeCellOffset(
      session.kshState,
      session.selectedSource,
      probabilityStarts,
      "probability",
      offset,
    );
    if (changed) {
      queueSelectedCellsLive(inspectorKeys);
    }
  }

  async function commitProbabilityOffset(offset) {
    restoreDefaultInspectorLayer();
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
    rollKeys = currentInspectorKeys();
    rollDragging = true;
    beginEditGestureHistory();
  }

  async function previewRollValue(value) {
    if (!inspectorActive || !rollKeys) {
      return;
    }

    const changed = applyAbsoluteCellMode(
      session.kshState,
      session.selectedSource,
      rollKeys,
      "roll",
      value,
    );
    if (changed) {
      queueSelectedCellsLive(rollKeys);
    }
  }

  async function commitRollValue(value) {
    restoreDefaultInspectorLayer();
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

  async function advanceRollValue() {
    restoreDefaultInspectorLayer();
    if (!inspectorActive) {
      return;
    }

    const next = rollValue >= MAX_ROLL ? 1 : rollValue + 1;
    const keys = currentInspectorKeys();
    applyAbsoluteCellMode(
      session.kshState,
      session.selectedSource,
      keys,
      "roll",
      next,
    );
    await flushSelectedCells(keys);
    await commitEditGestureHistory("Set selected roll");
    rollDragging = false;
    rollKeys = null;
  }

  async function deleteSelectedCells() {
    const locations = currentSelectedEnabledCellLocations();
    if (!inspectorActive || locations.length === 0) {
      return;
    }

    const keys = new Set(locations.map(({ key }) => key));
    restoreDefaultInspectorLayer();
    beginEditGestureHistory();

    for (const { channel, step } of locations) {
      session.kshState.sources[session.selectedSource][channel][step] = defaultCell();
    }

    await flushSelectedCells(keys);
    await commitEditGestureHistory("Delete selected cells");
  }
</script>

<div
  class="compact-panel-surface compact-cell-inspector-panel flex shrink-0 flex-col gap-3 self-stretch"
  class:compact-cell-inspector-panel-inactive={!inspectorActive}
  class:compact-cell-inspector-panel-active={inspectorActive}
  aria-label="Cell inspector"
>
  <div class="flex items-end gap-5">
    <div class="flex w-[74px] shrink-0 flex-col items-start justify-center self-stretch text-text">
      <span class="whitespace-nowrap text-[15px] font-bold leading-none tabular-nums">{selectedCount} Cells</span>
    </div>
    <div class="flex flex-col items-start gap-1">
      <span class="header-label text-text">Velocity</span>
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
      <span class="header-label text-text">Probability</span>
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
    <div class="flex flex-col items-start gap-1">
      <span class="header-label text-text">Roll</span>
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
    <div class="flex flex-col items-start gap-1">
      <span class="header-label text-text">Delete</span>
      <button
        type="button"
        class="mp-param-box mp-control-gradient flex h-8 items-center justify-center rounded-md border border-danger/60 text-lg font-semibold leading-none text-danger outline-none transition-[border-color,box-shadow,filter] duration-75 hover:border-danger disabled:opacity-50"
        style:--param-box-chars={3}
        disabled={!inspectorActive}
        aria-label="Delete selected cells"
        title="Delete selected cells"
        onclick={deleteSelectedCells}
      >
        ×
      </button>
    </div>
  </div>

  <div
    class="cycle-inspector-neutral min-w-0 overflow-hidden rounded-md border border-border-subtle p-1"
    role="group"
    aria-label="Cycle editor for selected cells"
  >
    <KshCyclePatternEditor
      cycle={cycleInspectorCell?.cycle ?? 1}
      cycleMask={cycleInspectorCell?.cycleMask ?? 1}
      disabled={!inspectorActive}
      onGestureStart={beginCyclePatternGesture}
      onPatternPreview={previewCycleInspectorPattern}
      onPatternCommit={commitCycleInspectorPattern}
    />
  </div>
</div>
