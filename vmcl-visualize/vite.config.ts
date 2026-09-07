import { defineConfig } from "vite";
export default defineConfig({
  base: "./",
  server: { host: "127.0.0.1", port: 5174, strictPort: true },
  preview: { host: "127.0.0.1", port: 4174, strictPort: true },
  worker: { format: "es" },
});
