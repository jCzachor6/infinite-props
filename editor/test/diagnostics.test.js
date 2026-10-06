"use strict";
const { test } = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { spawnSync } = require("node:child_process");
const { parseDiagnostics, utf16Column } = require("../diagnostics");

test("parses canonical and legacy diagnostics with paths containing colons", () => {
    const parsed = parseDiagnostics("ipc: C:\\a b\\t.ips:12:3: error: bad\r\nsource\r\n  ^\r\n" +
        "ipc: part.ips:\n2:8: error: other\n", "entry.ips");
    assert.deepEqual(parsed.get("C:\\a b\\t.ips"), [{ line:12,column:3,message:"bad" }]);
    assert.deepEqual(parsed.get("part.ips"), [{ line:2,column:8,message:"other" }]);
});

test("maps codepoint columns to VS Code UTF-16 positions", () => {
    assert.equal(utf16Column("a📦ąx", 1), 0);
    assert.equal(utf16Column("a📦ąx", 3), 3);
    assert.equal(utf16Column("a📦ąx", 5), 5);
    assert.equal(utf16Column("a📦ąx", 100), 5);
    assert.equal(utf16Column("a\tb", 3), 2);
});

test("real compiler errors retain source ownership through imports and lowering", () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "iprop-diagnostics-"));
    const ipc = process.env.IPROP_COMPILER || path.resolve(__dirname, "../../build/tools/ipc");
    const asset = "asset t { size 8x8 draw { } }";
    const run = (files, expectedFile, message) => {
        for (const [name, source] of Object.entries(files)) fs.writeFileSync(path.join(dir,name),source);
        const result = spawnSync(ipc,["entry.ips"],{cwd:dir,encoding:"utf8"});
        assert.equal(result.status,1,result.stderr);
        const errors = parseDiagnostics(result.stderr,"entry.ips");
        assert.equal(errors.size,1,result.stderr);
        const [error] = errors.get(expectedFile) || [];
        assert.ok(error,result.stderr);
        assert.ok(error.line > 0 && error.column > 0);
        assert.match(error.message,message);
        assert.ok(result.stderr.includes("^"),result.stderr);
        return error;
    };
    try {
        run({"entry.ips":'import tile from "tile.ips"\nasset t { size 8x8 draw {\n' +
            'tile.draw(1,2,3,at={0,0},rot=45)\n} }', "tile.ips":asset},"entry.ips",/rot transform/);
        run({"entry.ips":'import tile from "tile.ips"\n'+asset, "tile.ips":"asset bad {\n @ }"},
            "tile.ips",/unexpected/);
        run({"tile.ips":'import missing from "missing.ips"\n'+asset},"tile.ips",/cannot load/);
        run({"tile.ips":'import back from "entry.ips"\n'+asset},"tile.ips",/import cycle/);
        // Nonconstant origin is diagnosed by the assembly stage.
        run({"tile.ips":"asset t { size 8x8 origin unit(),0 draw { } }"},
            "tile.ips",/origin must be a constant/);
        const unicode = "/* 📦ż */ asset t { size 8x8 draw { smooh(0.5) } }";
        const error = run({"entry.ips":unicode}, "entry.ips", /did you mean 'smooth'/);
        const offset = unicode.indexOf("smooh");
        assert.equal(error.column, Array.from(unicode.slice(0,offset)).length + 1);
        assert.equal(utf16Column(unicode,error.column), offset);
    } finally { fs.rmSync(dir,{recursive:true,force:true}); }
});

