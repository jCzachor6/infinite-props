// Check the actual built website assets and WASM, including packed 0.4 files.
"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const vm = require("node:vm");
const { spawnSync } = require("node:child_process");
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
    assert.equal(pages, 6);
    for (const minor of [1, 2, 3, 4]) {
        const spec = `iprop-v0.${minor}.md`;
        assert.deepEqual(fs.readFileSync(path.join(site, "spec", spec)), fs.readFileSync(path.join(root, "spec", spec)));
    }
    const recipes = fs.readdirSync(path.join(site, "recipes")).filter(name => name.endsWith(".ips"));
    assert.equal(recipes.length, 24);
    for (const recipe of recipes) {
        assert.deepEqual(fs.readFileSync(path.join(site, "recipes", recipe)),
                         fs.readFileSync(path.join(root, "examples/recipes", recipe)));
    }
    const nav = vm.runInNewContext(fs.readFileSync(path.join(site, "docs/nav.js"), "utf8") +
        "\n({ PAGES, SECTIONS })", { document: { addEventListener() {} } });
    for (const section of nav.SECTIONS) {
        const html = fs.readFileSync(path.join(site, "docs", section.p), "utf8");
        assert.ok(html.includes(`id="${section.a}"`), `missing search anchor: ${section.t}`);
    }
    const Module = await require(path.join(site, "iprop.js"))({ locateFile: () => path.join(site, "iprop.wasm") });
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), "iprop-website-"));
    let comparisons = 0;
    try {
        for (const [asset, variants] of [["portrait", [0, 7]], ["plant", [0]], ["noise", [0]]]) {
            const file = path.join(site, asset + ".iprop");
            const bytes = fs.readFileSync(file);
            if (asset === "noise") {
                assert.equal(bytes.readUInt16LE(6), 4);
                assert.equal(bytes.readUInt32LE(8) & 1, 0);
                assert.equal(bytes.readUInt32LE(8) & 2, 2);
            }
            for (const seed of [0, 7, 26]) for (const variant of variants) {
                const rendered = Module.generate(bytes, seed, variant, 0, 0, 0);
                const png = path.join(temporary, "native.png");
                const native = spawnSync(generator, [file, "--seed", String(seed), "--variant", String(variant), "-o", png],
                                        { encoding: "utf8" });
                assert.equal(native.status, 0, native.stderr || String(native.error));
                const decoded = decodePng(fs.readFileSync(png));
                assert.equal(rendered.width, decoded.width);
                assert.equal(rendered.height, decoded.height);
                assert.deepEqual(Buffer.from(rendered.data), decoded.data, `${asset} seed ${seed} variant ${variant}`);
                comparisons++;
            }
        }
    } finally { fs.rmSync(temporary, { recursive: true, force: true }); }
    console.log(`website release smoke: PASS (${pages} pages, local links/search anchors, 4 specs, 24 recipes, ${comparisons} exact native/WASM images)`);
}

main().catch(error => { console.error(error); process.exit(1); });
