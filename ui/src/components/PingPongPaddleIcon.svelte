<script>
  /** Table-tennis paddle silhouette. */
  /** @type {{ class?: string }} */
  let { class: className = "h-[21px] w-[21px]" } = $props();
</script>

<svg
  xmlns="http://www.w3.org/2000/svg"
  viewBox="0 0 24 24"
  class={className}
  aria-hidden="true"
>
  <!-- The blade/handle gap is intentional: it keeps the silhouette readable at 21px. -->
  <g>
    <path
      fill="currentColor"
      d="M6.55 8C6.9 3.35 10.85.25 15.2.1 19.95 0 23.95 3.4 24 8.35c0 4.55-2.5 8.55-6.45 10.1-1.45.55-2.8.4-4-0.8l-7-6.8C6.15 10.1 6.25 8.9 6.55 8Z"
    />
    <path
      fill="currentColor"
      d="M5.5 10.6c1.1 1.8 1.3 4.1.5 5.7-.7 1.4-2.2 2.3-3.1 3.2L.2 21.7c-.5.5-.2 1.3.3 1.7l2.3.6c.6.2 1.2 0 1.6-.5l4.1-4.3c1.1-1.1 2.6-1.5 3.7-1.3l2 .5L5.5 10.6Z"
    />
  </g>
</svg>
