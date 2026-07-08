<script>
  /** @type {{ class?: string }} */
  let { class: className = "h-5 w-5" } = $props();
</script>

<svg
  xmlns="http://www.w3.org/2000/svg"
  viewBox="0 0 24 24"
  fill="none"
  stroke="currentColor"
  stroke-width="2"
  stroke-linecap="round"
  stroke-linejoin="round"
  class={className}
  aria-hidden="true"
>
  <path d="M12 4.25L5.25 19.25M12 4.25L18.75 19.25M8.25 14.25h7.5" />
  <path d="M10.25 10.25h3.5" stroke-width="1.5" />
</svg>
