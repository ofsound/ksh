<script>
  import StepNumberDragInput from "./StepNumberDragInput.svelte";
  import { interfaceAccent } from "./rowAccentTheme.js";
  import {
    MAX_CYCLE_PATTERN_CELLS,
    MAX_ROLL,
    SILENT_SOURCE,
  } from "../lib/kshConstants.js";
  import {
    applyAbsoluteCellMode,
    applyRelativeCellOffset,
    captureSelectedCellValues,
    commonSelectedCellValue,
    selectedStepsByChannel,
  } from "../lib/kshCellInspector.js";
  import {
    cellSelection,
    currentSelectedCellLocations,
    selectedCellKeys,
  } from "../lib/kshCellSelection.svelte.js";
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
  let cycleDragging = $state(false);
  let rollDragging = $state(false);

  const selectedCount = $derived(currentSelectedCellLocations().length);
  const inspectorActive = $derived(
    cellSelection.editMode
      && session.selectedSource !== SILENT_SOURCE
      && selectedCount > 0,
  );
  const selectionTitle = $derived(
    `${selectedCount} Cell${selectedCount === 1 ? "" : "s"} Selected`,
  );

  const commonCycle = $derived(
    inspectorActive
      ? commonSelectedCellValue(
          session.kshState,
          session.selectedSource,
          selectedCellKeys,
          "cycle",
        )
      : null,
  );
  const commonRoll = $derived(
    inspectorActive
      ? commonSelectedCellValue(
          session.kshState,
          session.selectedSource,
          selectedCellKeys,
          "roll",
        )
      : null,
  );

  const cycleValue = $derived(commonCycle ?? 1);
  const rollValue = $derived(commonRoll ?? 1);

  function focusInspectorLayer(mode) {
    setSourceLayerMode(mode);
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

  async function flushSelectedCells() {
    const byChannel = selectedStepsByChannel(
      selectedCellKeys,
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
      selectedCellKeys,
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
      selectedCellKeys,
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

  async function beginCycleGesture() {
    if (!inspectorActive) {
      return;
    }

    focusInspectorLayer("cycle");
    cycleDragging = true;
    beginEditGestureHistory();
  }

  async function previewCycleValue(value) {
    if (!inspectorActive) {
      return;
    }

    applyAbsoluteCellMode(
      session.kshState,
      session.selectedSource,
      selectedCellKeys,
      "cycle",
      value,
    );
  }

  async function commitCycleValue(value) {
    if (!inspectorActive) {
      cycleDragging = false;
      return;
    }

    applyAbsoluteCellMode(
      session.kshState,
      session.selectedSource,
      selectedCellKeys,
      "cycle",
      value,
    );
    await flushSelectedCells();
    await commitEditGestureHistory("Set selected cycle");
    cycleDragging = false;
  }

  async function beginRollGesture() {
    if (!inspectorActive) {
      return;
    }

    focusInspectorLayer("roll");
    rollDragging = true;
    beginEditGestureHistory();
  }

  async function previewRollValue(value) {
    if (!inspectorActive) {
      return;
    }

    applyAbsoluteCellMode(
      session.kshState,
      session.selectedSource,
      selectedCellKeys,
      "roll",
      value,
    );
  }

  async function commitRollValue(value) {
    if (!inspectorActive) {
      rollDragging = false;
      return;
    }

    applyAbsoluteCellMode(
      session.kshState,
      session.selectedSource,
      selectedCellKeys,
      "roll",
      value,
    );
    await flushSelectedCells();
    await commitEditGestureHistory("Set selected roll");
    rollDragging = false;
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
    <div class="flex flex-col items-start gap-1">
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
        formatValue={(value) => formatModeValue(value, commonCycle, cycleDragging)}
        ariaLabel="Set cycle mode for selected cells"
        onGestureStart={beginCycleGesture}
        onValuePreview={previewCycleValue}
        onValueCommit={commitCycleValue}
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
      />
    </div>
  </div>
</div>
