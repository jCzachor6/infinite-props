"use strict";
const { test } = require("node:test");
const assert = require("node:assert/strict");
const path = require("node:path");
const { execFileSync } = require("node:child_process");
const { entries, registerAuthoring } = require("../authoring");
const compiler = process.env.IPROP_COMPILER || path.resolve(__dirname, "../../build/tools/ipc");

test("hints expose real compiler overloads, namespace constants and handle types", () => {
    const catalog = JSON.parse(execFileSync(compiler, ["--describe-language"], { encoding: "utf8" }));
    const hints = entries(catalog);
    for (const name of ["noise.mask", "regions.gap", "part.draw", "parts.from_regions", "noise.below", "voronoi.grid", "contour.four", "surface.source_over", "parts2d"])
        assert.ok(hints.some(h => h.name === name), name);
    assert.equal(hints.filter(h => h.signatures.length).length, catalog.builtins.length);
    assert.match(hints.find(h => h.name === "noise.mask").signatures.join("\n"), /warp_amount: f32/);
    assert.throws(() => entries({ ...catalog, schema_version: 5 }), /unsupported/);
});

test("completion replaces the full qualified name and hover uses compiler signatures", async () => {
    const providers = {}, context = { subscriptions: [] }, range = { start: 0, end: 10 };
    const vscode = {
        languages: {
            registerCompletionItemProvider(_s, p) { providers.completion = p; return {}; },
            registerHoverProvider(_s, p) { providers.hover = p; return {}; }
        },
        CompletionItemKind: { Function: 1, Constant: 2, TypeParameter: 3 },
        CompletionItem: class { constructor(label, kind) { Object.assign(this, { label, kind }); } },
        MarkdownString: class { appendCodeblock(text) { this.value = text; } },
        Hover: class { constructor(contents, range) { Object.assign(this, { contents, range }); } }
    };
    registerAuthoring(vscode, context, () => compiler);
    const document = { uri: { fsPath: __filename }, getWordRangeAtPosition: () => range, getText: () => "part.draw" };
    const items = await providers.completion.provideCompletionItems(document, {}, {});
    assert.equal(items.find(i => i.label === "part.draw").range, range);
    const hover = await providers.hover.provideHover(document, {}, {});
    assert.match(hover.contents.value, /part.draw\(p: part2d\) -> void/);
    assert.equal(hover.range, range);
    assert.deepEqual(await providers.completion.provideCompletionItems(document, {}, { isCancellationRequested: true }), []);
});
