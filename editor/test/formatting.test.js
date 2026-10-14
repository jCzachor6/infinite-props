"use strict";
const { test } = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { EventEmitter } = require("node:events");
const { execFile, spawnSync } = require("node:child_process");
const { runCompilerFormatting } = require("../formatting");
const { editorHarness } = require("./support/editor");

const root = path.resolve(__dirname, "../..");
const compiler = process.env.IPROP_COMPILER || path.join(root, "build/tools/ipc");
const raw = "asset t { size 8x8 draw { rect(color=#abc,h=8,w=8,y=0,x=0) } }";
const canonical = "asset t {\n  size 8x8\n\n  draw {\n    rect(0, 0, 8, 8, #aabbcc)\n  }\n}\n";
function workspace(source = raw) {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "iprop-format-"));
    fs.writeFileSync(path.join(dir, "entry.ips"), source);
    return dir;
}
function token() {
    let listener;
    return { isCancellationRequested: false,
        onCancellationRequested(callback) { listener = callback; return { dispose() { listener = null; } }; },
        cancel() { this.isCancellationRequested = true; listener?.(); } };
}
const format = (h, cancellation = token()) => h.vscode.formatter.provideDocumentFormattingEdits(h.entry, {}, cancellation);

test("mask types scopes and operations highlight without restoring contextual names", () => {
    const grammar = JSON.parse(fs.readFileSync(path.join(root,"editor/syntaxes/ips.tmLanguage.json"),"utf8"));
    const mask = new RegExp(grammar.repository.maskType.match);
    const clip = new RegExp(grammar.repository.clipScope.match);
    assert.ok(mask.test("mask region = image")); assert.ok(mask.test("echo(region:mask)->mask"));
    assert.ok(clip.test("clip /* scope */ (region) {}")); assert.ok(clip.test("input i32 clip = 1"));
    assert.ok(!mask.test("maskCount")); assert.ok(!clip.test("clipCount"));
    const builtin = new RegExp(grammar.repository.builtins.match);
    for (const name of ["from","union","intersect","subtract","invert","expand","shrink","offset"])
        assert.ok(builtin.test("mask."+name+"("), name);
});

