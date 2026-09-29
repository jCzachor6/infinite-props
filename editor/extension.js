// The iprop studio VS Code extension: .ips syntax highlighting, inline
// diagnostics from the native ipc compiler and a live preview panel with
// seed/variant/archetype sliders and uniform editors. The integrated
// terminal is the console for CLI agents (opencode/codex) — they edit the
// files on disk and the watcher recompiles.

const vscode = require("vscode");
const { execFile } = require("child_process");
const path = require("path");
const fs = require("fs");
const os = require("os");

let diagnostics;

// ---- the compiler and renderer binaries -------------------------------------

function toolBinary(configKey, toolName) {
    const configured = vscode.workspace.getConfiguration("iprop").get(configKey);
    if (configured) return configured;
    const workspace = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
    if (workspace) {
        const candidate = path.join(workspace, "build", "tools", toolName);
        if (fs.existsSync(candidate)) return candidate;
    }
    return null;
}

function compilerBinary() { return toolBinary("compilerPath", "ipc"); }
function rendererBinary() { return toolBinary("rendererPath", "ipgen"); }

// ---- diagnostics (Phase 28) ----------------------------------------------------

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

// ---- the preview panel (Phase 29) -----------------------------------------------

const previewState = { seed: 0, variant: 0, archetype: 0, uniforms: {} };
let previewPanel = null;

function previewHtml() {
    return `<!doctype html>
<html>
<head>
<meta charset="utf-8">
<style>
  body { font-family: var(--vscode-font-family); color: var(--vscode-foreground); padding: 1rem; }
  img { image-rendering: pixelated; max-width: 320px; width: 100%; border: 1px solid var(--vscode-panel-border); background: #0a0f14; }
  .row { display: flex; align-items: center; gap: 0.6rem; margin: 0.5rem 0; font-size: 0.85rem; }
  .row label { width: 5.5rem; }
  input[type=range] { flex: 1; }
  input[type=number], input[type=text] { width: 7rem; background: var(--vscode-input-background); color: var(--vscode-input-foreground); border: 1px solid var(--vscode-input-border); padding: 0.2rem; }
  #status { margin-top: 0.8rem; font-size: 0.8rem; }
  #status.error { color: var(--vscode-errorForeground); white-space: pre-wrap; font-family: var(--vscode-editor-font-family); }
  #outputs { margin-top: 0.8rem; font-size: 0.8rem; white-space: pre-wrap; }
  #inputs { display: flex; flex-direction: column; }
  .uniform { display: flex; align-items: center; gap: 0.6rem; margin: 0.25rem 0; font-size: 0.85rem; }
  .uniform label { width: 5.5rem; }
</style>
</head>
<body>
  <img id="preview" alt="preview">
  <div class="row"><label>seed</label>
    <input type="range" id="seed-slider" min="0" max="4095" value="0">
    <input type="number" id="seed-value" min="0" value="0"></div>
  <div class="row"><label>variant</label>
    <input type="range" id="variant-slider" min="0" max="15" value="0">
    <input type="number" id="variant-value" min="0" max="15" value="0"></div>
  <div class="row"><label>archetype</label>
    <input type="range" id="archetype-slider" min="0" max="15" value="0">
    <input type="number" id="archetype-value" min="0" max="15" value="0"></div>
  <div id="inputs"></div>
  <div id="status">compile the active .ips to preview it</div>
  <div id="outputs"></div>
<script>
  const vscode = acquireVsCodeApi();

  function slider(kind, sliderId, valueId) {
    const slider = document.getElementById(sliderId);
    const value = document.getElementById(valueId);
    slider.addEventListener("input", () => {
      value.value = slider.value;
      vscode.postMessage({ type: "slider", kind, value: parseInt(slider.value, 10) });
    });
    value.addEventListener("change", () => {
      slider.value = value.value;
      vscode.postMessage({ type: "slider", kind, value: parseInt(value.value, 10) });
    });
  }
  slider("seed", "seed-slider", "seed-value");
  slider("variant", "variant-slider", "variant-value");
  slider("archetype", "archetype-slider", "archetype-value");

  window.addEventListener("message", (event) => {
    const message = event.data;
    const status = document.getElementById("status");
    const outputs = document.getElementById("outputs");
    if (message.type === "render") {
      document.getElementById("preview").src = "data:image/png;base64," + message.png;
      const json = JSON.parse(message.json);
      status.textContent = "fuel: " + json.fuel.spent + " / " + json.fuel.budget;
      status.className = "";
      outputs.textContent = json.outputs.map((o) =>
        o.name + " (" + o.kind + (o.list ? ", list" : "") + "): " +
        o.values.map((v) => JSON.stringify(v)).join(", ")).join("\\n");
      document.getElementById("inputs").textContent = "";
      for (const input of json.inputs) {
        const row = document.createElement("div");
        row.className = "uniform";
        const label = document.createElement("label");
        label.textContent = input.name;
        const field = document.createElement("input");
        field.type = input.type === "color" ? "text" : "number";
        field.value = String(input.default);
        field.addEventListener("change", () => {
          vscode.postMessage({ type: "uniform", name: input.name, value: field.value });
        });
        row.appendChild(label);
        row.appendChild(field);
        document.getElementById("inputs").appendChild(row);
      }
    } else if (message.type === "compiling") {
      status.textContent = "compiling…";
      status.className = "";
    } else if (message.type === "error") {
      status.textContent = message.message;
      status.className = "error";
      document.getElementById("preview").removeAttribute("src");
    }
  });
</script>
</body>
</html>`;
}

