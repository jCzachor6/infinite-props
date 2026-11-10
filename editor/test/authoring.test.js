"use strict";
const { test } = require("node:test");
const assert = require("node:assert/strict");
const path = require("node:path");
const { execFileSync } = require("node:child_process");
const fs = require("node:fs");
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
    assert.match(hover.contents.value, /part.draw\(part: part2d\) -> void/);
    assert.equal(hover.range, range);
    assert.deepEqual(await providers.completion.provideCompletionItems(document, {}, { isCancellationRequested: true }), []);
});

test("the grammar highlights every builtin the compiler reports, and no retired spelling", () => {
    const catalog = JSON.parse(execFileSync(compiler, ["--describe-language"], { encoding: "utf8" }));
    const grammar = JSON.parse(fs.readFileSync(path.join(__dirname, "../syntaxes/ips.tmLanguage.json"), "utf8"));
    const builtin = new RegExp(grammar.repository.builtins.match);
    for (const { name } of catalog.builtins) {
        const match = builtin.exec("x = " + name + "(1)");
        assert.equal(match && match[1], name, name);
    }
    // A member call is not a bare builtin, and a value named like one is not a call.
    for (const source of ["items.pick(3)", "noise(1.0, 2.0, 3)", "fill(d, 1.0)", "stream_push(1)", "i32trunc(1.5)",
        "refill(1)", "rect = 2"])
        assert.equal(builtin.test(source), false, source);
});
