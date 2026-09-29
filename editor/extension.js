// The iprop studio VS Code extension: .ips syntax highlighting and inline
// diagnostics from the native ipc compiler. The integrated terminal is the
// console for CLI agents (opencode/codex) — they edit the files on disk and
// the watcher recompiles.

const vscode = require("vscode");
const { execFile } = require("child_process");
const path = require("path");
const fs = require("fs");

let diagnostics;

// The compiler binary: the configured path, or <workspace>/build/tools/ipc.
function compilerBinary() {
    const configured = vscode.workspace.getConfiguration("iprop").get("compilerPath");
    if (configured) return configured;
    const workspace = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
    if (workspace) {
        const candidate = path.join(workspace, "build", "tools", "ipc");
        if (fs.existsSync(candidate)) return candidate;
    }
    return null;
}

// The native ipc prints "ipc: <file>:" followed by "N:M: error: <message>"
// lines (plus a caret snippet). This parses them into per-file diagnostics.
function parseDiagnostics(stderr, entry) {
    const byFile = new Map();
    let currentFile = entry;
    for (const line of stderr.split("\n")) {
        const fileMatch = line.match(/^ipc: (.+):$/);
        if (fileMatch) {
            currentFile = fileMatch[1];
            continue;
        }
        const errorMatch = line.match(/^(\d+):(\d+): error: (.*)$/);
        if (errorMatch) {
            const list = byFile.get(currentFile) ?? [];
            list.push({
                line: parseInt(errorMatch[1], 10),
                column: parseInt(errorMatch[2], 10),
                message: errorMatch[3],
            });
            byFile.set(currentFile, list);
        }
    }
    return byFile;
}

function compileActive() {
    const editor = vscode.window.activeTextEditor;
    if (!editor || editor.document.languageId !== "ips") return;
    const binary = compilerBinary();
    if (!binary) {
        vscode.window.showWarningMessage(
            "iprop: compiler not found — set iprop.compilerPath or build the toolchain (build/tools/ipc)");
        return;
    }
    const entry = editor.document.fileName;
    const cwd = path.dirname(entry);

    execFile(binary, [entry], { cwd, timeout: 15000 }, (error, stdout, stderr) => {
        const byFile = parseDiagnostics(stderr, entry);
        for (const [name, list] of byFile) {
            const uri = vscode.Uri.file(
                path.isAbsolute(name) ? name : path.join(cwd, name));
            diagnostics.set(uri, list.map((d) =>
                new vscode.Diagnostic(
                    new vscode.Range(d.line - 1, Math.max(0, d.column - 1),
                                     d.line - 1, Math.max(0, d.column - 1)),
                    d.message, vscode.DiagnosticSeverity.Error)));
        }
        if (!error) {
            vscode.window.setStatusBarMessage("iprop: ✓ compiles", 3000);
        }
    });
}

function activate(context) {
    diagnostics = vscode.languages.createDiagnosticCollection("iprop");
    context.subscriptions.push(diagnostics);

    context.subscriptions.push(
        vscode.commands.registerCommand("iprop.compile", compileActive));

    let timer = null;
    const schedule = () => {
        clearTimeout(timer);
        timer = setTimeout(compileActive, 500);
    };
    context.subscriptions.push(vscode.workspace.onDidChangeTextDocument((e) => {
        if (e.document.languageId === "ips") schedule();
    }));
    context.subscriptions.push(
        vscode.window.onDidChangeActiveTextEditor(() => schedule()));

    // External edits (CLI agents writing files on disk) also recompile.
    const watcher = vscode.workspace.createFileSystemWatcher("**/*.ips");
    context.subscriptions.push(watcher);
    watcher.onDidChange(schedule);
    watcher.onDidCreate(schedule);

    if (vscode.window.activeTextEditor?.document.languageId === "ips") {
        compileActive();
    }
}

function deactivate() {}

module.exports = { activate, deactivate };
