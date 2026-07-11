<script>
  /** Four-way move icon for the cell-selection and repositioning tool. */
  /** @type {{ class?: string }} */
  let { class: className = "h-[20px] w-[20px]" } = $props();
</script>

<svg
  xmlns="http://www.w3.org/2000/svg"
  viewBox="0 0 24 24"
  class={className}
  aria-hidden="true"
>
  <path
    fill="none"
    stroke="currentColor"
    stroke-linecap="round"
    stroke-linejoin="round"
    stroke-width="2.2"
    d="M12 3v18M3 12h18"
  />
  <path
    fill="none"
    stroke="currentColor"
    stroke-linecap="round"
    stroke-linejoin="round"
    stroke-width="2.2"
    d="m9 6 3-3 3 3M9 18l3 3 3-3M6 9l-3 3 3 3M18 9l3 3-3 3"
  />
</svg>
