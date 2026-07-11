<script>
  /** Stemmed triangle arrow — faces right (forward) or left (reverse). */
  /** @type {{ reversed?: boolean, class?: string }} */
  let { reversed = false, class: className = "h-[21px] w-[33px]" } = $props();
</script>

<svg
  xmlns="http://www.w3.org/2000/svg"
  viewBox="0 0 22 14"
  class={className}
  aria-hidden="true"
>
  {#if reversed}
    <path
      fill="currentColor"
      d="M9 1.5 L1.5 7 L9 12.5 Z M9 5.75 H20.5 V8.25 H9 Z"
    />
  {:else}
    <path
      fill="currentColor"
      d="M13 1.5 L20.5 7 L13 12.5 Z M1.5 5.75 H13 V8.25 H1.5 Z"
    />
  {/if}
</svg>
