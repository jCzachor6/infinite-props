// Check the actual built website assets, current documentation and WASM.
"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const vm = require("node:vm");
const { spawnSync } = require("node:child_process");
const crypto = require("node:crypto");
const { decodePng } = require("../../backends/html5/test/png");

const root = path.resolve(__dirname, "../..");
const site = path.resolve(process.argv[2] || path.join(root, "website/dist"));
const tools = path.resolve(process.argv[3] || path.join(root, "build/tools"));
const generator = path.join(tools, fs.existsSync(path.join(tools, "ipgen.exe")) ? "ipgen.exe" : "ipgen");

function checkLinks(directory) {
    let pages = 0;
    for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
        const file = path.join(directory, entry.name);
        if (entry.isDirectory()) pages += checkLinks(file);
        else if (entry.name.endsWith(".html")) {
            pages++;
            const html = fs.readFileSync(file, "utf8");
            for (const match of html.matchAll(/(?:src|href)=["']([^"']+)["']/g)) {
                const link = match[1];
                if (/^(?:[a-z]+:|\/\/|#)/i.test(link)) continue;
                const target = path.resolve(path.dirname(file), decodeURIComponent(link.split(/[?#]/)[0]));
                assert.ok(target === site || target.startsWith(site + path.sep), `link escapes website: ${link}`);
                assert.ok(fs.existsSync(target), `${file}: missing ${link}`);
            }
        }
    }
    return pages;
}

async function main() {
    const pages = checkLinks(site);
    assert.equal(pages, 7);
    const specs = fs.readdirSync(path.join(root, "spec")).filter(name => /^iprop-v.*\.md$/.test(name)).sort();
    assert.deepEqual(specs, ["iprop-v0.7.md"]);
    assert.deepEqual(fs.readdirSync(path.join(site, "spec")).filter(name => /^iprop-v.*\.md$/.test(name)).sort(), specs);
    for (const spec of specs) {
        assert.deepEqual(fs.readFileSync(path.join(site, "spec", spec)), fs.readFileSync(path.join(root, "spec", spec)));
    }
    const recipes = fs.readdirSync(path.join(site, "recipes")).filter(name => name.endsWith(".ips"));
    assert.equal(recipes.length, 24);
    for (const recipe of recipes) {
        assert.deepEqual(fs.readFileSync(path.join(site, "recipes", recipe)),
                         fs.readFileSync(path.join(root, "examples/recipes", recipe)));
    }
    const nav = vm.runInNewContext(fs.readFileSync(path.join(site, "docs/language-index.js"), "utf8") +
        "\n" + fs.readFileSync(path.join(site, "docs/nav.js"), "utf8") +
        "\n({ SECTIONS })", { document: { addEventListener() {} } });
    for (const section of nav.SECTIONS) {
        const html = fs.readFileSync(path.join(site, "docs", section.p), "utf8");
        assert.ok(html.includes(`id="${section.a}"`), `missing search anchor: ${section.t}`);
    }
    const docs = JSON.parse(fs.readFileSync(path.join(site, "docs/language-manifest.json"), "utf8"));
    assert.equal(docs.schema_version, 3);
    const catalogue = JSON.parse(fs.readFileSync(path.join(site, "docs/language.json"), "utf8"));
    assert.equal(catalogue.schema_version, 4);
    assert.equal(catalogue.source_rules.asset_constants.evaluation, "compile_time");
    const version = JSON.parse(fs.readFileSync(path.join(root, "website/package.json"), "utf8")).version;
    assert.equal(docs.product_version, version);
    const commit = spawnSync("git", ["rev-parse", "HEAD"], { cwd: root, encoding: "utf8" });
    assert.equal(commit.status, 0);
    assert.equal(docs.source_commit, commit.stdout.trim());
    const hash = bytes => crypto.createHash("sha256").update(bytes).digest("hex");
    for (const [file, digest] of Object.entries(docs.files))
        assert.equal(hash(fs.readFileSync(path.join(site, file))), digest, `changed documentation: ${file}`);
    for (const [file, digest] of Object.entries(docs.source_files))
        assert.equal(hash(Buffer.from(fs.readFileSync(path.join(root, file), "utf8").replace(/\r\n/g, "\n"))), digest, `stale documentation input: ${file}`);
    const compiler = path.join(tools, fs.existsSync(path.join(tools, "ipc.exe")) ? "ipc.exe" : "ipc");
    const verified = spawnSync(process.env.IPROP_PYTHON || "python3", [
        path.join(root, "scripts/language_docs.py"), "verify", "--output-dir", site,
        "--against-source", "--compiler", compiler
    ], { encoding: "utf8" });
    assert.equal(verified.status, 0, verified.stderr || String(verified.error));
    const described = spawnSync(compiler, ["--describe-language"], { encoding: "utf8" });
    assert.equal(described.status, 0, described.stderr);
    assert.deepEqual(JSON.parse(fs.readFileSync(path.join(site, "docs/language.json"), "utf8")), JSON.parse(described.stdout));
    const book = fs.readFileSync(path.join(site, "docs", `language-v${docs.language_version}.md`));
    assert.ok(book.length > 0);
    // A correct manifest must not mask stale files left by a previous build.
    const inventory = directory => fs.readdirSync(directory, { withFileTypes: true }).flatMap(entry =>
        entry.isDirectory() ? inventory(path.join(directory, entry.name)) : [path.relative(site, path.join(directory, entry.name)).replaceAll(path.sep, "/")]);
    for (const directory of ["docs", "examples", "spec", "backends", "editor", "website"]) {
        const expected = Object.keys(docs.files).filter(file => file.startsWith(directory + "/"));
        if (directory === "docs") expected.push("docs/language-manifest.json", "docs/nav.js", "docs/language-index.js");
        assert.deepEqual(inventory(path.join(site, directory)).filter(file => !file.endsWith(".html")).sort(), expected.sort());
    }
    assert.ok(!fs.existsSync(path.join(site, "reference")), "stale duplicate reference tree");
    // The home page showcase: every asset a tile or a game slot renders, the
    // binaries from examples/showcase (their sources are not public).
    const home = fs.readFileSync(path.join(site, "index.html"), "utf8");
    const showcase = [...new Set([...home.matchAll(/data-(?:asset|silhouette)="([\w-]+)"/g)].map(match => match[1]))];
    assert.equal(showcase.length, 7);
    const Module = await require(path.join(site, "iprop.js"))({ locateFile: () => path.join(site, "iprop.wasm") });
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), "iprop-website-"));
    let comparisons = 0;
    try {
        for (const asset of showcase) {
            const file = path.join(site, "showcase", asset + ".iprop");
            const bytes = fs.readFileSync(file);
            assert.deepEqual(bytes, fs.readFileSync(path.join(root, "examples/showcase", asset + ".iprop")),
                             `stale showcase asset: ${asset}`);
            // The former version bytes are reserved zeros.
            assert.equal(bytes.readUInt32LE(4), 0);
            assert.equal(bytes.readUInt32LE(8) & 1, 0);
            assert.equal(bytes.readUInt32LE(8) & 2, 2);
            for (const seed of [0, 7, 26]) for (const choice of [0, 2]) {
                const rendered = Module.generate(bytes, seed, 0, 0, {v: choice});
                const png = path.join(temporary, "native.png");
                const native = spawnSync(generator, [file, "--seed", String(seed), "--uniform", "v=" + choice, "-o", png],
                                        { encoding: "utf8" });
                assert.equal(native.status, 0, native.stderr || String(native.error));
                const decoded = decodePng(fs.readFileSync(png));
                assert.equal(rendered.width, decoded.width);
                assert.equal(rendered.height, decoded.height);
                assert.deepEqual(Buffer.from(rendered.data), decoded.data, `${asset} seed ${seed} input v ${choice}`);
                comparisons++;
            }
        }
    } finally { fs.rmSync(temporary, { recursive: true, force: true }); }
    console.log(`website release smoke: PASS (${pages} pages, local links/search anchors, ${docs.builtin_count} documented builtins, ${docs.example_count} language examples, ${specs.length} spec, 24 recipes, ${showcase.length} showcase assets, ${comparisons} exact native/WASM images)`);
}

main().catch(error => { console.error(error); process.exit(1); });
