<script>
  /** Forward path that turns 180 degrees and returns parallel in the opposite direction. */
  /** @type {{ class?: string }} */
  let { class: className = "h-[23px] w-[24px]" } = $props();
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
    stroke-width="2.8"
    d="M2.5 6.5h12c3.6 0 6.5 2.4 6.5 5.5s-2.9 5.5-6.5 5.5H9"
  />
  <path
    fill="currentColor"
    d="M9.5 10.2 1.2 17l8.3 6.8Z"
  />
</svg>
