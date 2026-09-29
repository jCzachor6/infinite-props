import { defineConfig } from "vite";

import { resolve } from "path";

export default defineConfig({
    base: "./",
    build: {
        outDir: "dist",
        emptyOutDir: true,
        target: "es2020",
        rollupOptions: {
            input: {
                main: resolve(__dirname, "index.html"),
                gettingStarted: resolve(__dirname, "docs/getting-started.html"),
                language: resolve(__dirname, "docs/language.html"),
                format: resolve(__dirname, "docs/format.html"),
                backends: resolve(__dirname, "docs/backends.html"),
                faq: resolve(__dirname, "docs/faq.html"),
            },
        },
    },
});
