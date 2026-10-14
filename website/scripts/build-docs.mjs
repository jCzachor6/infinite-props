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
        t: "Language: " + h.title.replace(/`/g, ""), p: "language.html", a: h.anchor
    }));
    return { html: md.renderer.render(tokens, md.options, {}), search };
}

// The same dependency-free reader protects SDKs, archives and website inputs.
export function verifyBundle(bundle, compiler = null) {
    const args = [path.join(root, "scripts/language_docs.py"), "verify", "--output-dir", bundle];
    if (compiler) args.push("--against-source", "--compiler", compiler);
    const verified = spawnSync(process.env.IPROP_PYTHON || "python3", args, { encoding: "utf8" });
    if (verified.status !== 0) throw new Error(verified.stderr || String(verified.error));
}

// These public trees are generated. Keep the authored navigation script while
// replacing the payload, so removed chapters and reports cannot survive a sync.
export function syncDocumentation(bundle, publicDir) {
    const docsDir = path.join(publicDir, "docs");
    if (fs.existsSync(docsDir)) {
        for (const entry of fs.readdirSync(docsDir)) {
            if (entry !== "nav.js") fs.rmSync(path.join(docsDir, entry), { recursive: true, force: true });
        }
    }
    for (const directory of ["examples", "spec", "reference", "backends", "editor", "website"])
        fs.rmSync(path.join(publicDir, directory), { recursive: true, force: true });
    for (const directory of ["docs", "examples", "spec", "backends", "editor", "website"])
        if (fs.existsSync(path.join(bundle, directory)))
            fs.cpSync(path.join(bundle, directory), path.join(publicDir, directory), { recursive: true });
    fs.copyFileSync(path.join(bundle, "LLM_REFERENCE.md"), path.join(publicDir, "LLM_REFERENCE.md"));
}

export function buildSite(compiler = process.env.IPROP_COMPILER || path.join(root, "build/tools", process.platform === "win32" ? "ipc.exe" : "ipc")) {
    const bundle = path.resolve(process.env.IPROP_LANGUAGE_DOCS_DIR || path.join(root, "build/language-docs"));
    const result = spawnSync(process.env.IPROP_PYTHON || "python3", [
        path.join(root, "scripts/language_docs.py"), "build", "--compiler", path.resolve(root, compiler), "--output-dir", bundle
    ], { encoding: "utf8" });
    if (result.status !== 0) throw new Error(result.stderr || String(result.error));
    process.stdout.write(result.stdout);
    verifyBundle(bundle, path.resolve(root, compiler));
    const info = JSON.parse(fs.readFileSync(path.join(bundle, "docs/language-manifest.json"), "utf8"));
    const minor = info.language_version;
    const book = fs.readFileSync(path.join(bundle, "docs", `language-v${minor}.md`), "utf8");
    const rendered = renderBook(book, info.headings, bundle);
    const publicDir = path.join(root, "website/public");
    syncDocumentation(bundle, publicDir);
    const template = fs.readFileSync(path.join(root, "website/templates/language.html"), "utf8");
    const html = template.replace("{{CONTENT}}", rendered.html)
        .replaceAll("{{VERSION}}", info.language_version).replaceAll("{{MINOR}}", minor)
        .replaceAll("{{PRODUCT_VERSION}}", info.product_version)
        .replaceAll("{{FORMAT_VERSION}}", info.format_version)
        .replaceAll("{{C_API_VERSION}}", info.c_api_version)
        .replaceAll("{{C_ABI_SOVERSION}}", info.c_abi_soversion);
    fs.writeFileSync(path.join(root, "website/docs/language.html"), html);
    fs.writeFileSync(path.join(publicDir, "docs/language-index.js"), "const LANGUAGE_SECTIONS = " + JSON.stringify(rendered.search, null, 2) + ";\n");
    console.log(`Rendered handbook: ${info.headings.length} headings, ${rendered.search.length} search entries`);
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    try { buildSite(process.argv[2]); }
    catch (error) { console.error("documentation build:", error.message); process.exitCode = 1; }
}
