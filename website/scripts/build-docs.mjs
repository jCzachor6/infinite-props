// Render the compiler-backed handbook into the existing static Vite site.
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { spawnSync } from "node:child_process";
import MarkdownIt from "markdown-it";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");

export function renderBook(markdown, headings, bundle) {
    const md = new MarkdownIt({ html: false, linkify: false, typographer: false });
    const tokens = md.parse(markdown, {});
    const anchors = new Set(headings.map(h => h.anchor));
    if (anchors.size !== headings.length) throw new Error("Duplicate handbook anchor");
    let index = 0;
    for (let i = 0; i < tokens.length; i++) {
        const token = tokens[i];
        if (token.type === "heading_open") {
            const heading = headings[index++];
            const inline = tokens[i + 1];
            if (!heading || inline.content !== heading.title || token.tag !== `h${heading.level}`)
                throw new Error("Markdown headings differ from the documentation manifest");
            token.attrSet("id", heading.anchor);
        }
        for (const child of token.children || []) {
            const href = child.attrGet("href") || child.attrGet("src");
            if (!href || /^(?:[a-z]+:|\/\/)/i.test(href)) continue;
            const [file, fragment] = href.split("#");
            if (!file) {
                if (!anchors.has(decodeURIComponent(fragment))) throw new Error("Missing handbook anchor: " + href);
            } else {
                const target = path.resolve(bundle, "docs", decodeURIComponent(file));
                if (!target.startsWith(path.resolve(bundle) + path.sep) || !fs.existsSync(target))
                    throw new Error("Missing or escaping handbook link: " + href);
            }
        }
    }
    if (index !== headings.length) throw new Error("Handbook headings are incomplete");
    const search = headings.filter(h => h.level > 1).map(h => ({
        t: "Language — " + h.title.replace(/`/g, ""), p: "language.html", a: h.anchor
    }));
    return { html: md.renderer.render(tokens, md.options, {}), search };
}

export function buildSite(compiler = process.env.IPROP_COMPILER || path.join(root, "build/tools", process.platform === "win32" ? "ipc.exe" : "ipc")) {
    const bundle = path.join(root, "build/language-docs");
    const result = spawnSync(process.env.IPROP_PYTHON || "python3", [
        path.join(root, "scripts/language_docs.py"), "build", "--compiler", path.resolve(root, compiler), "--output-dir", bundle
    ], { encoding: "utf8" });
    if (result.status !== 0) throw new Error(result.stderr || String(result.error));
    process.stdout.write(result.stdout);
    const info = JSON.parse(fs.readFileSync(path.join(bundle, "docs/language-manifest.json"), "utf8"));
    const minor = info.product_version.split(".").slice(0, 2).join(".");
    const book = fs.readFileSync(path.join(bundle, "docs", `language-v${minor}.md`), "utf8");
    const rendered = renderBook(book, info.headings, bundle);
    const publicDir = path.join(root, "website/public");
    for (const directory of ["docs", "examples", "spec"])
        fs.cpSync(path.join(bundle, directory), path.join(publicDir, directory), { recursive: true });
    // Preserve the old download URL as a generated copy of the same book.
    fs.mkdirSync(path.join(publicDir, "reference"), { recursive: true });
    fs.copyFileSync(path.join(bundle, "docs", `language-v${minor}.md`), path.join(publicDir, "reference", `language-v${minor}.md`));
    const template = fs.readFileSync(path.join(root, "website/templates/language.html"), "utf8");
    const html = template.replace("{{CONTENT}}", rendered.html)
        .replaceAll("{{VERSION}}", info.product_version).replaceAll("{{MINOR}}", minor);
    fs.writeFileSync(path.join(root, "website/docs/language.html"), html);
    fs.writeFileSync(path.join(publicDir, "docs/language-index.js"), "const LANGUAGE_SECTIONS = " + JSON.stringify(rendered.search, null, 2) + ";\n");
    console.log(`Rendered handbook: ${info.headings.length} headings, ${rendered.search.length} search entries`);
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    try { buildSite(process.argv[2]); }
    catch (error) { console.error("documentation build:", error.message); process.exitCode = 1; }
}