test("compiler exports canonical source with identical executable roundtrips", () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "iprop-format-"));
    const ipc = process.env.IPROP_COMPILER || path.resolve(__dirname, "../../build/tools/ipc");
    // Windows forbids filename quotes; a nested path still tests escaped imports.
    const part = process.platform === "win32" ? "parts\\tile ż.ips" : 'tile "ż".ips';
    const source = 'import tile from ' + JSON.stringify(part) + '\n' +
        'asset example { size 8x8 draw {\n' +
        '  // author comment\n  rect(color=#fff,h=4,w=4,y=0,x=0)\n' +
        '  tile.draw(seed,v,a,at={0,0})\n} }';
    const run = args => spawnSync(ipc,args,{cwd:dir,encoding:"utf8"});
    try {
        fs.mkdirSync(path.dirname(path.join(dir,part)),{recursive:true});
        fs.writeFileSync(path.join(dir,part),"asset tile { size 8x8 draw { pixel(0,0,#f00) } }");
        fs.writeFileSync(path.join(dir,"entry.ips"),source);
        let result = run(["entry.ips","-o","original.iprop","--strip","--format","canonical.ips"]);
        assert.equal(result.status,0,result.stderr);
        const formatted = fs.readFileSync(path.join(dir,"canonical.ips"),"utf8");
        assert.match(formatted,/    \/\/ author comment\n    rect\(x = 0, y = 0, w = 4, h = 4, color = #ffffff\)/);
        assert.equal(fs.readFileSync(path.join(dir,"entry.ips"),"utf8"),source);
        result = run(["canonical.ips","-o","roundtrip.iprop","--strip","--format","twice.ips"]);
        assert.equal(result.status,0,result.stderr);
        assert.deepEqual(fs.readFileSync(path.join(dir,"roundtrip.iprop")),fs.readFileSync(path.join(dir,"original.iprop")));
        assert.equal(fs.readFileSync(path.join(dir,"twice.ips"),"utf8"),formatted);
        result = run(["entry.ips","--format","entry.ips"]);
        assert.equal(result.status,2);
        assert.match(result.stderr,/distinct source output/);
        assert.equal(fs.readFileSync(path.join(dir,"entry.ips"),"utf8"),source);
        fs.linkSync(path.join(dir,"entry.ips"),path.join(dir,"source-alias.ips"));
        result = run(["entry.ips","--format","source-alias.ips"]);
        assert.equal(result.status,2);
        assert.match(result.stderr,/distinct source output/);
        assert.equal(fs.readFileSync(path.join(dir,"entry.ips"),"utf8"),source);
        const binary = fs.readFileSync(path.join(dir,"original.iprop"));
        fs.linkSync(path.join(dir,"original.iprop"),path.join(dir,"binary-alias.ips"));
        result = run(["entry.ips","-o","original.iprop","--format","binary-alias.ips"]);
        assert.equal(result.status,2);
        assert.match(result.stderr,/distinct source output/);
        assert.deepEqual(fs.readFileSync(path.join(dir,"original.iprop")),binary);
        if (fs.existsSync(path.join(dir,"ENTRY.IPS"))) {
            result = run(["entry.ips","-o","case.iprop","--format","CASE.IPROP"]);
            assert.equal(result.status,2);
            assert.match(result.stderr,/distinct source output/);
            assert.equal(fs.readFileSync(path.join(dir,"case.iprop")).subarray(0,4).toString(),"IPRP");
        }
    } finally { fs.rmSync(dir,{recursive:true,force:true}); }
});

const { parseJsonDiagnostics, editorRange, sourceContentId, fixEdits, runCompilerDiagnostics } = require("../diagnostics");
const { editorHarness } = require("./support/editor");

function point(source, offset) {
    const prefix = source.slice(0, offset);
    const lines = prefix.split("\n");
    return { byte_offset: Buffer.byteLength(prefix), line: lines.length, column: Array.from(lines.at(-1)).length + 1 };
}
function rangeOf(source, text) {
    const start = source.indexOf(text);
    assert.notEqual(start, -1);
    return { start: point(source, start), end: point(source, start + text.length) };
}
function jsonDiagnostic(source, overrides = {}) {
    return { code: "IPS-TYPE-004", stage: "typing", severity: "error", message: "signed default needs i32",
        file: "entry.ips", range: rangeOf(source, "u32"), location: null, source_id: sourceContentId(source),
        expected_type: null, actual_type: null, suggestions: ["Use input i32 for a signed default."], related_locations: [],
        fix_its: [{ range: rangeOf(source, "u32"), replacement: "i32", source_id: sourceContentId(source) }], ...overrides };
}
function envelope(diagnostics = [], status = "invalid_source") {
    return { schema_version: 1, product_version: "0.5.0", language_version: { major: 0, minor: 5 }, status, diagnostics };
}
function callbackJson(callback, result) {
    callback(result.status === "ok" ? null : { code: { invalid_source: 1, cli_error: 2, internal_error: 3 }[result.status] },
        JSON.stringify(result), "");
}
function workspace(source = "asset t { size 8x8 input u32 n=-7 draw{} }") {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "iprop-editor-test-"));
    fs.writeFileSync(path.join(dir, "entry.ips"), source);
    return dir;
}


