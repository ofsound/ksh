<script>
  import { onMount } from "svelte";
  import CompactStrip from "./components/CompactStrip.svelte";
  import EditorView from "./components/EditorView.svelte";
  import { initKshSession } from "./lib/kshSession.svelte.js";

  onMount(() => initKshSession());
</script>

<main class="flex min-h-screen items-center justify-center bg-zinc-950 p-2">
  <div class="flex flex-col gap-2">
    <EditorView />
    <CompactStrip />
  </div>
</main>
