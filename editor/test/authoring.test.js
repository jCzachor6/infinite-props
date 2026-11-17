"use strict";
const { test } = require("node:test");
const assert = require("node:assert/strict");
const path = require("node:path");
const { execFileSync } = require("node:child_process");
const fs = require("node:fs");
const { entries, describe, registerAuthoring } = require("../authoring");
const compiler = process.env.IPROP_COMPILER || path.resolve(__dirname, "../../build/tools/ipc");

test("hints expose real compiler overloads, namespace constants and handle types", () => {
    const catalog = JSON.parse(execFileSync(compiler, ["--describe-language"], { encoding: "utf8" }));
    const hints = entries(catalog);
    for (const name of ["noise.mask", "regions.gap", "part.draw", "parts.from_regions", "noise.below", "voronoi.grid", "contour.four", "surface.source_over", "parts2d"])
        assert.ok(hints.some(h => h.name === name), name);
    const models = catalog.source_rules.models;
    assert.equal(hints.filter(h => h.signatures.length).length, catalog.builtins.length + models.builtins.length);
    assert.match(hints.find(h => h.name === "noise.mask").signatures.join("\n"), /warp_amount: f32/);
    assert.throws(() => entries({ ...catalog, schema_version: 5 }), /unsupported/);
    assert.throws(() => entries({ ...catalog, schema_version: 7 }), /unsupported/);
});

test("hints cover the builtins and types of render assets and say where they belong", () => {
    const catalog = JSON.parse(execFileSync(compiler, ["--describe-language"], { encoding: "utf8" }));
    const hints = entries(catalog);
    const named = name => hints.filter(h => h.name === name);
    for (const builtin of catalog.source_rules.models.builtins) {
        assert.equal(named(builtin.name).length, 1, builtin.name);
        assert.equal(named(builtin.name)[0].signatures.length, builtin.overloads.length, builtin.name);
        assert.match(describe(named(builtin.name)[0]), /^\/\/ in an asset with render\n/, builtin.name);
    }
    assert.deepEqual(named("box")[0].signatures, [
        "box(at: vec3, size: vec3, skin: skin) -> void",
        "box(at: vec3, size: vec3, bevel: f32, skin: skin) -> void"]);
    assert.equal(named("skin.density")[0].signatures[0], "skin.density(skin: skin, texels: f32) -> skin");
    // One entry for lerp: its 2D overloads, then the one a render asset adds.
    const lerp = named("lerp");
    assert.equal(lerp.length, 1);
    assert.ok(lerp[0].signatures.includes("lerp(a: vec3, b: vec3, t: f32) -> vec3  // in an asset with render"));
    assert.ok(lerp[0].signatures.some(s => /^lerp\(a: f32, b: f32, t: f32\) -> f32$/.test(s)));
    assert.equal(lerp[0].note, undefined);
    for (const type of ["vec3", "skin"]) {
        assert.equal(named(type).length, 1, type);
        assert.equal(named(type)[0].kind, "TypeParameter");
    }
    // A builtin of a draw asset says nothing about render.
    assert.equal(describe(named("rect")[0]).includes("render"), false);
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
    for (const { name } of [...catalog.builtins, ...catalog.source_rules.models.builtins]) {
        const match = builtin.exec("x = " + name + "(1)");
        assert.equal(match && match[1], name, name);
    }
    // A member call is not a bare builtin, and a value named like one is not a call.
    for (const source of ["items.pick(3)", "noise(1.0, 2.0, 3)", "fill(d, 1.0)", "stream_push(1)", "i32trunc(1.5)",
        "refill(1)", "rect = 2", "box = 2", "skin = wood", "toolbox(1)", "s.fit()", "vec3 p = up"])
        assert.equal(builtin.test(source), false, source);
});

test("the words of a model highlight only where the grammar gives them a meaning", () => {
    const grammar = JSON.parse(fs.readFileSync(path.join(__dirname, "../syntaxes/ips.tmLanguage.json"), "utf8"));
    const keyword = new RegExp(grammar.repository.modelKeywords.match);
    for (const source of ["  render {", "render{", "  scale 100", "    place(at = {0, 4, 0}) {", "place (yaw = 90.0) {",
        "    smooth(40.0) {", "bend(90.0) { refine(4) { box(a, b, s) } }", "displace(1.5, 0.1, root.fork(\"dents\")) {"])
        assert.ok(keyword.test(source), source);
    // A helper, a local or an argument of those names is an ordinary name.
    for (const source of ["render(1.0)", "render = 2", "scale = 1.5", "put(scale = 1.6) {",
        "x = scale * 2.0", "place = 3", "replace(1)", "x = smooth(0.4)", "bend = 3", "taper(2)", "if (smooth(t) > 0.5) {",
        "if (bend(a, b) == 0) { x = 1 }"])
        assert.equal(keyword.test(source), false, source);
    const skins = grammar.repository.modelTypes.patterns.map(pattern => new RegExp(pattern.match));
    const typed = source => skins.some(pattern => pattern.test(source));
    for (const source of ["leg(at: vec3, s: skin) {", "dull(s: skin) -> skin {", "    skin oak = wood.draw(1)"])
        assert.ok(typed(source), source);
    for (const source of ["skin = wood.draw(1)", "box(at, size, skin)", "x = skin.fit(s)", "skins = 3"])
        assert.equal(typed(source), false, source);
    const types = new RegExp(grammar.repository.types.match);
    assert.ok(types.test("leg(at: vec3, s: skin) {"));
    assert.ok(types.test("const vec3 up = {0, 1, 0}"));
    // The model keywords come before the general ones, the skin type before the builtins' neighbours.
    const order = grammar.patterns.map(pattern => pattern.include);
    assert.ok(order.indexOf("#modelKeywords") < order.indexOf("#keywords"));
    assert.ok(order.indexOf("#builtins") < order.indexOf("#modelTypes"));
});

test("named seeds highlight only where the grammar gives them a meaning", () => {
    const grammar = JSON.parse(fs.readFileSync(path.join(__dirname, "../syntaxes/ips.tmLanguage.json"), "utf8"));
    const type = new RegExp(grammar.repository.seedType.match);
    const method = new RegExp(grammar.repository.seedMethod.match);
    const builtin = new RegExp(grammar.repository.builtins.match);
    assert.equal(type.exec("wrap(base: seed) -> seed {")[1], "seed");
    // As an expression `seed` still reads the stream; `fork` stays an ordinary name.
    for (const source of ["x = seed", "start = seed ^ 1", "x = seed.fork(1)"]) assert.equal(type.test(source), false, source);
    assert.ok(method.test("leaves.fork(\"leaf\")"));
    assert.equal(method.test("fork(1)"), false);
    assert.equal(builtin.exec("x = seed.initial()")[1], "seed.initial");
    assert.equal(builtin.test("x = seed.fork(1)"), false);
});
