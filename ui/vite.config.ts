import react from "@vitejs/plugin-react";
import { defineConfig } from "vite";
import { viteSingleFile } from "vite-plugin-singlefile";

// Everything ends up in one HTML file, which is embedded in why.exe.
export default defineConfig({
  plugins: [react(), viteSingleFile()],
  server: { port: 5199, strictPort: true },
  build: { target: "es2022", cssCodeSplit: false, assetsInlineLimit: 100_000_000 },
});
