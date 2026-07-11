<script>
  /** Forward path that turns 180 degrees and returns parallel in the opposite direction. */
  /** @type {{ class?: string }} */
  let { class: className = "h-[21px] w-[21px]" } = $props();
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
    stroke-width="2.4"
    d="M3 7h12c3.3 0 6 2.2 6 5s-2.7 5-6 5H7"
  />
  <path
    fill="none"
    stroke="currentColor"
    stroke-linecap="round"
    stroke-linejoin="round"
    stroke-width="2.4"
    d="m7 13-4 4 4 4"
  />
</svg>
