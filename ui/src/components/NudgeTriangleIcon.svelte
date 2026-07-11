<script>
  /** Solid nudge triangle — left or right. */
  /** @type {{ reversed?: boolean, class?: string }} */
  let { reversed = false, class: className = "h-[21px] w-[14px]" } = $props();
</script>

<svg
  xmlns="http://www.w3.org/2000/svg"
  viewBox="0 0 14 21"
  class={className}
  aria-hidden="true"
>
  {#if reversed}
    <path fill="currentColor" d="M13 1.5 L1.5 10.5 L13 19.5 Z" />
  {:else}
    <path fill="currentColor" d="M1 1.5 L12.5 10.5 L1 19.5 Z" />
  {/if}
</svg>
