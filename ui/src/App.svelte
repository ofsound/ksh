<script>
  import { onMount } from "svelte";
  import CompactStrip from "./components/CompactStrip.svelte";
  import EditorView from "./components/EditorView.svelte";
  import ThemeModeToggle from "./components/ThemeModeToggle.svelte";
  import UiScaleDragInput from "./components/UiScaleDragInput.svelte";
  import { SILENT_SOURCE } from "./lib/kshConstants.js";
  import { combinedDimensions, previewPanelHeight } from "./lib/kshEditorUtils.js";
  import {
    initKshSession,
    initializeThemeModeFromNative,
    initializeUiScaleFromNative,
    session,
    setExplicitUiScalePercent,
    setThemeMode,
    setUiScaleViewportSize,
    syncEditorScaleMinimumToNative,
    toggleDcColors,
    togglePatternViewScale,
  } from "./lib/kshSession.svelte.js";
  import { uiScaleState } from "./lib/uiScale.svelte.js";

  let appRoot = $state(null);

  onMount(() => {
    initializeThemeModeFromNative();
    initializeUiScaleFromNative();
    const teardownSession = initKshSession();
    let scaleFrameId = 0;

    const updateUiScale = () => {
      const target = appRoot ?? document.documentElement;
      const rect = target.getBoundingClientRect();

      setUiScaleViewportSize(
        rect.width || window.innerWidth,
        rect.height || window.innerHeight,
      );
      void syncEditorScaleMinimumToNative();
    };

    const scheduleUiScaleUpdate = () => {
      if (scaleFrameId) return;

      scaleFrameId = requestAnimationFrame(() => {
        scaleFrameId = 0;
        updateUiScale();
      });
    };

    const resizeObserver = new ResizeObserver(scheduleUiScaleUpdate);
    resizeObserver.observe(appRoot ?? document.documentElement);
    updateUiScale();

    return () => {
      teardownSession?.();
      resizeObserver.disconnect();
      if (scaleFrameId) {
        cancelAnimationFrame(scaleFrameId);
      }
    };
  });

  const brandingSteps = Array.from({ length: 16 }, (_, step) => step);
  const dims = $derived(combinedDimensions(session.kshState, session.patternViewScale));
  const scaledDims = $derived({
    width: Math.round(dims.width * uiScaleState.scale),
    height: Math.round(dims.height * uiScaleState.scale),
  });
</script>

<main class="bg-app" bind:this={appRoot}>
  <div
    class="flex flex-col overflow-hidden bg-app"
    style={`width:${scaledDims.width}px;height:${scaledDims.height}px;`}
  >
    <div
      class="origin-top-left"
      style={`width:${dims.width}px;height:${dims.height}px;transform:scale(${uiScaleState.scale});`}
    >
      <EditorView />
      <div
        class="preview-panel relative shrink-0 border-t border-border-subtle bg-app"
        style={`width:${dims.width}px;height:${previewPanelHeight()}px;`}
      >
        <div class="preview-branding" aria-hidden="true">
          <div class="preview-branding-steps">
            {#each brandingSteps as step (step)}
              <span class={step % 4 === 0 ? "is-downbeat" : ""}></span>
            {/each}
          </div>

          <div class="preview-branding-lockup">
            <span class="preview-branding-maker">OFSOUND</span>
            <div class="preview-branding-title">
              <span>K</span><span>S</span><span>H</span>
            </div>
            <div class="preview-branding-legend">
              <span>Kick</span><span>Snare</span><span>Hat</span>
            </div>
          </div>

          <div class="preview-branding-drums">
            <span class="drum-icon drum-icon-kick"></span>
            <span class="drum-icon drum-icon-snare"></span>
            <span class="drum-icon drum-icon-hat"></span>
            <span class="drum-stick drum-stick-left"></span>
            <span class="drum-stick drum-stick-right"></span>
          </div>
        </div>
        <CompactStrip />
        <footer class="absolute inset-x-0 bottom-0 z-20 flex h-[24px] items-center justify-end bg-app px-3 text-[11px] text-text-muted">
          Source {session.selectedSource === SILENT_SOURCE ? "M" : session.selectedSource + 1} · {session.kshState.channelCount} channel(s) · cycle layer: drag ↖ cycle ↘ offset · Shift/Opt/Opt+Shift layers
        </footer>
      </div>

      <footer
        class="flex h-[40px] shrink-0 items-center justify-between border-t border-border-subtle bg-shell px-3 text-[11px] font-semibold uppercase tracking-[0.14em] text-text-muted"
        style={`width:${dims.width}px;`}
      >
        <span>KSH</span>
        <div class="flex shrink-0 items-center gap-2">
          <button
            type="button"
            class={`mp-param-box mp-control-gradient flex h-8 min-w-[2.75rem] items-center justify-center rounded-md border text-sm font-semibold tabular-nums normal-case tracking-normal outline-none transition-[border-color,color] duration-75 focus-visible:ring-1 focus-visible:ring-focus-ring ${
              session.patternViewScale === 1.5
                ? "border-accent text-accent"
                : "border-border text-text-muted"
            }`}
            style="--param-box-chars:4"
            aria-pressed={session.patternViewScale === 1.5}
            aria-label="Pattern view scale"
            title={session.patternViewScale === 1.5 ? "Pattern scale 1.5x" : "Pattern scale 1x"}
            onclick={togglePatternViewScale}
          >
            {session.patternViewScale === 1.5 ? "1.5x" : "1x"}
          </button>
          <button
            type="button"
            class={`mp-param-box mp-control-gradient flex h-8 min-w-[2.25rem] items-center justify-center rounded-md border text-sm font-semibold tabular-nums normal-case tracking-normal outline-none transition-[border-color,color] duration-75 focus-visible:ring-1 focus-visible:ring-focus-ring ${
              session.dcColors
                ? "border-accent text-accent"
                : "border-border text-text-muted"
            }`}
            style="--param-box-chars:2"
            aria-pressed={Boolean(session.dcColors)}
            aria-label="Drum color channels"
            title={session.dcColors ? "Drum colors on" : "Drum colors off"}
            onclick={toggleDcColors}
          >
            DC
          </button>
          <ThemeModeToggle value={session.themeMode} onValueChange={setThemeMode} />
          <span>UI</span>
          <UiScaleDragInput
            value={uiScaleState.percent}
            onValueChange={setExplicitUiScalePercent}
          />
        </div>
      </footer>
    </div>
  </div>
</main>
