"use strict";
const { test } = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const vm = require("node:vm");
const { createRequire } = require("node:module");
const { spawnSync } = require("node:child_process");
const { inflateSync } = require("node:zlib");

const root = path.resolve(__dirname, "../..");
const compiler = process.env.IPROP_COMPILER || path.join(root, "build/tools/ipc");
const tool = name => path.join(path.dirname(compiler), name + (compiler.endsWith(".exe") ? ".exe" : ""));

function run(binary, args) {
    const result = spawnSync(binary, args, { encoding: "utf8" });
    assert.equal(result.status, 0, result.stderr || String(result.error));
    return result.stdout;
}

function expanded(bytes) {
    if (!(bytes.readUInt32LE(8) & 2)) return bytes;
    const plain = inflateSync(bytes.subarray(24));
    assert.equal(plain.length, bytes.readUInt32LE(20));
    return plain;
}

function hasDebug(bytes) {
    const plain = expanded(bytes);
    let debugSection = false;
    for (let i = 0; i < plain.readUInt32LE(12); ++i) {
        if (plain.readUInt32LE(20 + 12 * i) === 4) debugSection = true;
    }
    const debug = Boolean(plain.readUInt32LE(8) & 1);
    assert.equal(debug, debugSection);
    assert.equal(Boolean(bytes.readUInt32LE(8) & 1), debug);
    return debug;
}

test("production defaults preserve execution and debug remains explicitly available", () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "iprop-production-"));
    const source = path.join(root, "examples/portrait.ips");
    let counter = 0;
    const compile = flags => {
        const file = path.join(dir, String(counter++) + ".iprop");
        run(compiler, [source, "-o", file, ...flags]);
        return { file, bytes: fs.readFileSync(file) };
    };
    try {
        const production = compile([]);
        const plain = compile(["--no-compress"]);
        const debug = compile(["--debug"]);
        const debugPlain = compile(["--debug", "--no-compress"]);
        assert.equal(hasDebug(production.bytes), false);
        assert.equal(hasDebug(debug.bytes), true);
        assert.equal(production.bytes.readUInt32LE(8) & 2, 2);
        assert.equal(plain.bytes.readUInt32LE(8) & 2, 0);
        assert.deepEqual(expanded(production.bytes), plain.bytes);
        assert.deepEqual(expanded(debug.bytes), debugPlain.bytes);
        assert.ok(production.bytes.length < plain.bytes.length);
        assert.ok(plain.bytes.length < debugPlain.bytes.length);
        for (const flags of [["--strip"], ["--debug", "--strip"], ["--no-compress", "--compress"]]) {
            assert.deepEqual(compile(flags).bytes, production.bytes);
        }
        assert.deepEqual(compile(["--strip", "--debug"]).bytes, debug.bytes);
        assert.deepEqual(compile(["--compress", "--no-compress"]).bytes, plain.bytes);
        assert.match(run(tool("ipdis"), [debug.file, "--source"]), /; line [1-9]/);
        assert.doesNotMatch(run(tool("ipdis"), [production.file, "--source"]), /; line [1-9]/);
        const results = [production, plain, debug, debugPlain].map(({ file }, i) => {
            const png = path.join(dir, i + ".png");
            const meta = JSON.parse(run(tool("ipgen"), [file, "--seed", "26", "--float-bits", "-o", png]));
            return { meta, pixels: fs.readFileSync(png) };
        });
        for (const result of results.slice(1)) assert.deepEqual(result, results[0]);
    } finally { fs.rmSync(dir, { recursive: true, force: true }); }
});

test("editor inspector keeps source maps and binary export uses production output", { timeout: 30000 }, async () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "iprop-editor-production-"));
    const entry = path.join(dir, "entry.ips");
    const exported = path.join(dir, "export.iprop");
    fs.writeFileSync(entry, "asset t { size 8x8 draw {\n  rect(0,0,8,8,#345)\n} }");
    const commands = new Map();
    const disposable = () => ({ dispose() {} });
    let post, notify;
    const inspection = new Promise(resolve => { post = resolve; });
    const notification = new Promise(resolve => { notify = resolve; });
    const vscode = {
        CodeActionKind: { QuickFix: "quickfix" },
        ViewColumn: { Beside: 2 },
        Uri: { file: fsPath => ({ fsPath, toString: () => fsPath }) },
        commands: { registerCommand(name, callback) { commands.set(name, callback); return disposable(); } },
        languages: {
            createDiagnosticCollection: () => ({ set() {}, clear() {}, delete() {}, dispose() {} }),
            registerCodeLensProvider: disposable,
            registerCodeActionsProvider: disposable,
        },
        workspace: {
            textDocuments: [],
            getConfiguration: () => ({ get: key => ({ compilerPath: compiler,
                disassemblerPath: tool("ipdis"), validatorPath: tool("ipval") })[key] }),
            onDidChangeTextDocument: disposable,
            onDidSaveTextDocument: disposable,
            onDidCloseTextDocument: disposable,
            createFileSystemWatcher: () => ({ ...disposable(), onDidChange() {}, onDidCreate() {}, onDidDelete() {} }),
        },
        window: {
            activeTextEditor: { document: { fileName: entry, languageId: "ips", getText: () => fs.readFileSync(entry, "utf8") } },
            createOutputChannel: () => ({ appendLine() {}, dispose() {} }),
            setStatusBarMessage: disposable,
            onDidChangeTextEditorSelection: disposable,
            onDidChangeActiveTextEditor: disposable,
            createWebviewPanel: () => ({ onDidDispose() {}, webview: { postMessage: post, onDidReceiveMessage() {} } }),
            showSaveDialog: async () => ({ fsPath: exported }),
            showInformationMessage: notify,
            showErrorMessage: message => notify(new Error(message)),
        },
    };
    const extension = path.join(root, "editor/extension.js");
    const requireExtension = createRequire(extension);
    const context = vm.createContext({ module: { exports: {} }, setTimeout, clearTimeout, AbortController, Buffer,
        require: name => name === "vscode" ? vscode : name === "os"
            ? { ...os, tmpdir: () => dir } : requireExtension(name) });
    try {
        vm.runInContext(fs.readFileSync(extension, "utf8"), context, { filename: extension });
        context.module.exports.activate({ subscriptions: [] });
        commands.get("iprop.inspector")();
        const message = await inspection;
        assert.equal(message.type, "disassembly", message.message);
        assert.match(message.text, /; line [1-9]/);
        await commands.get("iprop.exportBinary")();
        const notice = await notification;
        assert.equal(typeof notice, "string", String(notice));
        assert.match(notice, /ipval: valid/);
        assert.equal(hasDebug(fs.readFileSync(exported)), false);
        const reference = path.join(dir, "reference.iprop");
        run(compiler, [entry, "-o", reference]);
        assert.deepEqual(fs.readFileSync(exported), fs.readFileSync(reference));
    } finally { fs.rmSync(dir, { recursive: true, force: true }); }
});