test("editor formatting retains mask comments and orders mask paint arguments", async () => {
    const source="asset t{size 3x2 echo(region:mask)->mask{return region}draw{"+
        "layer image(alpha=surface.source_over){pixel(0,0,#ffffff80)}"+
        "mask region=echo(mask.from(image))clip(/* region */region){"+
        "surface.paint(blend=surface.source_over,color=#123,image=region)}}}";
    const dir=workspace(source),h=editorHarness(dir,execFile);h.settings.compilerPath=compiler;
    try {
        const edits=await format(h);assert.equal(edits.length,1,h.warnings.join("\n"));
        const text=edits[0].newText;assert.ok(text.includes("/* region */"));
        assert.match(text,/mask region = echo\(mask\.from\(image\)\)/);
        assert.match(text,/surface\.paint\(image = region, color = #112233, blend = surface\.source_over\)/);
        const output=path.join(dir,"formatted.ips");fs.writeFileSync(output,text);
        const check=spawnSync(compiler,[output,"--check"],{encoding:"utf8"});assert.equal(check.status,0,check.stderr);
        clean(dir);
    } finally {h.dispose();fs.rmSync(dir,{recursive:true,force:true});}
});
function pending(dir) {
    const calls = [];
    const h = editorHarness(dir, (binary, args, options, callback) => { calls.push({ binary, args, options, callback }); });
    h.settings.compilerPath = path.join(dir, "test-ipc");
    fs.writeFileSync(h.settings.compilerPath, "mock compiler");
    return { h, calls, complete(index = 0, error = null, output = canonical, stderr = "") {
        const call = calls[index];
        fs.writeFileSync(call.args.at(-1), output);
        call.callback(error, "", stderr);
    } };
}
function clean(dir) { assert.equal(fs.readdirSync(dir).filter(name => name.startsWith(".iprop-format-")).length, 0); }

test("asset constants format through the editor with forward references and comments", async () => {
    const source = "asset t{size 1x1 const vec2 direction=vec2.normalize({next,4}); // direction\n" +
        "const i32 next=3 const distribution typical=gaussian(sigma=0.18,mean=0.5) draw{point(direction)} " +
        "point(value:vec2){pixel(0,0,#123)}}";
    const dir = workspace(source);
    const h = editorHarness(dir, execFile);
    h.settings.compilerPath = compiler;
    try {
        const edits = await format(h);
        assert.equal(edits.length, 1, h.warnings.join("\n"));
        const formatted = edits[0].newText;
        assert.match(formatted, /const vec2 direction = vec2\.normalize\(\{next, 4\}\)/);
        assert.ok(formatted.includes("// direction"));
        assert.ok(formatted.includes("const distribution typical = gaussian(mean = 0.5, sigma = 0.18)"));
        const target = path.join(dir, "formatted.ips");
        fs.writeFileSync(target, formatted);
        const check = spawnSync(compiler, [target, "--check"], {encoding:"utf8"});
        assert.equal(check.status, 0, check.stderr);
        assert.equal(fs.readdirSync(dir).filter(name => name.endsWith(".iprop")).length, 0);
        clean(dir);
    } finally { h.dispose(); fs.rmSync(dir, {recursive:true,force:true}); }
});

test("editor highlights the reserved const keyword in every position", () => {
    const grammar = JSON.parse(fs.readFileSync(path.join(root, "editor/syntaxes/ips.tmLanguage.json"), "utf8"));
    const introducer = new RegExp(grammar.repository.assetConstant.match);
    for (const source of ["const i32 Plain=1", "const color iron=#abc", "const distribution typical=gaussian(0,1)", "const list<vec2> points=[]"])
        assert.equal(introducer.test(source), true, source);
    for (const source of ["const(1)", "const = 1", "input i32 const=1", "const.x"])
        assert.equal(introducer.test(source), true, source);
});

test("real formatting returns one edit without saving the readonly entry or a binary", async () => {
    const source = "// " + "ż😀".repeat(18000) + "\nasset t { size 8x8 input i32 signed=-7 input f32 zero=-0.0 draw { rect(color= /*color*/ #abc,h=8,w=8,y=0,x=0) } }";
    const dir = workspace(source);
    const h = editorHarness(dir, execFile);
    h.settings.compilerPath = compiler;
    const before = fs.readFileSync(h.entry.fileName);
    fs.chmodSync(h.entry.fileName, 0o444);
    try {
        const edits = await format(h);
        assert.equal(edits.length, 1, h.warnings.join("\n"));
        assert.match(edits[0].newText, /rect\(color = \/\*color\*\//);
        assert.ok(edits[0].newText.includes("input i32 signed = (-7)"));
        assert.ok(edits[0].newText.includes("input f32 zero = (-0.0)"));
        assert.ok(edits[0].newText.startsWith("// " + "ż😀".repeat(18000)));
        assert.deepEqual(fs.readFileSync(h.entry.fileName), before);
        assert.equal(h.entry.getText(), source);
        assert.equal(h.entry.version, 1);
        assert.equal(h.entry.isDirty, false);
        assert.equal(fs.readdirSync(dir).filter(name => name.endsWith(".iprop")).length, 0);
        clean(dir);
    } finally {
        h.dispose(); fs.chmodSync(h.entry.fileName, 0o644);
        fs.rmSync(dir, { recursive: true, force: true });
    }
});

test("output stays in the physical entry parent and preserves imported aliases and comments", async () => {
    const dir = workspace('import p from "tile.ips" // own import\nasset t { size 8x8 draw { p.draw(seed,at={0,0}) } }');
    fs.writeFileSync(path.join(dir, "tile.ips"), "asset tile { size 2x2 draw { rect(0,0,2,2,#123) } }");
    const alias = path.join(dir, "parent-alias");
    fs.symlinkSync(dir, alias, process.platform === "win32" ? "junction" : "dir");
    const calls = [];
    const h = editorHarness(dir, (binary, args, options, callback) => {
        calls.push(args);
        assert.equal(path.dirname(args.at(-1)), fs.realpathSync(dir));
        return execFile(binary, args, options, callback);
    });
    h.settings.compilerPath = compiler;
    h.entry = h.document(path.join(alias, "entry.ips"));
    try {
        const edits = await format(h);
        assert.equal(edits.length, 1, h.warnings.join("\n"));
        assert.match(edits[0].newText, /import p from "tile\.ips" \/\/ own import/);
        const formatted = path.join(dir, "formatted.ips");
        fs.writeFileSync(formatted, edits[0].newText);
        const before = path.join(dir, "before.iprop"), after = path.join(dir, "after.iprop");
        for (const [entry, output] of [[h.entry.fileName, before], [formatted, after]]) {
            const result = spawnSync(compiler, [entry, "--strip", "--no-compress", "-o", output], { encoding: "utf8" });
            assert.equal(result.status, 0, result.stderr);
        }
        assert.deepEqual(fs.readFileSync(before), fs.readFileSync(after));
        clean(dir);
        assert.equal(calls.length, 1);
    } finally { h.dispose(); fs.rmSync(dir, { recursive: true, force: true }); }
});

test("dirty entry or another dirty IPS buffer refuses disk formatting before any process", async () => {
    for (const dirtyImport of [false, true]) {
        const dir = workspace();
        const { h, calls } = pending(dir);
        try {
            if (dirtyImport) {
                fs.writeFileSync(path.join(dir, "part.ips"), "asset p {}");
                h.document(path.join(dir, "part.ips")).isDirty = true;
            } else h.entry.isDirty = true;
            assert.equal((await format(h)).length, 0);
            assert.equal(calls.length, 0);
            assert.match(h.statuses.at(-1), /save IPS changes/);
        } finally { h.dispose(); fs.rmSync(dir, { recursive: true, force: true }); }
    }
});

test("source bytes, document version, settings and executable identity reject stale edits", async () => {
    for (const mutation of [
        h => fs.writeFileSync(h.entry.fileName, raw + "\n"),
        h => ++h.entry.version,
        h => { h.settings.compilerPath += "changed"; },
        h => fs.writeFileSync(h.settings.compilerPath, "changed compiler"),
    ]) {
        const dir = workspace();
        const { h, calls, complete } = pending(dir);
        try {
            const result = format(h);
            assert.equal(calls.length, 1);
            mutation(h); complete();
            assert.equal((await result).length, 0);
            assert.equal(h.entry.getText(), raw);
            clean(dir);
        } finally { h.dispose(); fs.rmSync(dir, { recursive: true, force: true }); }
    }
});

test("real source changes and compiler configuration events cancel, compiler staging events do not", async () => {
    const dir = workspace();
    const { h, calls, complete } = pending(dir);
    try {
        const first = format(h);
        const ownedStaging = { fsPath: path.join(dir, ".ipc-format-123-0", "source.ips") };
        h.events.diskCreate(ownedStaging); h.events.diskChange(ownedStaging); h.events.diskDelete(ownedStaging);
        assert.equal(calls[0].options.signal.aborted, false);
        h.events.diskChange({ fsPath: path.join(dir, "part.ips") });
        assert.equal(calls[0].options.signal.aborted, true);
        complete(); assert.equal((await first).length, 0);
        const second = format(h);
        h.events.configuration({ affectsConfiguration: name => name === "iprop.compilerPath" });
        assert.equal(calls[1].options.signal.aborted, true);
        complete(1); assert.equal((await second).length, 0);
        clean(dir);
    } finally { h.dispose(); fs.rmSync(dir, { recursive: true, force: true }); }
});

test("new requests and cancellation cannot return an older edit", async () => {
    const dir = workspace();
    const { h, calls, complete } = pending(dir);
    try {
        const first = format(h), second = format(h);
        assert.equal(calls[0].options.signal.aborted, true);
        assert.notEqual(calls[0].args.at(-1), calls[1].args.at(-1));
        complete(1); assert.equal((await second).length, 1);
        complete(0); assert.equal((await first).length, 0);
        const cancellation = token(), third = format(h, cancellation);
        cancellation.cancel();
        assert.equal(calls[2].options.signal.aborted, true);
        complete(2); assert.equal((await third).length, 0);
        clean(dir);
    } finally { h.dispose(); fs.rmSync(dir, { recursive: true, force: true }); }
});

test("abort cleanup waits for actual child close and leaves unrelated staging intact", async () => {
    const dir = workspace();
    const staging = path.join(dir, ".ipc-format-777-0");
    fs.mkdirSync(staging); fs.writeFileSync(path.join(staging, "source.ips"), "unrelated");
    const controller = new AbortController(), child = new EventEmitter();
    let output, callback;
    const result = runCompilerFormatting("ipc", path.join(dir, "entry.ips"), { signal: controller.signal,
        execute(_binary, args, _options, done) { output = args.at(-1); callback = done; return child; } });
    try {
        controller.abort();
        callback({ code: "ABORT_ERR" }, "", "");
        await Promise.resolve();
        assert.equal(fs.existsSync(output), true);
        fs.writeFileSync(output, canonical);
        child.emit("close");
        await assert.rejects(result, /cancelled/);
        assert.equal(fs.existsSync(output), false);
        assert.equal(fs.readFileSync(path.join(staging, "source.ips"), "utf8"), "unrelated");
    } finally { fs.rmSync(dir, { recursive: true, force: true }); }
});

test("unsupported formatting, transport failure and invalid UTF-8 never edit", async () => {
    for (const failure of [
        { error: { code: 2 }, stderr: "ipc: unknown option '--format-only'" },
        { error: { killed: true, message: "timed out" }, stderr: "" },
        { error: null, output: Buffer.from([255]), stderr: "" },
        { error: null, output: "", stderr: "" },
    ]) {
        const dir = workspace();
        const { h, calls, complete } = pending(dir);
        try {
            const result = format(h); complete(0, failure.error, failure.output ?? canonical, failure.stderr);
            assert.equal((await result).length, 0);
            assert.equal(calls.length, 1);
            assert.equal(h.entry.getText(), raw);
            assert.equal(fs.readFileSync(h.entry.fileName, "utf8"), raw);
            assert.match(h.warnings.at(-1), /formatting failed/);
            clean(dir);
        } finally { h.dispose(); fs.rmSync(dir, { recursive: true, force: true }); }
    }
});

test("formatted output ceiling is checked before reading the source into memory", async () => {
    const dir = workspace();
    try {
        await assert.rejects(runCompilerFormatting("ipc", path.join(dir, "entry.ips"), { maxBytes: 32,
            execute(_binary, args, _options, callback) { fs.writeFileSync(args.at(-1), "x".repeat(33)); callback(null, "", ""); } }), /byte ceiling/);
        clean(dir);
    } finally { fs.rmSync(dir, { recursive: true, force: true }); }
});


test("collection formatting and highlighting preserve element types and list call order", async () => {
    const source = "asset t{size 8x8 const list<vec2> outline=[{1,1},/* corner */{6,1},{1,6}] " +
        "empty()->list<vec2>{return []} draw{list<color> colors=[] poly(color=#fff,points=outline) items=empty()}}";
    const dir = workspace(source);
    const h = editorHarness(dir, execFile); h.settings.compilerPath = compiler;
    try {
        const edits = await format(h);
        assert.equal(edits.length,1,h.warnings.join("\n"));
        const formatted=edits[0].newText;
        assert.ok(formatted.includes("/* corner */"));
        assert.ok(formatted.includes("list<color> colors = []"));
        assert.ok(formatted.includes("poly(points = outline, color = #ffffff)"));
        fs.writeFileSync(path.join(dir,"formatted.ips"),formatted);
        const checked=spawnSync(compiler,[path.join(dir,"formatted.ips"),"--check"],{encoding:"utf8"});
        assert.equal(checked.status,0,checked.stderr);clean(dir);
        const grammar=JSON.parse(fs.readFileSync(path.join(root,"editor/syntaxes/ips.tmLanguage.json"),"utf8"));
        assert.equal(new RegExp(grammar.repository.types.match).test("list<vec2>"),true);
        assert.equal(new RegExp(grammar.repository.builtins.match).test("list.len(items)"),true);
    } finally { h.dispose();fs.rmSync(dir,{recursive:true,force:true}); }
});

test("sampler formatting preserves captures, arbitrary receivers, types and comments", async () => {
    const source="asset t{size 1x1 echo(s:sampler<i32>)->sampler<i32>{return s} " +
        "draw{list<sampler<i32>> items=[sampler((10 to 15)),/* description */sampler(40)] " +
        "values=echo(items[0]).pick(n=1) value=items[1].pick()}}";
    const dir=workspace(source);const h=editorHarness(dir,execFile);h.settings.compilerPath=compiler;
    try{
        const edits=await format(h);assert.equal(edits.length,1,h.warnings.join("\n"));
        const formatted=edits[0].newText;
        assert.ok(formatted.includes("list<sampler<i32>> items"));
        assert.ok(formatted.includes("/* description */"));
        assert.ok(formatted.includes("echo(items[0]).pick(1)"));
        fs.writeFileSync(path.join(dir,"formatted.ips"),formatted);
        const checked=spawnSync(compiler,[path.join(dir,"formatted.ips"),"--check"],{encoding:"utf8"});
        assert.equal(checked.status,0,checked.stderr);clean(dir);
        const grammar=JSON.parse(fs.readFileSync(path.join(root,"editor/syntaxes/ips.tmLanguage.json"),"utf8"));
        assert.equal(new RegExp(grammar.repository.types.match).test("sampler<i32>"),true);
        assert.equal(new RegExp(grammar.repository.samplerCapture.match).test("sampler(10 to 15)"),true);
        assert.equal(new RegExp(grammar.repository.samplerMethod.match).test("items[0].pick(3)"),true);
    }finally{h.dispose();fs.rmSync(dir,{recursive:true,force:true});}
});

test("stepper formatting and highlighting preserve deterministic captures and required counts", async () => {
    const source="asset t{size 1x1 const stepper<i32> fixed=stepper(0 to 5) " +
        "echo(s:stepper<i32>)->stepper<i32>{return s} " +
        "draw{list<stepper<i32>> items=[fixed,/* captured */stepper(40)] " +
        "values=echo(items[0]).pick(n=10);items.distinct()}}";
    const dir=workspace(source),h=editorHarness(dir,execFile);h.settings.compilerPath=compiler;
    try{
        const edits=await format(h);assert.equal(edits.length,1,h.warnings.join("\n"));
        const formatted=edits[0].newText;
        assert.ok(formatted.includes("const stepper<i32> fixed"));
        assert.ok(formatted.includes("list<stepper<i32>> items"));
        assert.ok(formatted.includes("/* captured */"));
        assert.ok(formatted.includes("echo(items[0]).pick(10)"));
        fs.writeFileSync(path.join(dir,"formatted.ips"),formatted);
        const checked=spawnSync(compiler,[path.join(dir,"formatted.ips"),"--check"],{encoding:"utf8"});
        assert.equal(checked.status,0,checked.stderr);clean(dir);
        const grammar=JSON.parse(fs.readFileSync(path.join(root,"editor/syntaxes/ips.tmLanguage.json"),"utf8"));
        assert.ok(new RegExp(grammar.repository.types.match).test("stepper<i32>"));
        assert.ok(new RegExp(grammar.repository.samplerCapture.match).test("stepper(0 to 5)"));
        assert.ok(new RegExp(grammar.repository.samplerMethod.match).test("items[0].pick(10)"));
    }finally{h.dispose();fs.rmSync(dir,{recursive:true,force:true});}
});

test("list methods preserve chains comments and ordinary helper names", async () => {
    const source="asset t{size 1x1 const list<i32> none=[].sort().distinct() " +
        "echo(items:list<i32>)->list<i32>{return items} sort(x:i32)->i32{return x} " +
        "draw{items=echo([3,/* first duplicate */1,3]).distinct().sort();items.sort();x=sort(1)}}";
    const dir=workspace(source),h=editorHarness(dir,execFile);h.settings.compilerPath=compiler;
    try{
        const edits=await format(h);assert.equal(edits.length,1,h.warnings.join("\n"));
        const formatted=edits[0].newText;
        assert.ok(formatted.includes("/* first duplicate */"));
        assert.ok(formatted.includes(".distinct().sort()"));
        assert.ok(formatted.includes("[].sort().distinct()"));
        fs.writeFileSync(path.join(dir,"formatted.ips"),formatted);
        const checked=spawnSync(compiler,[path.join(dir,"formatted.ips"),"--check"],{encoding:"utf8"});
        assert.equal(checked.status,0,checked.stderr);clean(dir);
        const grammar=JSON.parse(fs.readFileSync(path.join(root,"editor/syntaxes/ips.tmLanguage.json"),"utf8"));
        const method=new RegExp(grammar.repository.listMethod.match);
        assert.ok(method.test("items.distinct()"));assert.ok(method.test("items.sort()"));
        assert.equal(method.test("sort(1)"),false);assert.equal(method.test("items.sorted()"),false);
    }finally{h.dispose();fs.rmSync(dir,{recursive:true,force:true});}
});

test("sampling palette formatting preserves const graphs and explicit palette picks", async () => {
    const source="asset t{size 1x1 const sampler<i32> fixed=sampler(40) " +
        "palette widths=[10 to 15,/* captured */20 to 30 with shape,fixed] " +
        "const distribution shape=gaussian(mean=25,sigma=2) const list<sampler<i32>> specs=widths " +
        "draw{values=widths[-1].pick(n=3) value=specs[0].pick()}}";
    const dir=workspace(source); const h=editorHarness(dir,execFile); h.settings.compilerPath=compiler;
    try{
        const edits=await format(h); assert.equal(edits.length,1,h.warnings.join("\n"));
        const formatted=edits[0].newText;
        assert.ok(formatted.includes("const sampler<i32> fixed"));
        assert.ok(formatted.includes("const list<sampler<i32>> specs"));
        assert.ok(formatted.includes("/* captured */"));
        assert.ok(formatted.includes("widths[(-1)].pick(3)"));
        fs.writeFileSync(path.join(dir,"formatted.ips"),formatted);
        const checked=spawnSync(compiler,[path.join(dir,"formatted.ips"),"--check"],{encoding:"utf8"});
        assert.equal(checked.status,0,checked.stderr); clean(dir);
    }finally{h.dispose(); fs.rmSync(dir,{recursive:true,force:true});}
});

test("point distribution literals preserve pair comments", async () => {
    const source = "asset t{size 1x1 adjustWeight(x:i32)->i32{return x} " +
        "const distribution d=weighted{16777217:50,/* mass */16777218:20,} " +
        "draw{x=adjustWeight(2);s=sampler(16777217 to 16777218 with d);items=s.pick(3)}}";
    const dir = workspace(source), h = editorHarness(dir, execFile); h.settings.compilerPath = compiler;
    try {
        const edits = await format(h); assert.equal(edits.length, 1, h.warnings.join("\n"));
        const formatted = edits[0].newText;
        assert.ok(formatted.includes("weighted {"));
        assert.ok(formatted.includes("/* mass */"));
        assert.ok(formatted.includes("adjustWeight(2)"));
        const grammar = JSON.parse(fs.readFileSync(path.join(__dirname, "../syntaxes/ips.tmLanguage.json"), "utf8"));
        const literal = new RegExp(grammar.repository.pointDistribution.match);
        assert.equal(literal.test("weighted {1:2}"), true);
        assert.equal(literal.test("weighted(2)"), true);
        fs.writeFileSync(path.join(dir,"formatted.ips"),formatted);
        const checked=spawnSync(compiler,[path.join(dir,"formatted.ips"),"--check"],{encoding:"utf8"});
        assert.equal(checked.status,0,checked.stderr); clean(dir);
    } finally { h.dispose(); fs.rmSync(dir,{recursive:true,force:true}); }
});

test("density literals preserve helper calls and numeric loop bounds", async () => {
    const source="asset t{size 1x1 input i32 dotCount=2 " +
        "make(d:distribution)->i32{return 2} draw{for i in 0..dotCount{d=density{0:0,/* height */1:1}} " +
        "for i in 0..make(density{0:1,1:1}){x=i}}}";
    const dir=workspace(source),h=editorHarness(dir,execFile);h.settings.compilerPath=compiler;
    try {
        const edits=await format(h);assert.equal(edits.length,1,h.warnings.join("\n"));
        const formatted=edits[0].newText;assert.ok(formatted.includes("/* height */"));
        assert.ok(formatted.includes("0..dotCount {"));assert.ok(formatted.includes("density {0: 1, 1: 1}"));
        fs.writeFileSync(path.join(dir,"formatted.ips"),formatted);
        const checked=spawnSync(compiler,[path.join(dir,"formatted.ips"),"--check"],{encoding:"utf8"});
        assert.equal(checked.status,0,checked.stderr);clean(dir);
        const grammar=JSON.parse(fs.readFileSync(path.join(root,"editor/syntaxes/ips.tmLanguage.json"),"utf8"));
        assert.equal(new RegExp(grammar.repository.pointDistribution.match).test("density {0:1,1:1}"),true);
        assert.equal(new RegExp(grammar.repository.pointDistribution.match).test("density(2)"),true);
    } finally {h.dispose();fs.rmSync(dir,{recursive:true,force:true});}
});


test("layer captures preserve mode comments and helper transfer", async () => {
    const source="asset t{size 2x2 input u32 copies=1 echo(image:layer)->layer{return image} "+
        "draw{for i in 0..copies{layer image(alpha=/* mode */surface.source_over){pixel(0,0,#fff)}} "+
        "layer copy=echo(image);surface.paint(blend=surface.source_over,image=copy)}}";
    const dir=workspace(source),h=editorHarness(dir,execFile);h.settings.compilerPath=compiler;
    try {
        const edits=await format(h);assert.equal(edits.length,1,h.warnings.join("\n"));
        const formatted=edits[0].newText;assert.ok(formatted.includes("/* mode */"));
        assert.ok(formatted.includes("0..copies {"));assert.ok(formatted.includes("layer copy = echo(image)"));
        assert.ok(formatted.includes("surface.paint(image = copy, blend = surface.source_over)"));
        fs.writeFileSync(path.join(dir,"formatted.ips"),formatted);
        const checked=spawnSync(compiler,[path.join(dir,"formatted.ips"),"--check"],{encoding:"utf8"});
        assert.equal(checked.status,0,checked.stderr);clean(dir);
        const grammar=JSON.parse(fs.readFileSync(path.join(root,"editor/syntaxes/ips.tmLanguage.json"),"utf8"));
        const capture=new RegExp(grammar.repository.layerCapture.match);
        for(const text of ["layer image {", "layer image(alpha=surface.source_over){"])assert.equal(capture.test(text),true,text);
        for(const text of ["input u32 layer=1", "layer(2)", "layer=1", "layer copy=image"])assert.equal(capture.test(text),true,text);
        const mode=new RegExp(grammar.repository.surfaceMode.match);
        assert.equal(mode.test("surface.source_over"),true);assert.equal(mode.test("surface.legacy"),true);
        assert.equal(new RegExp(grammar.repository.builtins.match).test("surface.paint(image)"),true);
    } finally {h.dispose();fs.rmSync(dir,{recursive:true,force:true});}
});

test("resource output declarations preserve comments and use reserved types", async () => {
    const source="asset t{size 1x1 output /* mask */mask region output layer image draw{layer captured{}region=mask.from(captured)image=captured}}";
    const dir=workspace(source),h=editorHarness(dir,execFile);h.settings.compilerPath=compiler;
    try {
        const edits=await format(h);assert.equal(edits.length,1,h.warnings.join("\n"));
        const formatted=edits[0].newText;assert.ok(formatted.includes("/* mask */"));
        assert.ok(formatted.includes("output layer image"));
        fs.writeFileSync(path.join(dir,"formatted.ips"),formatted);
        const checked=spawnSync(compiler,[path.join(dir,"formatted.ips"),"--check"],{encoding:"utf8"});assert.equal(checked.status,0,checked.stderr);
        const grammar=JSON.parse(fs.readFileSync(path.join(root,"editor/syntaxes/ips.tmLanguage.json"),"utf8"));
        assert.equal(new RegExp(grammar.repository.maskType.match).test("output mask region"),true);
        assert.equal(new RegExp(grammar.repository.layerCapture.match).test("output layer image"),true);
    } finally {h.dispose();fs.rmSync(dir,{recursive:true,force:true});}
});
