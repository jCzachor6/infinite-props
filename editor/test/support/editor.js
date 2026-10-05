"use strict";
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const { createRequire } = require("node:module");

function editorHarness(dir, execute) {
    const commands = new Map(), documents = new Map(), collection = new Map(), events = {};
    const warnings = [], statuses = [], logs = [], subscriptions = [];
    const uri = file => ({ fsPath: file, toString: () => "file:" + file });
    function document(file, text = fs.readFileSync(file, "utf8")) {
        const doc = { fileName: file, uri: uri(file), languageId: "ips", version: 1, isDirty: false,
            getText: () => doc.text, text };
        documents.set(file, doc);
        return doc;
    }
    const entry = document(path.join(dir, "entry.ips"));
    // Activate without an implicit request, so tests own the process ordering.
    entry.languageId = "plaintext";
    const disposable = () => ({ dispose() {} });
    const event = name => callback => { events[name] = callback; return disposable(); };
    const watcher = { ...disposable(), onDidChange: event("diskChange"), onDidCreate: event("diskCreate"), onDidDelete: event("diskDelete") };
    class Range { constructor(a, b, c, d) { this.start = { line: a, character: b }; this.end = { line: c, character: d }; } }
    class Diagnostic { constructor(range, message, severity) { Object.assign(this, { range, message, severity }); } }
    class Location { constructor(uri, range) { Object.assign(this, { uri, range }); } }
    class DiagnosticRelatedInformation { constructor(location, message) { Object.assign(this, { location, message }); } }
    class CodeAction { constructor(title, kind) { Object.assign(this, { title, kind }); } }
    const vscode = {
        Uri: { file: uri }, Range, Diagnostic, Location, DiagnosticRelatedInformation, CodeAction,
        CodeActionKind: { QuickFix: "quickfix" }, DiagnosticSeverity: { Error: 0, Warning: 1, Information: 2 },
        commands: { registerCommand(name, callback) { commands.set(name, callback); return disposable(); } },
        languages: {
            createDiagnosticCollection: () => ({ ...disposable(), set: (uri, items) => collection.set(uri.fsPath, items),
                clear: () => collection.clear() }),
            registerCodeLensProvider: disposable,
            registerCodeActionsProvider(_selector, provider) { vscode.provider = provider; return disposable(); },
        },
        workspace: {
            get textDocuments() { return [...documents.values()]; },
            getConfiguration: () => ({ get: key => key === "compilerPath" ? "test-ipc" : null }),
            onDidChangeTextDocument: event("textChange"), onDidSaveTextDocument: event("save"), onDidCloseTextDocument: event("close"),
            createFileSystemWatcher: () => watcher,
            openTextDocument: async uri => documents.get(uri.fsPath) ?? document(uri.fsPath),
        },
        window: {
            activeTextEditor: { document: entry },
            createOutputChannel: () => ({ ...disposable(), appendLine: text => logs.push(text) }),
            showWarningMessage: message => warnings.push(message), showInformationMessage: message => message,
            setStatusBarMessage: message => statuses.push(message),
            onDidChangeTextEditorSelection: event("selection"), onDidChangeActiveTextEditor: event("active"),
            showTextDocument: async doc => {
                if (vscode.emitActiveOnShow) {
                    vscode.window.activeTextEditor = { document: doc };
                    events.active(vscode.window.activeTextEditor);
                }
                return { document: doc, edit: async callback => {
                const version = doc.version;
                const edits = [];
                callback({ replace: (range, text) => edits.push({ range, text }) });
                if (vscode.concurrentEdit) vscode.concurrentEdit(doc);
                if (doc.version !== version) return false;
                const offset = position => doc.text.split("\n").slice(0, position.line)
                    .reduce((n, line) => n + line.length + 1, 0) + position.character;
                for (const { range, text } of edits.sort((a, b) => offset(b.range.start) - offset(a.range.start)))
                    doc.text = doc.text.slice(0, offset(range.start)) + text + doc.text.slice(offset(range.end));
                ++doc.version; doc.isDirty = true;
                return true;
            } };
            },
        },
    };
    const extension = path.resolve(__dirname, "../../extension.js");
    const requireExtension = createRequire(extension);
    const context = vm.createContext({ module: { exports: {} }, setTimeout, clearTimeout, AbortController, Buffer,
        require: name => name === "vscode" ? vscode : name === "child_process"
            ? { ...require("node:child_process"), execFile: execute } : requireExtension(name) });
    vm.runInContext(fs.readFileSync(extension, "utf8"), context, { filename: extension });
    context.module.exports.activate({ subscriptions });
    entry.languageId = "ips";
    return { vscode, commands, documents, collection, events, entry, document, warnings, statuses, logs,
        compile: () => commands.get("iprop.compile")(),
        actions: doc => vscode.provider.provideCodeActions(doc, null, { diagnostics: collection.get(doc.fileName) ?? [] }),
        dispose() { context.module.exports.deactivate(); for (const item of subscriptions) item.dispose(); } };
}
module.exports = { editorHarness };
