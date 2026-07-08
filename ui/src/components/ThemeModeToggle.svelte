<script>
  import ThemeAltIcon from "./ThemeAltIcon.svelte";
  import ThemeMoonIcon from "./ThemeMoonIcon.svelte";
  import ThemeSunIcon from "./ThemeSunIcon.svelte";
  import { nextThemeMode, themeModeTitles } from "../lib/themeMode.js";

  let {
    value = "dark",
    onValueChange = () => {},
  } = $props();

  let title = $derived(themeModeTitles[value] ?? themeModeTitles.dark);
  let nextTitle = $derived(themeModeTitles[nextThemeMode(value)] ?? themeModeTitles.light);

  function cycleThemeMode() {
    onValueChange(nextThemeMode(value));
  }
</script>

<button
  type="button"
  aria-label={title}
  title="{title} - click for {nextTitle.toLowerCase()}"
  class="flex h-8 w-8 shrink-0 items-center justify-center rounded-sm border-0 bg-transparent p-0 text-text-muted outline-none transition-colors hover:text-text focus-visible:ring-1 focus-visible:ring-focus-ring"
  onclick={cycleThemeMode}
>
  {#if value === "light"}
    <ThemeSunIcon class="pointer-events-none h-4 w-4" />
  {:else if value === "dark"}
    <ThemeMoonIcon class="pointer-events-none h-4 w-4" />
  {:else}
    <ThemeAltIcon class="pointer-events-none h-4 w-4" />
  {/if}
</button>