test("JSON schema preserves structured fields and rejects mixed, malformed, or mismatched responses", () => {
    const source = "asset t { size 8x8 input u32 n=-7 draw{} }";
    const report = envelope([jsonDiagnostic(source)]);
    assert.deepEqual(parseJsonDiagnostics(JSON.stringify(report), 1), report);
    for (const broken of ["compiled file\n" + JSON.stringify(report), JSON.stringify(report) + "\n{}", "{"]) {
        assert.throws(() => parseJsonDiagnostics(broken, 1), /JSON/);
    }
    assert.throws(() => parseJsonDiagnostics(JSON.stringify(report), 0), /schema/);
    for (const mutate of [
        d => { d.range.end.byte_offset = -1; },
        d => { d.code = "unknown"; },
        d => { d.stage = "unknown"; },
        d => { d.range.start.column = 0; },
        d => { d.fix_its[0].source_id = "sha256:unknown"; },
        d => { d.suggestions = "guess a replacement"; },
        d => { d.related_locations = [{ file: "part.ips", message: "related", range: null }]; },
    ]) {
        const broken = structuredClone(report);
        mutate(broken.diagnostics[0]);
        assert.throws(() => parseJsonDiagnostics(JSON.stringify(broken), 1), /schema/);
    }
});

test("UTF-8 exclusive ranges become UTF-16 only at the editor boundary, including CRLF and astral scalars", () => {
    const source = "// ż\r\n\t📦 t {\r\n input u32 n=-7\r\n}";
    assert.deepEqual(editorRange(source, rangeOf(source, "📦")), {
        start: { line: 1, character: 1 }, end: { line: 1, character: 3 },
    });
    const multiline = { start: point(source, source.indexOf("📦")), end: point(source, source.indexOf("u32") + 3) };
    assert.deepEqual(editorRange(source, multiline), {
        start: { line: 1, character: 1 }, end: { line: 2, character: 10 },
    });
    const insideEmoji = structuredClone(rangeOf(source, "📦"));
    ++insideEmoji.start.byte_offset;
    assert.equal(editorRange(source, insideEmoji), null);
    const wrongColumn = structuredClone(multiline);
    ++wrongColumn.end.column;
    assert.equal(editorRange(source, wrongColumn), null);
    const eof = point(source, source.length);
    assert.deepEqual(editorRange(source, { start: eof, end: eof }), {
        start: { line: 3, character: 1 }, end: { line: 3, character: 1 },
    });
    const middleCRLF = point(source, source.indexOf("\n"));
    assert.equal(editorRange(source, { start: middleCRLF, end: middleCRLF }), null);
});