function post(message) {
    if (previewPanel) previewPanel.webview.postMessage(message);
}

function renderPreview() {
    const editor = vscode.window.activeTextEditor;
    if (!editor || editor.document.languageId !== "ips") return;
    const compiler = compilerBinary();
    const renderer = rendererBinary();
    if (!compiler || !renderer) {
        post({ type: "error", message: "iprop: compiler/renderer not found — set iprop.compilerPath/iprop.rendererPath or build the toolchain" });
        return;
    }
    const entry = editor.document.fileName;
    const cwd = path.dirname(entry);
    const tmp = os.tmpdir();
    const ipropPath = path.join(tmp, "iprop-preview.iprop");
    const pngPath = path.join(tmp, "iprop-preview.png");

    post({ type: "compiling" });
    execFile(compiler, [entry, "-o", ipropPath], { cwd, timeout: 15000 }, (compileError, _out, stderr) => {
        if (compileError || !fs.existsSync(ipropPath)) {
            const summary = stderr.split("\n").filter((l) => l.includes("error:")).join("\n") || "compilation failed";
            post({ type: "error", message: summary });
            return;
        }
        const args = [ipropPath, "--seed", String(previewState.seed >>> 0),
            "--variant", String(previewState.variant), "--archetype", String(previewState.archetype),
            "-o", pngPath];
        for (const [name, value] of Object.entries(previewState.uniforms)) {
            args.push("--uniform", name + "=" + value);
        }
        execFile(renderer, args, { timeout: 15000 }, (renderError, stdout, renderStderr) => {
            if (renderError) {
                post({ type: "error", message: renderStderr || "generation failed" });
                return;
            }
            try {
                const png = fs.readFileSync(pngPath).toString("base64");
                post({ type: "render", png, json: stdout });
            } catch {
                post({ type: "error", message: "cannot read the rendered PNG" });
            }
        });
    });
}

function openPreview() {
    if (previewPanel) {
        previewPanel.reveal(vscode.ViewColumn.Beside);
        return;
    }
    previewPanel = vscode.window.createWebviewPanel(
        "iprop.preview", "iprop preview", vscode.ViewColumn.Beside,
        { enableScripts: true, retainContextWhenHidden: true });
    previewPanel.webview.html = previewHtml();
    previewPanel.onDidDispose(() => { previewPanel = null; });
    previewPanel.webview.onDidReceiveMessage((message) => {
        if (message.type === "slider") {
            if (message.kind === "seed") previewState.seed = message.value >>> 0;
            if (message.kind === "variant") previewState.variant = message.value >>> 0;
            if (message.kind === "archetype") previewState.archetype = message.value >>> 0;
            renderPreview();
        } else if (message.type === "uniform") {
            previewState.uniforms[message.name] = message.value;
            renderPreview();
        }
    });
    renderPreview();
}

// ---- activation ---------------------------------------------------------------------

function activate(context) {
    diagnostics = vscode.languages.createDiagnosticCollection("iprop");
    context.subscriptions.push(diagnostics);

    context.subscriptions.push(
        vscode.commands.registerCommand("iprop.compile", compileActive));
    context.subscriptions.push(
        vscode.commands.registerCommand("iprop.preview", openPreview));

    let timer = null;
    const schedule = () => {
        clearTimeout(timer);
        timer = setTimeout(() => {
            compileActive();
            if (previewPanel) renderPreview();
        }, 500);
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
