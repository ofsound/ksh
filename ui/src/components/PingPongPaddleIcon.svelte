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
  <ellipse cx="10.5" cy="9.5" rx="7.2" ry="8" fill="currentColor" transform="rotate(-28 10.5 9.5)" />
  <path
    fill="currentColor"
    d="M13.2 15.4 L18.8 22.1 C19.35 22.75 20.35 22.55 20.6 21.75 L21.35 19.35 C21.55 18.7 21.15 18.05 20.5 17.85 L14.55 15.85 Z"
  />
</svg>
