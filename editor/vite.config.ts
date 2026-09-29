import { defineConfig } from "vite";

// The studio renderer bundles to dist/; Electron loads dist/index.html.
export default defineConfig({
    base: "./",
    build: {
        outDir: "dist",
        emptyOutDir: true,
        target: "es2020",
    },
});
