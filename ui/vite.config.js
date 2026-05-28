import { defineConfig } from "vite";
import { svelte } from "@sveltejs/vite-plugin-svelte";
import tailwindcss from "@tailwindcss/vite";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));

export default defineConfig({
  plugins: [svelte(), tailwindcss()],
  resolve: {
    alias: {
      "@juce": path.resolve(__dirname, "../JUCE/modules/juce_gui_extra/native/javascript"),
    },
  },
  server: {
    port: 5173,
    strictPort: true,
    cors: true,
  },
  build: {
    outDir: "../assets/webview/dist",
    emptyOutDir: true,
  },
});
