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
    const dir = workspace('import p from "tile.ips" // own import\nasset t { size 8x8 draw { p.draw(seed,v,a,at={0,0}) } }');
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
