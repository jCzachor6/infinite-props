import { defineConfig, type Plugin } from "vite";

import fs from "fs";
import path, { resolve } from "path";

// Every page of the site, relative to this directory.
const PAGES = {
    main: "index.html",
    source: "source.html",
    gettingStarted: "docs/getting-started.html",
    language: "docs/language.html",
    format: "docs/format.html",
    backends: "docs/backends.html",
    faq: "docs/faq.html",
};

// The versions the pages quote: the product from package.json, the rest
// from the compiler-backed documentation manifest written by `npm run docs`.
function versions() {
    const product = JSON.parse(fs.readFileSync(resolve(__dirname, "package.json"), "utf8")).version;
    const manifest = JSON.parse(fs.readFileSync(resolve(__dirname, "public/docs/language-manifest.json"), "utf8"));
    return {
        PRODUCT_VERSION: product,
        LANGUAGE_VERSION: manifest.language_version,
        FORMAT_VERSION: manifest.format_version,
        C_API_VERSION: manifest.c_api_version,
        C_ABI_SOVERSION: manifest.c_abi_soversion,
    };
}

// Expands `<!-- @header home -->`-style markers into the shared page chrome
// from templates/partials, marks the current page in the navigation and
// fills the {{BASE}} and version placeholders, so pages work without JS.
function sitePartials(): Plugin {
    return {
        name: "iprop-site-partials",
        transformIndexHtml: {
            order: "pre",
            handler(html, context) {
                const page = path.relative(__dirname, context.filename).split(path.sep).join("/");
                const values: Record<string, string> = {
                    BASE: "../".repeat(page.split("/").length - 1),
                    ...versions(),
                };
                const expanded = html.replace(/<!--\s*@([\w-]+)(?:\s+([\w-]+))?\s*-->/g, (_, name: string, current?: string) => {
                    let partial = fs.readFileSync(resolve(__dirname, "templates/partials", name + ".html"), "utf8");
                    if (current) {
                        if (!partial.includes(`data-page="${current}"`))
                            throw new Error(`${page}: the ${name} partial has no page '${current}'`);
                        partial = partial.replaceAll(`data-page="${current}"`, `data-page="${current}" aria-current="page"`);
                    }
                    return partial;
                });
                return expanded.replace(/\{\{([A-Z_]+)\}\}/g, (match, key: string) => values[key] ?? match);
            },
        },
    };
}

export default defineConfig({
    base: "./",
    plugins: [sitePartials()],
    build: {
        outDir: "dist",
        emptyOutDir: true,
        target: "es2020",
        rollupOptions: {
            input: Object.fromEntries(Object.entries(PAGES).map(([name, file]) => [name, resolve(__dirname, file)])),
        },
    },
});
