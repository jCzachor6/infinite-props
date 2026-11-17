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
            registerCompletionItemProvider: () => ({ dispose() {} }),
            registerHoverProvider: () => ({ dispose() {} }),
            registerDocumentFormattingEditProvider: disposable,
        },
        workspace: {
            textDocuments: [],
            getConfiguration: () => ({ get: key => ({ compilerPath: compiler,
                disassemblerPath: tool("ipdis"), validatorPath: tool("ipval") })[key] }),
            onDidChangeTextDocument: disposable,
            onDidSaveTextDocument: disposable,
            onDidCloseTextDocument: disposable,
            onDidChangeConfiguration: disposable,
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

test("editor previews, inspects and exports a model", { timeout: 30000 }, async () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "iprop-editor-model-"));
    const entry = path.join(dir, "crate.ips");
    fs.writeFileSync(path.join(dir, "tile.ips"), "asset tile { size 16x8 input color tint = #a3713d draw {\n  rect(0,0,16,8,tint)\n} }");
    fs.writeFileSync(entry, "import tile from \"tile.ips\"\nasset crate { size 12x10x8 input f32 wide = 8.0 render {\n" +
        "  box({0, 0, 0}, {wide, 10, 8}, 0.5, tile.draw(seed.initial()))\n} }");
    const commands = new Map();
    const disposable = () => ({ dispose() {} });
    const waiting = [];
    const posted = [];
    const post = message => {
        posted.push(message);
        for (const item of [...waiting]) {
            if (!item.matches(message)) continue;
            waiting.splice(waiting.indexOf(item), 1);
            item.resolve(message);
        }
    };
    const next = matches => new Promise(resolve => waiting.push({ matches, resolve }));
    const panels = [];
    let saveOptions, notify;
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
            registerCompletionItemProvider: () => ({ dispose() {} }),
            registerHoverProvider: () => ({ dispose() {} }),
            registerDocumentFormattingEditProvider: disposable,
        },
        workspace: {
            textDocuments: [],
            getConfiguration: () => ({ get: key => ({ compilerPath: compiler, rendererPath: tool("ipgen"),
                disassemblerPath: tool("ipdis"), validatorPath: tool("ipval") })[key] }),
            onDidChangeTextDocument: disposable,
            onDidSaveTextDocument: disposable,
            onDidCloseTextDocument: disposable,
            onDidChangeConfiguration: disposable,
            createFileSystemWatcher: () => ({ ...disposable(), onDidChange() {}, onDidCreate() {}, onDidDelete() {} }),
        },
        window: {
            activeTextEditor: { document: { fileName: entry, languageId: "ips", getText: () => fs.readFileSync(entry, "utf8") } },
            createOutputChannel: () => ({ appendLine() {}, dispose() {} }),
            setStatusBarMessage: disposable,
            onDidChangeTextEditorSelection: disposable,
            onDidChangeActiveTextEditor: disposable,
            createWebviewPanel: kind => {
                const panel = { kind, onDidDispose() {}, reveal() {},
                    webview: { html: "", postMessage: post, onDidReceiveMessage() {} } };
                panels.push(panel);
                return panel;
            },
            showSaveDialog: async options => { saveOptions = options; return { fsPath: path.join(dir, "exported.iprop3d") }; },
            showInformationMessage: notify,
            showWarningMessage: message => notify(new Error(message)),
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

        // The preview compiles the model and renders a picture of it with
        // the command line of any other asset.
        const rendered = next(message => message.type === "render" || message.type === "error");
        commands.get("iprop.preview")();
        const render = await rendered;
        assert.equal(render.type, "render", render.message);
        const picture = Buffer.from(render.png, "base64");
        assert.equal(picture.subarray(1, 4).toString("latin1"), "PNG");
        const result = JSON.parse(render.json);
        assert.equal(result.triangles, 44);
        assert.deepEqual(result.inputs, [{ name: "wide", type: "f32", default: 8 }]);

        // The page shows the model's numbers and its inputs instead of 2D outputs.
        const page = panels.find(panel => panel.kind === "iprop.preview").webview.html;
        const script = /<script>([\s\S]*)<\/script>/.exec(page)[1];
        const elements = new Map();
        const element = id => {
            if (!elements.has(id)) elements.set(id, { id, textContent: "", className: "", value: "", children: [],
                addEventListener() {}, appendChild(child) { this.children.push(child); } });
            return elements.get(id);
        };
        let receive;
        vm.runInNewContext(script, {
            acquireVsCodeApi: () => ({ postMessage() {} }),
            document: { getElementById: element, createElement: () => element(Symbol("created")) },
            window: { addEventListener: (_kind, handler) => { receive = handler; } },
            parseInt, JSON, String,
        });
        receive({ data: render });
        assert.equal(element("status").textContent, "fuel: " + result.fuel.spent + " / " + result.fuel.budget);
        assert.equal(element("outputs").textContent, "96 vertices, 44 triangles, 1 surfaces\ntexture tile 16x8");
        assert.equal(element("inputs").children.length, 1);
        assert.match(element("preview").src, /^data:image\/png;base64,/);

        // The assembly listing does not cover models yet, and says so.
        const inspected = next(message => message.type === "disassembly" || message.type === "error");
        commands.get("iprop.inspector")();
        const listing = await inspected;
        assert.equal(listing.type, "error");
        assert.match(listing.message, /this is an \.iprop3d model; ipdis does not read models yet/);

        // An export is offered under the model's own extension and validates.
        await commands.get("iprop.exportBinary")();
        const notice = await notification;
        assert.equal(typeof notice, "string", String(notice));
        assert.match(notice, /ipval: valid/);
        assert.match(saveOptions.defaultUri.fsPath, /crate\.iprop3d$/);
        const exported = fs.readFileSync(path.join(dir, "exported.iprop3d"));
        assert.equal(exported.subarray(0, 4).toString("latin1"), "IP3D");
        const reference = path.join(dir, "reference.iprop3d");
        run(compiler, [entry, "-o", reference]);
        assert.deepEqual(exported, fs.readFileSync(reference));
    } finally { fs.rmSync(dir, { recursive: true, force: true }); }
});
