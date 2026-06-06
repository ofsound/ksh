<script>
  import { onMount } from "svelte";
  import CompactStrip from "./components/CompactStrip.svelte";
  import EditorView from "./components/EditorView.svelte";
  import { combinedDimensions, compactPanelHeight } from "./lib/kshEditorUtils.js";
  import { initKshSession, session } from "./lib/kshSession.svelte.js";

  onMount(() => initKshSession());

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
      <CompactStrip />
      <footer class="absolute inset-x-0 bottom-0 flex h-[24px] items-center justify-end bg-ksh-bg px-3 text-[11px] text-ksh-muted">
        Source {session.selectedSource + 1} · {session.kshState.channelCount} channel(s) · cycle layer: drag ↖ cycle ↘ offset · Shift/Cmd/Opt layers
      </footer>
    </div>
  </div>
</main>
