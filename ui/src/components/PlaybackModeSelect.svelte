<script>
  import {
    playbackModeFromFlags,
    playbackModeIsPingPong,
    playbackModeIsReversed,
    playbackModeLabel,
  } from "../lib/kshEditorUtils.js";
  import PlaybackDirectionIcon from "./PlaybackDirectionIcon.svelte";
  import PingPongUturnIcon from "./PingPongUturnIcon.svelte";

  /** @type {{ value?: string, onChange?: (mode: string) => void | Promise<void> }} */
  let { value = "normal", onChange, distributed = false } = $props();

  const reversed = $derived(playbackModeIsReversed(value));
  const pingPong = $derived(playbackModeIsPingPong(value));
  const modeLabel = $derived(playbackModeLabel(value));

  async function setFlags(nextReversed, nextPingPong) {
    const next = playbackModeFromFlags(nextReversed, nextPingPong);
    if (next === playbackModeFromFlags(reversed, pingPong)) {
      return;
    }
    await onChange?.(next);
  }

  async function toggleDirection() {
    await setFlags(!reversed, pingPong);
  }

  async function togglePingPong() {
    await setFlags(reversed, !pingPong);
  }
</script>

<div
  class={distributed ? "contents" : "flex shrink-0 items-center gap-0.5"}
  role="group"
  aria-label={`Playback mode: ${modeLabel}`}
>
  <button
    type="button"
    class="flex h-[26px] w-[34px] shrink-0 items-center justify-center p-0 text-accent outline-none focus-visible:ring-1 focus-visible:ring-focus-ring"
    aria-label={reversed ? "Direction: reverse. Click for forward." : "Direction: forward. Click for reverse."}
    aria-pressed={reversed}
    title={reversed ? "Reverse" : "Forward"}
    onclick={toggleDirection}
  >
    <PlaybackDirectionIcon {reversed} />
  </button>
  <button
    type="button"
    class={`flex h-[26px] w-[28px] shrink-0 items-center justify-center p-0 outline-none focus-visible:ring-1 focus-visible:ring-focus-ring ${pingPong ? "text-accent" : "text-text-faint"}`}
    aria-label={pingPong ? "Ping-pong on. Click to turn off." : "Ping-pong off. Click to turn on."}
    aria-pressed={pingPong}
    title="Ping-Pong"
    onclick={togglePingPong}
  >
    <PingPongUturnIcon {reversed} />
  </button>
</div>