test("fix transactions require exact source bytes and reject overlapping or inconsistent ranges", () => {
    assert.equal(sourceContentId("abc"), "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const source = "// 📦\r\nasset t { input u32 n=-1 }";
    const d = jsonDiagnostic(source);
    assert.equal(fixEdits(source, d)[0].replacement, "i32");
    assert.equal(fixEdits(source.replaceAll("\r\n", "\n"), d), null);
    assert.equal(fixEdits(source + " ", d), null);
    assert.equal(fixEdits(source, { ...d, fix_its: [...d.fix_its, ...d.fix_its] }), null);
    const differentIdentity = structuredClone(d);
    differentIdentity.fix_its[0].source_id = sourceContentId("other");
    assert.equal(fixEdits(source, differentIdentity), null);
    const bomSource = "\ufeff" + source;
    const bomDiagnostic = jsonDiagnostic(bomSource, { fix_its: [{
        range: rangeOf(bomSource, "\ufeff"), replacement: "", source_id: sourceContentId(bomSource),
    }] });
    assert.equal(fixEdits(bomSource, bomDiagnostic), null);
});

test("diagnostic transport falls back only for unsupported options and cleans unique legacy artifacts", async () => {
    const dir = workspace();
    const entry = path.join(dir, "entry.ips");
    const calls = [];
    const run = async () => runCompilerDiagnostics("ipc", entry, { execute: (_binary, args, _options, callback) => {
        calls.push(args);
        if (args.includes("--check")) callback({ code: 1 }, "", "ipc: unknown option '--check'\n");
        else {
            fs.writeFileSync(args[2], "legacy bytes");
            callback(null, "compiled source\n", "");
        }
    } });
    try {
        assert.equal((await run()).protocol, "text");
        assert.equal((await run()).protocol, "text");
        const outputs = calls.filter(args => args.includes("-o")).map(args => args[2]);
        assert.equal(outputs.length, 2);
        assert.notEqual(outputs[0], outputs[1]);
        for (const output of outputs) assert.equal(fs.existsSync(path.dirname(output)), false);
        assert.deepEqual(fs.readdirSync(dir), ["entry.ips"]);
        for (const response of [
            { code: 1, stdout: "", stderr: "ipc: entry.ips:1:1: error: invalid source\n" },
            { code: 0, stdout: "{", stderr: "" },
            { code: 2, stdout: JSON.stringify(envelope([], "cli_error")), stderr: "" },
        ]) {
            let count = 0;
            const request = runCompilerDiagnostics("ipc", entry, { execute: (_binary, _args, _options, callback) => {
                ++count;
                callback(response.code ? { code: response.code } : null, response.stdout, response.stderr);
            } });
            if (response.stdout.startsWith("{\"schema")) assert.equal((await request).protocol, "json");
            else await assert.rejects(request, /JSON/);
            assert.equal(count, 1);
        }
    } finally { fs.rmSync(dir, { recursive: true, force: true }); }
});

test("cancelled legacy checks remove their temporary output directory", async () => {
    const dir = workspace();
    const controller = new AbortController();
    let output, started;
    const fallbackStarted = new Promise(resolve => { started = resolve; });
    const request = runCompilerDiagnostics("ipc", path.join(dir, "entry.ips"), {
        signal: controller.signal, execute: (_binary, args, options, callback) => {
            if (args.includes("--check")) callback({ code: 1 }, "", "ipc: unknown option '--check'\n");
            else {
                output = args[2]; fs.writeFileSync(output, "partial");
                options.signal.addEventListener("abort", () => callback({ code: "ABORT_ERR" }, "", ""), { once: true });
                started();
            }
        },
    });
    try {
        await fallbackStarted;
        controller.abort();
        await assert.rejects(request);
        assert.equal(fs.existsSync(path.dirname(output)), false);
    } finally { fs.rmSync(dir, { recursive: true, force: true }); }
});

test("real JSON compiler exposes import ownership, exclusive ranges, exact hashes and a working fix", async () => {
    const ipc = process.env.IPROP_COMPILER || path.resolve(__dirname, "../../build/tools/ipc");
    const dir = workspace('import tile from "tile.ips"\r\nasset t { size 8x8 draw{} }');
    const source = "/* 📦ż */ asset tile { size 8x8\r\n\tinput u32 n=-7\r\n draw{} }";
    fs.writeFileSync(path.join(dir, "tile.ips"), source);
    try {
        const result = await runCompilerDiagnostics(ipc, path.join(dir, "entry.ips"));
        assert.equal(result.protocol, "json");
        assert.equal(result.status, "invalid_source");
        const [d] = result.diagnostics;
        assert.equal(d.file, "tile.ips");
        assert.equal(d.source_id, sourceContentId(source));
        assert.equal(Buffer.from(source).subarray(d.range.start.byte_offset, d.range.end.byte_offset).toString(), "-");
        const [fix] = fixEdits(source, d);
        assert.equal(fix.range.start.line, 1);
        assert.equal(fix.range.start.character, 7);
        assert.equal(fix.range.end.character, 10);
        const bytes = Buffer.from(source);
        const range = d.fix_its[0].range;
        const repaired = Buffer.concat([bytes.subarray(0, range.start.byte_offset), Buffer.from(fix.replacement),
            bytes.subarray(range.end.byte_offset)]).toString();
        fs.writeFileSync(path.join(dir, "tile.ips"), repaired);
        assert.equal((await runCompilerDiagnostics(ipc, path.join(dir, "entry.ips"))).status, "ok");
        assert.deepEqual(fs.readdirSync(dir).sort(), ["entry.ips", "tile.ips"]);
    } finally { fs.rmSync(dir, { recursive: true, force: true }); }
});

test("frozen compiler 0.4 produces real text diagnostics without writing beside the entry", {
    skip: !process.env.IPROP_LEGACY_COMPILER,
}, async () => {
    const dir = workspace('import tile from "tile.ips"\nasset t { size 8x8 draw{} }');
    fs.writeFileSync(path.join(dir, "tile.ips"), "/* 📦 */ asset tile { size 8x8 draw { smooh(0.5) } }");
    try {
        const result = await runCompilerDiagnostics(process.env.IPROP_LEGACY_COMPILER, path.join(dir, "entry.ips"));
        assert.equal(result.protocol, "text");
        assert.equal(result.status, "invalid_source");
        assert.equal(result.diagnostics[0].file, "tile.ips");
        assert.match(result.diagnostics[0].message, /smooth/);
        assert.equal(result.diagnostics[0].code, null);
        assert.deepEqual(fs.readdirSync(dir).sort(), ["entry.ips", "tile.ips"]);
        fs.writeFileSync(path.join(dir, "tile.ips"), "asset tile { size 8x8 draw{} }");
        assert.equal((await runCompilerDiagnostics(process.env.IPROP_LEGACY_COMPILER, path.join(dir, "entry.ips"))).status, "ok");
        assert.deepEqual(fs.readdirSync(dir).sort(), ["entry.ips", "tile.ips"]);
    } finally { fs.rmSync(dir, { recursive: true, force: true }); }
});

test("VS Code maps imported warnings/notes, stable codes, and related source ranges", async () => {
    const dir = workspace();
    const source = "// 📦\r\nasset tile { input u32 n=-7 }";
    fs.writeFileSync(path.join(dir, "tile.ips"), source);
    const entrySource = fs.readFileSync(path.join(dir, "entry.ips"), "utf8");
    const warning = jsonDiagnostic(source, { file: "tile.ips", severity: "warning", related_locations: [{
        file: "entry.ips", message: "imported here", range: rangeOf(entrySource, "asset"),
    }] });
    const note = { ...warning, severity: "note", code: "IPS-NOTE-001", fix_its: [], suggestions: [] };
    const h = editorHarness(dir, (_binary, _args, _options, callback) => callbackJson(callback, envelope([warning, note], "ok")));
    try {
        await h.compile();
        const imported = h.collection.get(path.join(dir, "tile.ips"));
        assert.equal(imported.length, 2);
        assert.equal(imported[0].severity, h.vscode.DiagnosticSeverity.Warning);
        assert.equal(imported[1].severity, h.vscode.DiagnosticSeverity.Information);
        assert.equal(imported[0].code, "IPS-TYPE-004");
        assert.equal(imported[0].range.end.character - imported[0].range.start.character, 3);
        assert.equal(imported[0].relatedInformation[0].location.uri.fsPath, path.join(dir, "entry.ips"));
        assert.equal(imported[0].relatedInformation[0].location.range.end.character, 5);
    } finally { h.dispose(); fs.rmSync(dir, { recursive: true, force: true }); }
});

test("quick fixes use the exact compiler edit and recheck source identity when executed", async () => {
    const dir = workspace("// 📦\r\nasset t { size 8x8 input u32 n=-7 draw{} }");
    const source = fs.readFileSync(path.join(dir, "entry.ips"), "utf8");
    const report = envelope([jsonDiagnostic(source)]);
    const h = editorHarness(dir, (_binary, _args, _options, callback) => callbackJson(callback, report));
    try {
        await h.compile();
        const actions = h.actions(h.entry);
        assert.equal(actions.length, 2);
        assert.equal(actions[0].command.command, "iprop.applyCompilerFix");
        assert.equal(actions[1].command.command, "iprop.showCompilerSuggestion");
        assert.equal(actions[0].edit, undefined); // no stale WorkspaceEdit baked into a menu item
        h.entry.text += " "; ++h.entry.version;
        assert.equal(h.actions(h.entry).length, 0);
        assert.equal(await h.commands.get(actions[0].command.command)(...actions[0].command.arguments), false);
        assert.match(h.warnings.at(-1), /source changed/);
        assert.match(h.entry.text, /input u32/);
        h.entry.text = source; ++h.entry.version;
        await h.compile();
        const [fix] = h.actions(h.entry);
        assert.equal(await h.commands.get(fix.command.command)(...fix.command.arguments), true);
        assert.equal(h.entry.text, source.replace("u32", "i32"));
        assert.equal(h.collection.size, 0);
        assert.equal(fs.readFileSync(h.entry.fileName, "utf8"), source); // edits the buffer; no implicit save
    } finally { h.dispose(); fs.rmSync(dir, { recursive: true, force: true }); }
});

test("a concurrent document edit rejects the version-bound fix transaction", async () => {
    const dir = workspace();
    const source = fs.readFileSync(path.join(dir, "entry.ips"), "utf8");
    const h = editorHarness(dir, (_binary, _args, _options, callback) => callbackJson(callback, envelope([jsonDiagnostic(source)])));
    try {
        await h.compile();
        const [fix] = h.actions(h.entry);
        h.vscode.concurrentEdit = doc => { doc.text += " "; ++doc.version; };
        assert.equal(await h.commands.get(fix.command.command)(...fix.command.arguments), false);
        assert.equal(h.entry.text, source + " ");
    } finally { h.dispose(); fs.rmSync(dir, { recursive: true, force: true }); }
});

test("the newest diagnostic run wins and successful checks clear prior imported errors", async () => {
    const dir = workspace();
    const source = fs.readFileSync(path.join(dir, "entry.ips"), "utf8");
    fs.writeFileSync(path.join(dir, "tile.ips"), source);
    const callbacks = [];
    const h = editorHarness(dir, (_binary, _args, _options, callback) => callbacks.push(callback));
    try {
        const first = h.compile();
        const second = h.compile();
        callbackJson(callbacks[1], envelope([jsonDiagnostic(source, { file: "tile.ips", message: "new error" })]));
        await second;
        callbackJson(callbacks[0], envelope([jsonDiagnostic(source, { message: "old error" })]));
        await first;
        assert.equal(h.collection.size, 1);
        assert.equal(h.collection.get(path.join(dir, "tile.ips"))[0].message, "new error");
        const third = h.compile();
        callbackJson(callbacks[2], envelope([], "ok"));
        await third;
        assert.equal(h.collection.size, 0);
    } finally { h.dispose(); fs.rmSync(dir, { recursive: true, force: true }); }
});

test("changed imports and unsaved buffers cannot receive old source diagnostics", async () => {
    const dir = workspace();
    const source = fs.readFileSync(path.join(dir, "entry.ips"), "utf8");
    fs.writeFileSync(path.join(dir, "tile.ips"), source);
    let callback, calls = 0;
    const h = editorHarness(dir, (_binary, _args, _options, cb) => { callback = cb; ++calls; });
    try {
        const first = h.compile();
        fs.writeFileSync(path.join(dir, "tile.ips"), source + " ");
        callbackJson(callback, envelope([jsonDiagnostic(source, { file: "tile.ips" })]));
        await first;
        assert.equal(h.collection.size, 0);
        assert.ok(h.logs.some(log => log.includes("changed source")));
        h.entry.text += " "; ++h.entry.version; h.entry.isDirty = true;
        await h.compile();
        assert.equal(calls, 1);
        assert.match(h.statuses.at(-1), /save changes/);
    } finally { h.dispose(); fs.rmSync(dir, { recursive: true, force: true }); }
});

test("change/delete events immediately invalidate diagnostics and in-flight results before debounce", async () => {
    const dir = workspace();
    const source = fs.readFileSync(path.join(dir, "entry.ips"), "utf8");
    let callback;
    const h = editorHarness(dir, (_binary, _args, _options, cb) => { callback = cb; });
    try {
        const first = h.compile();
        callbackJson(callback, envelope([jsonDiagnostic(source)]));
        await first;
        assert.equal(h.collection.size, 1);
        h.events.diskDelete();
        assert.equal(h.collection.size, 0);
        const second = h.compile();
        h.events.textChange({ document: h.entry });
        callbackJson(callback, envelope([jsonDiagnostic(source)]));
        await second;
        assert.equal(h.collection.size, 0);
    } finally { h.dispose(); fs.rmSync(dir, { recursive: true, force: true }); }
});

test("protocol failure is visible in the editor and never triggers text fallback", async () => {
    const dir = workspace();
    let calls = 0;
    const h = editorHarness(dir, (_binary, _args, _options, callback) => { ++calls; callback(null, "{", ""); });
    try {
        await h.compile();
        assert.equal(calls, 1);
        assert.equal(h.collection.size, 0);
        assert.match(h.logs.at(-1), /invalid diagnostic JSON/);
        assert.match(h.statuses.at(-1), /diagnostics failed/);
    } finally { h.dispose(); fs.rmSync(dir, { recursive: true, force: true }); }
});

test("native invalid UTF-8 diagnostics retain their raw-byte identity and appear as point errors", async () => {
    const dir = workspace();
    const bytes = Buffer.concat([Buffer.from("asset t { size 8x8 draw { "), Buffer.from([0xff]), Buffer.from(" } }")]);
    fs.writeFileSync(path.join(dir, "entry.ips"), bytes);
    const ipc = process.env.IPROP_COMPILER || path.resolve(__dirname, "../../build/tools/ipc");
    const h = editorHarness(dir, (_binary, args, options, callback) => require("node:child_process").execFile(ipc, args, options, callback));
    try {
        await h.compile();
        const [diagnostic] = h.collection.get(h.entry.fileName) ?? [];
        assert.ok(diagnostic, h.logs.join("\n"));
        assert.match(diagnostic.message, /UTF-8/);
        assert.equal(diagnostic.code, "IPS-LEX-001");
        assert.equal(diagnostic.range.start.character, 26);
        assert.equal(diagnostic.range.end.character, 26);
        assert.equal(h.actions(h.entry).length, 0);
        assert.notEqual(sourceContentId(bytes), sourceContentId(bytes.toString("utf8")));
    } finally { h.dispose(); fs.rmSync(dir, { recursive: true, force: true }); }
});

test("legacy fallback never attaches saved-file errors to an already dirty imported buffer", async () => {
    const dir = workspace();
    const source = "asset tile { size 8x8 draw { smooh(0.5) } }";
    fs.writeFileSync(path.join(dir, "tile.ips"), source);
    const h = editorHarness(dir, (_binary, args, _options, callback) => {
        if (args.includes("--check")) callback({ code: 1 }, "", "ipc: unknown option '--check'\n");
        else callback({ code: 1 }, "", "ipc: tile.ips:1:29: error: unknown function\n");
    });
    try {
        const imported = h.document(path.join(dir, "tile.ips"));
        imported.text = "// unsaved first line\n" + source;
        imported.isDirty = true;
        await h.compile();
        assert.equal(h.collection.size, 0);
        assert.ok(h.logs.some(log => log.includes("changed source")));
    } finally { h.dispose(); fs.rmSync(dir, { recursive: true, force: true }); }
});

test("a clean BOM-stripped editor model still receives the native lexical diagnostic", async () => {
    const dir = workspace("\ufeffasset t { size 8x8 draw{} }");
    const ipc = process.env.IPROP_COMPILER || path.resolve(__dirname, "../../build/tools/ipc");
    let calls = 0;
    const h = editorHarness(dir, (_binary, args, options, callback) => {
        ++calls;
        return require("node:child_process").execFile(ipc, args, options, callback);
    });
    h.entry.text = h.entry.text.slice(1); // VS Code omits the BOM from getText().
    try {
        await h.compile();
        assert.equal(calls, 1);
        const [d] = h.collection.get(h.entry.fileName) ?? [];
        assert.ok(d, h.logs.join("\n"));
        assert.match(d.message, /unexpected/);
        assert.equal(d.range.start.character, 0);
        assert.equal(d.range.end.character, 0);
    } finally { h.dispose(); fs.rmSync(dir, { recursive: true, force: true }); }
});

test("clean mixed-EOL models map byte ranges and apply hash-bound fixes to the normalized buffer", async () => {
    const source = "// 📦\r\nasset t { size 8x8\r input u32 n=-7\n draw{} }";
    const dir = workspace(source);
    const ipc = process.env.IPROP_COMPILER || path.resolve(__dirname, "../../build/tools/ipc");
    const h = editorHarness(dir, (_binary, args, options, callback) =>
        require("node:child_process").execFile(ipc, args, options, callback));
    h.entry.text = source.replace(/\r\n|\r|\n/g, "\r\n"); // A clean VS Code CRLF model.
    try {
        await h.compile();
        const [d] = h.collection.get(h.entry.fileName) ?? [];
        assert.ok(d, h.logs.join("\n"));
        assert.equal(d.range.start.line, 2);
        assert.equal(d.range.start.character, 13);
        const [fix] = h.actions(h.entry);
        assert.ok(fix);
        assert.equal(await h.commands.get(fix.command.command)(...fix.command.arguments), true);
        assert.equal(h.entry.text, source.replace(/\r\n|\r|\n/g, "\r\n").replace("u32", "i32"));
        assert.equal(fs.readFileSync(h.entry.fileName, "utf8"), source);
    } finally { h.dispose(); fs.rmSync(dir, { recursive: true, force: true }); }
});

test("disjoint multi-edit fixes including EOF insertion form one atomic source-bound transaction", async () => {
    const source = "// 📦\r\nasset t { input u32 n=-1 input u32 m=-2 draw{} }";
    const dir = workspace(source);
    const first = rangeOf(source, "u32");
    const secondOffset = source.indexOf("u32", source.indexOf("u32") + 3);
    const second = { start: point(source, secondOffset), end: point(source, secondOffset + 3) };
    const eof = point(source, source.length);
    const d = jsonDiagnostic(source, { fix_its: [
        { range: first, replacement: "i32", source_id: sourceContentId(source) },
        { range: second, replacement: "i32", source_id: sourceContentId(source) },
        { range: { start: eof, end: eof }, replacement: "\r\n// fixed", source_id: sourceContentId(source) },
    ] });
    const h = editorHarness(dir, (_binary, _args, _options, callback) => callbackJson(callback, envelope([d])));
    try {
        await h.compile();
        const [fix] = h.actions(h.entry);
        assert.equal(await h.commands.get(fix.command.command)(...fix.command.arguments), true);
        assert.equal(h.entry.text, source.replaceAll("u32", "i32") + "\r\n// fixed");
        assert.equal(h.entry.version, 2); // One editor transaction, three explicit edits.
    } finally { h.dispose(); fs.rmSync(dir, { recursive: true, force: true }); }
});

test("real compiler multiple independent errors all reach the Problems collection with their own ranges", async () => {
    const source = "asset t { size 8x8 draw {\r\n first=unknown_first(1)\r\n second=unknown_second(2)\r\n} }";
    const dir = workspace(source);
    const ipc = process.env.IPROP_COMPILER || path.resolve(__dirname, "../../build/tools/ipc");
    const h = editorHarness(dir, (_binary, args, options, callback) =>
        require("node:child_process").execFile(ipc, args, options, callback));
    try {
        await h.compile();
        const list = h.collection.get(h.entry.fileName) ?? [];
        assert.equal(list.length, 2, h.logs.join("\n"));
        assert.match(list[0].message, /unknown_first/);
        assert.match(list[1].message, /unknown_second/);
        assert.equal(list[0].code, "IPS-TYPE-001");
        assert.equal(list[1].code, "IPS-TYPE-001");
        assert.equal(list[0].range.start.line, 1);
        assert.equal(list[1].range.start.line, 2);
        assert.equal(list[0].range.end.character - list[0].range.start.character, 13);
        assert.equal(list[1].range.end.character - list[1].range.start.character, 14);
        assert.deepEqual(fs.readdirSync(dir), ["entry.ips"]);
    } finally { h.dispose(); fs.rmSync(dir, { recursive: true, force: true }); }
});

test("opening an imported file for a Problems quick fix does not invalidate its own transaction", async () => {
    const dir = workspace();
    const source = "asset tile { size 8x8 input u32 n=-7 draw{} }";
    fs.writeFileSync(path.join(dir, "tile.ips"), source);
    const h = editorHarness(dir, (_binary, _args, _options, callback) =>
        callbackJson(callback, envelope([jsonDiagnostic(source, { file: "tile.ips" })])));
    try {
        await h.compile();
        const imported = h.document(path.join(dir, "tile.ips"));
        const [fix] = h.actions(imported);
        h.vscode.emitActiveOnShow = true;
        assert.equal(await h.commands.get(fix.command.command)(...fix.command.arguments), true);
        assert.equal(imported.text, source.replace("u32", "i32"));
    } finally { h.dispose(); fs.rmSync(dir, { recursive: true, force: true }); }
});
