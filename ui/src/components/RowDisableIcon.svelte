<script>
  /** Circle with vertical line — channel mute / row power. */
  /** @type {{ class?: string }} */
  let { class: className = "h-4 w-4" } = $props();
</script>

<svg
  xmlns="http://www.w3.org/2000/svg"
  viewBox="0 0 24 24"
  class={className}
  aria-hidden="true"
>
  <path
    d="M12 3v9"
    fill="none"
    stroke="currentColor"
    stroke-width="3"
    stroke-linecap="round"
  />
  <path
    d="M18.36 6.64a9 9 0 1 1-12.73 0"
    fill="none"
    stroke="currentColor"
    stroke-width="3"
    stroke-linecap="round"
  />
</svg>
