<script>
  import { onMount } from "svelte";
  import CompactStrip from "./components/CompactStrip.svelte";
  import EditorView from "./components/EditorView.svelte";
  import { combinedDimensions, compactPanelHeight } from "./lib/kshEditorUtils.js";
  import { initKshSession, session } from "./lib/kshSession.svelte.js";

  onMount(() => initKshSession());

  const brandingSteps = Array.from({ length: 16 }, (_, step) => step);
  const dims = $derived(combinedDimensions(session.kshState, session.patternViewScale));
</script>

<main class="bg-ksh-bg">
  <div
    class="flex flex-col overflow-hidden bg-ksh-bg"
    style={`width:${dims.width}px;height:${dims.height}px;`}
  >
    <EditorView />
    <div
      class="preview-panel relative shrink-0 border-t border-ksh-stroke-soft bg-ksh-bg"
      style={`width:${dims.width}px;height:${compactPanelHeight()}px;`}
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
      <footer class="absolute inset-x-0 bottom-0 z-20 flex h-[24px] items-center justify-end bg-ksh-bg px-3 text-[11px] text-ksh-muted">
        Source {session.selectedSource + 1} · {session.kshState.channelCount} channel(s) · cycle layer: drag ↖ cycle ↘ offset · Shift/Cmd/Opt layers
      </footer>
    </div>
  </div>
</main>
