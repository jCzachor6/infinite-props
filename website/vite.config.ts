import { defineConfig, type HtmlTagDescriptor, type Plugin } from "vite";

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

const SITE_URL = "https://infiniteprops.dev/";
const SOCIAL_IMAGE = new URL("social-preview.png", SITE_URL).href;
const SOCIAL_IMAGE_ALT = "Infinite Props: procedural 2D assets, with sixteen generated butterflies in a collector display.";
const pageUrl = (page: string) => new URL(page === "index.html" ? "" : page, SITE_URL).href;

// The versions the pages quote: the product from package.json, the rest
// from the compiler-backed documentation manifest written by `npm run docs`.
// The file format is unversioned; its specification follows the product line.
function versions() {
    const product = JSON.parse(fs.readFileSync(resolve(__dirname, "package.json"), "utf8")).version;
    const manifest = JSON.parse(fs.readFileSync(resolve(__dirname, "public/docs/language-manifest.json"), "utf8"));
    return {
        PRODUCT_VERSION: product,
        SPEC_VERSION: product.split(".").slice(0, 2).join("."),
        LANGUAGE_VERSION: manifest.language_version,
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

// Metadata is emitted into the static HTML, after partials and versions resolve.
// Canonicals, social URLs and the sitemap share the same page inventory.
function siteMetadata(): Plugin {
    return {
        name: "iprop-site-metadata",
        transformIndexHtml: {
            order: "post",
            handler(html, context) {
                const page = path.relative(__dirname, context.filename).split(path.sep).join("/");
                if (!Object.values(PAGES).includes(page)) throw new Error(`Unknown SEO page: ${page}`);
                const title = html.match(/<title>([^<]+)<\/title>/)?.[1]
                    .replaceAll("&amp;", "&");
                const description = html.match(/<meta name="description" content="([^"]+)"\s*\/?>/)?.[1];
                if (!title || !description) throw new Error(`${page}: missing title or meta description`);
                const url = pageUrl(page);
                const meta = (key: string, value: string, property = false): HtmlTagDescriptor => ({
                    tag: "meta", attrs: { [property ? "property" : "name"]: key, content: value }, injectTo: "head",
                });
                const graph: Record<string, unknown>[] = [{
                    "@type": page.startsWith("docs/") ? "TechArticle" : "WebPage",
                    "@id": url + "#page", url, name: title, description, inLanguage: "en",
                    isPartOf: { "@id": SITE_URL + "#website" },
                    about: { "@id": SITE_URL + "#software" },
                }];
                if (page === "index.html") graph.push({
                    "@type": "WebSite", "@id": SITE_URL + "#website",
                    name: "Infinite Props", url: SITE_URL, inLanguage: "en",
                }, {
                    "@type": "SoftwareSourceCode", "@id": SITE_URL + "#software",
                    name: "Infinite Props", url: SITE_URL, description, image: SOCIAL_IMAGE,
                    codeRepository: "https://github.com/jCzachor6/infinite-props",
                    programmingLanguage: ["C++", "C"], version: versions().PRODUCT_VERSION,
                });
                return { html, tags: [
                    { tag: "link", attrs: { rel: "canonical", href: url }, injectTo: "head" },
                    meta("robots", "index, follow, max-image-preview:large"),
                    meta("og:type", "website", true),
                    meta("og:site_name", "Infinite Props", true),
                    meta("og:locale", "en_US", true),
                    meta("og:title", title, true),
                    meta("og:description", description, true),
                    meta("og:url", url, true),
                    meta("og:image", SOCIAL_IMAGE, true),
                    meta("og:image:type", "image/png", true),
                    meta("og:image:width", "1200", true),
                    meta("og:image:height", "630", true),
                    meta("og:image:alt", SOCIAL_IMAGE_ALT, true),
                    meta("twitter:card", "summary_large_image"),
                    meta("twitter:title", title),
                    meta("twitter:description", description),
                    meta("twitter:image", SOCIAL_IMAGE),
                    meta("twitter:image:alt", SOCIAL_IMAGE_ALT),
                    { tag: "script", attrs: { type: "application/ld+json" },
                        children: JSON.stringify({ "@context": "https://schema.org", "@graph": graph }).replaceAll("<", "\\u003c"),
                        injectTo: "head" },
                ] };
            },
        },
        generateBundle() {
            this.emitFile({ type: "asset", fileName: "robots.txt",
                source: `User-agent: *\nAllow: /\n\nSitemap: ${SITE_URL}sitemap.xml\n` });
            const urls = Object.values(PAGES).map(page => `  <url><loc>${pageUrl(page)}</loc></url>`).join("\n");
            this.emitFile({ type: "asset", fileName: "sitemap.xml",
                source: `<?xml version="1.0" encoding="UTF-8"?>\n<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">\n${urls}\n</urlset>\n` });
        },
    };
}

export default defineConfig({
    base: "./",
    plugins: [sitePartials(), siteMetadata()],
    build: {
        outDir: "dist",
        emptyOutDir: true,
        target: "es2020",
        rollupOptions: {
            input: Object.fromEntries(Object.entries(PAGES).map(([name, file]) => [name, resolve(__dirname, file)])),
        },
    },
});
