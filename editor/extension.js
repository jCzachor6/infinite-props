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
const { runCompilerDiagnostics, editorRange, sourceContentId, fixEdits } = require("./diagnostics");

let diagnostics;
let log;

// ---- the compiler and renderer binaries -------------------------------------

function toolBinary(configKey, toolName) {
    const configured = vscode.workspace.getConfiguration("iprop").get(configKey);
    if (configured) {
        log.appendLine("tool " + toolName + ": configured path " + configured);
        return configured;
    }
    const candidates = [];
    // 1. The workspace root (the repository opened directly).
    const workspace = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
    if (workspace) candidates.push(path.join(workspace, "build", "tools", toolName));
    // 2. Walk up from the entry file (or the active editor): the repository
    //    may be opened at a subfolder (e.g. examples/), while the toolchain
    //    lives at the repository root's build/tools. The active editor is
    //    undefined while the preview panel has focus, hence the state.
    const editor = vscode.window.activeTextEditor;
    const from = editor ? path.dirname(editor.document.fileName)
                        : (previewState.entry ? path.dirname(previewState.entry) : null);
    let dir = from;
    while (dir) {
        candidates.push(path.join(dir, "build", "tools", toolName));
        const parent = path.dirname(dir);
        if (parent === dir) break;
        dir = parent;
    }
    for (const candidate of candidates) {
        if (fs.existsSync(candidate)) {
            log.appendLine("tool " + toolName + ": " + candidate);
            return candidate;
        }
    }
    log.appendLine("tool " + toolName + ": NOT FOUND (tried " + candidates.join(", ") + ")");
    return null;
}

function compilerBinary() { return toolBinary("compilerPath", "ipc"); }
function rendererBinary() { return toolBinary("rendererPath", "ipgen"); }

// ---- compiler diagnostics and guarded quick fixes ----------------------------

let diagnosticGeneration = 0;
let diagnosticRequest = null;
let fixOpeningDocument = null;
const diagnosticMetadata = new WeakMap();

function invalidateDiagnostics() {
    ++diagnosticGeneration;
    diagnosticRequest?.abort();
    diagnosticRequest = null;
    diagnostics?.clear();
}

function sourceSnapshot(uri) {
    const open = vscode.workspace.textDocuments.find(doc => doc.uri.toString() === uri.toString());
    try {
        const bytes = fs.readFileSync(uri.fsPath);
        const decoded = bytes.toString("utf8");
        const text = open ? open.getText() : decoded;
        const modelText = decoded.replace(/^\ufeff/, "");
        const normalized = modelText.replace(/\r\n|\r/g, "\n");
        const savedMatches = text === decoded || (!open?.isDirty &&
            (text === normalized || text === normalized.replace(/\n/g, "\r\n")));
        return { text, nativeText: decoded, sourceId: sourceContentId(bytes), savedMatches,
            validUtf8: Buffer.from(decoded, "utf8").equals(bytes) };
    } catch {
        return open ? { text: open.getText(), nativeText: open.getText(), sourceId: null,
            savedMatches: false, validUtf8: true } : null;
    }
}

function currentFixes(document, raw) {
    const source = sourceSnapshot(document.uri);
    return source?.savedMatches && source.validUtf8 && source.sourceId === raw.source_id
        ? fixEdits(source.nativeText, raw) : null;
}

function ownedUri(file, cwd, entry) {
    return vscode.Uri.file(file ? path.resolve(cwd, file) : entry);
}

function vscodeRange(range) {
    return new vscode.Range(range.start.line, range.start.character, range.end.line, range.end.character);
}

async function compileActive() {
    invalidateDiagnostics();
    const generation = diagnosticGeneration;
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
    const savedSource = sourceSnapshot(vscode.Uri.file(entry));
    // The CLI reads saved files. Never label a different unsaved buffer as
    // checked or place a disk diagnostic on text the compiler did not see.
    if (savedSource?.sourceId && !savedSource.savedMatches) {
        vscode.window.setStatusBarMessage("iprop: save changes to check the current source", 3000);
        return;
    }
    const documents = new Set([...vscode.workspace.textDocuments, editor.document]);
    const snapshots = [...documents].filter(doc => doc.languageId === "ips")
        .map(doc => ({ doc, version: doc.version, text: doc.getText() }));
    const request = new AbortController();
    diagnosticRequest = request;
    const current = () => generation === diagnosticGeneration && !request.signal.aborted &&
        snapshots.every(snapshot => snapshot.doc.version === snapshot.version && snapshot.doc.getText() === snapshot.text) &&
        sourceSnapshot(vscode.Uri.file(entry))?.sourceId === savedSource?.sourceId;
    try {
        const result = await runCompilerDiagnostics(binary, entry, { execute: execFile, signal: request.signal });
        if (!current()) return;
        const byFile = new Map();
        let changedSource = false;
        for (const raw of result.diagnostics) {
            const uri = ownedUri(raw.file, cwd, entry);
            const source = sourceSnapshot(uri);
            if ((raw.source_id && source?.sourceId !== raw.source_id) ||
                ((raw.location || raw.range) && source && !source.savedMatches)) {
                changedSource = true;
                continue;
            }
            // Invalid UTF-8 has an exact native byte hash, but its replacement
            // characters in the editor cannot represent the native byte range.
            // Keep the lexer error at its legacy point; never invent an edit.
            const range = editorRange(source?.nativeText ?? "", source?.validUtf8 === false ? null : raw.range, raw.location);
            if (!range) { log.appendLine("diagnostic has an invalid source range: " + raw.code); continue; }
            const severity = { error: vscode.DiagnosticSeverity.Error, warning: vscode.DiagnosticSeverity.Warning,
                note: vscode.DiagnosticSeverity.Information }[raw.severity];
            const diagnostic = new vscode.Diagnostic(vscodeRange(range), raw.message, severity);
            diagnostic.source = "ipc";
            if (raw.code) diagnostic.code = raw.code;
            diagnostic.relatedInformation = raw.related_locations.flatMap(related => {
                const relatedUri = ownedUri(related.file, cwd, entry);
                const relatedSource = sourceSnapshot(relatedUri);
                const relatedRange = relatedSource?.savedMatches && relatedSource.validUtf8
                    ? editorRange(relatedSource.nativeText, related.range) : null;
                return relatedRange ? [new vscode.DiagnosticRelatedInformation(
                    new vscode.Location(relatedUri, vscodeRange(relatedRange)), related.message)] : [];
            });
            diagnosticMetadata.set(diagnostic, { raw, uri, generation });
            const key = uri.toString();
            const group = byFile.get(key) ?? { uri, diagnostics: [] };
            group.diagnostics.push(diagnostic);
            byFile.set(key, group);
        }
        if (!current()) return;
        for (const group of byFile.values()) diagnostics.set(group.uri, group.diagnostics);
        if (changedSource) log.appendLine("discarded diagnostics for changed source; save and check again");
        else if (result.status === "ok" && snapshots.every(snapshot => !snapshot.doc.isDirty))
            vscode.window.setStatusBarMessage("iprop: ✓ checks", 3000);
        if (result.protocol === "text") log.appendLine("compiler 0.4 text diagnostics (temporary output cleaned)");
    } catch (error) {
        if (!current()) return;
        // A transport/protocol failure is visible and never silently retried as
        // text. Only an explicit unsupported-option response enables fallback.
        log.appendLine("diagnostics failed: " + error.message);
        vscode.window.setStatusBarMessage("iprop: diagnostics failed — see the iprop output", 5000);
    } finally {
        if (diagnosticRequest === request) diagnosticRequest = null;
    }
}

class IpsCodeActionProvider {
    provideCodeActions(document, _range, context) {
        const actions = [];
        for (const diagnostic of context.diagnostics) {
            const saved = diagnosticMetadata.get(diagnostic);
            if (!saved || saved.generation !== diagnosticGeneration || saved.uri.toString() !== document.uri.toString()) continue;
            const { raw } = saved;
            const source = sourceSnapshot(document.uri);
            if (raw.source_id && (!source?.savedMatches || source.sourceId !== raw.source_id)) continue;
            if (currentFixes(document, raw)) {
                const action = new vscode.CodeAction("Apply compiler fix", vscode.CodeActionKind.QuickFix);
                action.diagnostics = [diagnostic];
                action.command = { command: "iprop.applyCompilerFix", title: action.title,
                    arguments: [{ file: saved.uri.fsPath, diagnostic: raw, generation: saved.generation }] };
                actions.push(action);
            }
            for (const suggestion of raw.suggestions) {
                const action = new vscode.CodeAction(suggestion, vscode.CodeActionKind.QuickFix);
                action.diagnostics = [diagnostic];
                action.command = { command: "iprop.showCompilerSuggestion", title: suggestion, arguments: [suggestion] };
                actions.push(action);
            }
        }
        return actions;
    }
}

async function applyCompilerFix(saved) {
    if (!saved || saved.generation !== diagnosticGeneration) return false;
    const document = await vscode.workspace.openTextDocument(vscode.Uri.file(saved.file));
    if (saved.generation !== diagnosticGeneration) return false;
    let editor;
    fixOpeningDocument = document;
    try {
        editor = await vscode.window.showTextDocument(document, { preview: false, preserveFocus: true });
    } finally { fixOpeningDocument = null; }
    if (saved.generation !== diagnosticGeneration || !currentFixes(editor.document, saved.diagnostic)) {
        vscode.window.showWarningMessage("iprop: source changed; check again before applying this fix");
        return false;
    }
    // TextEditor.edit binds the transaction to the current document version;
    // it rejects a concurrent edit instead of applying stale positions.
    const applied = await editor.edit(builder => {
        for (const fix of currentFixes(editor.document, saved.diagnostic) ?? [])
            builder.replace(vscodeRange(fix.range), fix.replacement);
    });
    if (applied) invalidateDiagnostics();
    return applied;
}

// ---- the preview panel (Phase 29) -----------------------------------------------

const previewState = { seed: 0, variant: 0, archetype: 0, uniforms: {}, entry: null, compiled: false };
let previewPanel = null;
let renderTimer = null;
let atlasPanel = null;
let inspectorPanel = null;

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
      document.getElementById("status").textContent = "rendering " + kind + " " + slider.value + "…";
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
      // Clamp the sliders to the declared ranges.
      const variantSlider = document.getElementById("variant-slider");
      const variantValue = document.getElementById("variant-value");
      variantSlider.max = json.variants - 1;
      variantValue.max = json.variants - 1;
      const archetypeSlider = document.getElementById("archetype-slider");
      const archetypeValue = document.getElementById("archetype-value");
      archetypeSlider.max = json.archetypes - 1;
      archetypeValue.max = json.archetypes - 1;
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
    } else if (message.type === "clamp") {
      const slider = document.getElementById(message.kind + "-slider");
      const value = document.getElementById(message.kind + "-value");
      if (slider && value) {
        slider.value = 0;
        value.value = 0;
      }
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

function previewPaths() {
    const tmp = os.tmpdir();
    return { ipropPath: path.join(tmp, "iprop-preview.iprop"), pngPath: path.join(tmp, "iprop-preview.png") };
}

function activeEntry() {
    const editor = vscode.window.activeTextEditor;
    const entry = editor && editor.document.fileName.endsWith(".ips")
        ? editor.document.fileName
        : previewState.entry;
    return entry || null;
}

// Compiles `entry` into the temp .iprop; resolves with the path, rejects
// with the stderr summary.
function compileEntry(entry, { debug = false } = {}) {
    return new Promise((resolve, reject) => {
        const compiler = compilerBinary();
        if (!compiler) {
            reject("iprop: compiler not found — set iprop.compilerPath or build the toolchain");
            return;
        }
        const { ipropPath } = previewPaths();
        try { fs.unlinkSync(ipropPath); } catch {}
        log.appendLine("compile: " + compiler + " " + entry);
        execFile(compiler, [entry, "-o", ipropPath, ...(debug ? ["--debug"] : [])], { cwd: path.dirname(entry), timeout: 15000 },
            (error, _out, stderr) => {
                if (error || !fs.existsSync(ipropPath)) {
                    reject(stderr.split("\n").filter((l) => l.includes("error:")).join("\n") || "compilation failed");
                    return;
                }
                resolve(ipropPath);
            });
    });
}

// Renders one cell of the given binary; resolves with { png (base64), json }.
function renderCell(ipropPath, seed, variant, archetype, uniforms) {
    return new Promise((resolve, reject) => {
        const renderer = rendererBinary();
        if (!renderer) {
            reject("iprop: renderer not found — set iprop.rendererPath or build the toolchain");
            return;
        }
        const pngPath = path.join(os.tmpdir(),
            "iprop-cell-" + process.pid + "-" + Math.random().toString(36).slice(2) + ".png");
        const args = [ipropPath, "--seed", String(seed >>> 0),
            "--variant", String(variant), "--archetype", String(archetype), "-o", pngPath];
        for (const [name, value] of Object.entries(uniforms || {})) {
            args.push("--uniform", name + "=" + value);
        }
        execFile(renderer, args, { timeout: 15000 }, (error, stdout, stderr) => {
            if (error) {
                reject(stderr || "generation failed");
                return;
            }
            try {
                const png = fs.readFileSync(pngPath).toString("base64");
                try { fs.unlinkSync(pngPath); } catch {}
                resolve({ png, json: stdout });
            } catch {
                reject("cannot read the rendered PNG");
            }
        });
    });
}

// Renders the already-compiled .iprop with the current controls. Slider and
// uniform changes take this path (no recompilation), debounced so a drag
// does not pile up concurrent renders. Deliberately does NOT read the
// active editor: it is undefined while the preview panel has focus.
function renderOnly() {
    const renderer = rendererBinary();
    if (!renderer) {
        post({ type: "error", message: "iprop: renderer not found — set iprop.rendererPath or build the toolchain" });
        return;
    }
    const { ipropPath, pngPath } = previewPaths();
    if (!previewState.compiled || !fs.existsSync(ipropPath)) {
        compileAndRender();
        return;
    }
    const args = [ipropPath, "--seed", String(previewState.seed >>> 0),
        "--variant", String(previewState.variant), "--archetype", String(previewState.archetype),
        "-o", pngPath];
    for (const [name, value] of Object.entries(previewState.uniforms)) {
        args.push("--uniform", name + "=" + value);
    }
    log.appendLine("render: " + renderer + " " + args.slice(1).join(" "));
    execFile(renderer, args, { timeout: 15000 }, (renderError, stdout, renderStderr) => {
        if (renderError) {
            log.appendLine("render failed: " + (renderStderr || renderError.message));
            const detail = renderStderr || "";
            if (detail.includes("variant index out of range")) {
                previewState.variant = 0;
                post({ type: "clamp", kind: "variant" });
                renderOnly();
                return;
            }
            if (detail.includes("archetype index out of range")) {
                previewState.archetype = 0;
                post({ type: "clamp", kind: "archetype" });
                renderOnly();
                return;
            }
            post({ type: "error", message: detail || "generation failed" });
            return;
        }
        try {
            const png = fs.readFileSync(pngPath).toString("base64");
            post({ type: "render", png, json: stdout });
        } catch {
            post({ type: "error", message: "cannot read the rendered PNG" });
        }
    });
}

// Compiles the active .ips file (or the remembered entry when the panel has
// focus), then renders. Edits take this path.
function compileAndRender() {
    const editor = vscode.window.activeTextEditor;
    const entry = editor && editor.document.fileName.endsWith(".ips")
        ? editor.document.fileName
        : previewState.entry;
    if (!entry) {
        post({ type: "error", message: "iprop: open a .ips file to preview it" });
        return;
    }
    const compiler = compilerBinary();
    if (!compiler) {
        post({ type: "error", message: "iprop: compiler not found — set iprop.compilerPath or build the toolchain" });
        return;
    }
    const cwd = path.dirname(entry);
    const { ipropPath, pngPath } = previewPaths();

    // A failed compile must not leave a stale binary behind: remove the
    // previous outputs so a broken edit cannot show an old image.
    try { fs.unlinkSync(ipropPath); } catch {}
    try { fs.unlinkSync(pngPath); } catch {}

    log.appendLine("compile: " + compiler + " " + entry);
    post({ type: "compiling" });
    execFile(compiler, [entry, "-o", ipropPath], { cwd, timeout: 15000 }, (compileError, _out, stderr) => {
        if (compileError || !fs.existsSync(ipropPath)) {
            previewState.compiled = false;
            const summary = stderr.split("\n").filter((l) => l.includes("error:")).join("\n") || "compilation failed";
            post({ type: "error", message: summary });
            return;
        }
        previewState.compiled = true;
        previewState.entry = entry;
        renderOnly();
    });
}

// Debounced render for the sliders and uniforms: short enough to feel
// continuous while dragging, long enough to coalesce the drag events.
function scheduleRender() {
    clearTimeout(renderTimer);
    renderTimer = setTimeout(renderOnly, 100);
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
        log.appendLine("message: " + JSON.stringify(message));
        if (message.type === "slider") {
            if (message.kind === "seed") previewState.seed = message.value >>> 0;
            if (message.kind === "variant") previewState.variant = message.value >>> 0;
            if (message.kind === "archetype") previewState.archetype = message.value >>> 0;
            scheduleRender();
        } else if (message.type === "uniform") {
            previewState.uniforms[message.name] = message.value;
            scheduleRender();
        }
    });
    compileAndRender();
}

// ---- the atlas browser ---------------------------------------------------------------

function atlasHtml() {
    return `<!doctype html>
<html>
<head>
<meta charset="utf-8">
<style>
  body { font-family: var(--vscode-font-family); color: var(--vscode-foreground); padding: 1rem; }
  #grid { display: flex; flex-wrap: wrap; gap: 0.9rem; }
  .cell { cursor: pointer; text-align: center; }
  .cell img { image-rendering: pixelated; width: 96px; height: 96px; background: #0a0f14; border: 1px solid var(--vscode-panel-border); }
  .cell .label { font-size: 0.75rem; color: var(--vscode-descriptionForeground); margin-top: 0.25rem; }
  .cell.selected img { outline: 2px solid var(--vscode-focusBorder); }
  #status { margin-bottom: 0.8rem; font-size: 0.8rem; }
  #status.error { color: var(--vscode-errorForeground); white-space: pre-wrap; }
</style>
</head>
<body>
  <div id="status">building the atlas…</div>
  <div id="grid"></div>
<script>
  const vscode = acquireVsCodeApi();
  window.addEventListener("message", (event) => {
    const message = event.data;
    const status = document.getElementById("status");
    if (message.type === "grid") {
      status.textContent = "variants × archetypes at seed " + message.seed + " — click a cell to preview it";
      status.className = "";
      const grid = document.getElementById("grid");
      grid.textContent = "";
      for (const cell of message.cells) {
        const wrap = document.createElement("div");
        wrap.className = "cell";
        const img = document.createElement("img");
        img.src = "data:image/png;base64," + cell.png;
        const label = document.createElement("div");
        label.className = "label";
        label.textContent = "v" + cell.variant + " · a" + cell.archetype;
        wrap.appendChild(img);
        wrap.appendChild(label);
        wrap.addEventListener("click", () => {
          for (const node of grid.children) node.classList.remove("selected");
          wrap.classList.add("selected");
          vscode.postMessage({ type: "pick", variant: cell.variant, archetype: cell.archetype });
        });
        grid.appendChild(wrap);
      }
    } else if (message.type === "error") {
      status.textContent = message.message;
      status.className = "error";
    }
  });
</script>
</body>
</html>`;
}

async function buildAtlas() {
    const entry = activeEntry();
    if (!entry) {
        if (atlasPanel) atlasPanel.webview.postMessage({ type: "error", message: "open a .ips file first" });
        return;
    }
    try {
        const ipropPath = await compileEntry(entry);
        // Learn the declared counts from a seed render.
        const probe = await renderCell(ipropPath, previewState.seed >>> 0, 0, 0, previewState.uniforms);
        const meta = JSON.parse(probe.json);
        const cells = [];
        for (let a = 0; a < meta.archetypes; ++a) {
            for (let v = 0; v < meta.variants; ++v) {
                const cell = await renderCell(ipropPath, previewState.seed >>> 0, v, a, previewState.uniforms);
                cells.push({ variant: v, archetype: a, png: cell.png });
            }
        }
        if (atlasPanel) {
            atlasPanel.webview.postMessage({ type: "grid", seed: previewState.seed >>> 0, cells });
        }
    } catch (message) {
        if (atlasPanel) atlasPanel.webview.postMessage({ type: "error", message: String(message) });
    }
}

function openAtlas() {
    if (atlasPanel) {
        atlasPanel.reveal(vscode.ViewColumn.Beside);
        return;
    }
    atlasPanel = vscode.window.createWebviewPanel(
        "iprop.atlas", "iprop atlas", vscode.ViewColumn.Beside,
        { enableScripts: true, retainContextWhenHidden: true });
    atlasPanel.webview.html = atlasHtml();
    atlasPanel.onDidDispose(() => { atlasPanel = null; });
    atlasPanel.webview.onDidReceiveMessage((message) => {
        if (message.type === "pick") {
            previewState.variant = message.variant;
            previewState.archetype = message.archetype;
            if (previewPanel) scheduleRender();
            else openPreview();
        }
    });
    buildAtlas();
}

// ---- the bytecode inspector ------------------------------------------------------------

function inspectorHtml() {
    return `<!doctype html>
<html>
<head>
<meta charset="utf-8">
<style>
  body { font-family: var(--vscode-editor-font-family); color: var(--vscode-foreground); padding: 0.6rem; font-size: 0.8rem; }
  pre { margin: 0; white-space: pre; }
  .ins { cursor: pointer; }
  .ins:hover { background: var(--vscode-list-hoverBackground); }
  .ins.hit { background: var(--vscode-editor-selectionBackground); }
  .annot { color: var(--vscode-descriptionForeground); font-style: italic; }
  #status { color: var(--vscode-descriptionForeground); margin-bottom: 0.6rem; }
  #status.error { color: var(--vscode-errorForeground); white-space: pre-wrap; }
</style>
</head>
<body>
  <div id="status">compile the active .ips to disassemble it</div>
  <pre id="code"></pre>
<script>
  const vscode = acquireVsCodeApi();
  let sourceLines = [];
  window.addEventListener("message", (event) => {
    const message = event.data;
    const status = document.getElementById("status");
    if (message.type === "disassembly") {
      status.textContent = message.note || "disassembly (source lines from the debug section)";
      status.className = "";
      const code = document.getElementById("code");
      code.textContent = "";
      sourceLines = [];
      let currentLine = 0;
      for (const line of message.text.split("\n")) {
        const match = line.match(/^\s*; line (\d+)$/);
        if (match) {
          const annot = document.createElement("div");
          annot.className = "annot";
          annot.textContent = line;
          code.appendChild(annot);
          currentLine = parseInt(match[1], 10);
          continue;
        }
        const row = document.createElement("div");
        row.className = "ins";
        row.textContent = line;
        row.dataset.line = String(currentLine);
        row.addEventListener("click", () => {
          if (currentLine > 0) vscode.postMessage({ type: "goto", line: currentLine });
        });
        code.appendChild(row);
        sourceLines.push({ node: row, line: currentLine });
      }
    } else if (message.type === "cursor") {
      for (const entry of sourceLines) {
        entry.node.classList.toggle("hit", entry.line === message.line);
      }
      const hit = sourceLines.find((entry) => entry.line === message.line);
      if (hit) hit.node.scrollIntoView({ block: "center" });
    } else if (message.type === "error") {
      status.textContent = message.message;
      status.className = "error";
    }
  });
</script>
</body>
</html>`;
}

async function buildInspector() {
    const entry = activeEntry();
    if (!entry) {
        if (inspectorPanel) inspectorPanel.webview.postMessage({ type: "error", message: "open a .ips file first" });
        return;
    }
    try {
        const ipropPath = await compileEntry(entry, { debug: true });
        const disassembler = toolBinary("disassemblerPath", "ipdis");
        if (!disassembler) {
            if (inspectorPanel) inspectorPanel.webview.postMessage({ type: "error", message: "iprop: disassembler not found" });
            return;
        }
        const { execFileSync } = require("child_process");
        const text = execFileSync(disassembler, [ipropPath, "--source"], { encoding: "utf8" });
        if (inspectorPanel) {
            inspectorPanel.webview.postMessage({ type: "disassembly", text, note: entry });
        }
    } catch (message) {
        if (inspectorPanel) inspectorPanel.webview.postMessage({ type: "error", message: String(message) });
    }
}

function openInspector() {
    if (inspectorPanel) {
        inspectorPanel.reveal(vscode.ViewColumn.Beside);
        return;
    }
    inspectorPanel = vscode.window.createWebviewPanel(
        "iprop.inspector", "iprop bytecode", vscode.ViewColumn.Beside,
        { enableScripts: true, retainContextWhenHidden: true });
    inspectorPanel.webview.html = inspectorHtml();
    inspectorPanel.onDidDispose(() => { inspectorPanel = null; });
    inspectorPanel.webview.onDidReceiveMessage((message) => {
        if (message.type === "goto") {
            const editor = vscode.window.activeTextEditor;
            if (editor) {
                const line = Math.min(Math.max(0, message.line - 1), editor.document.lineCount - 1);
                const range = new vscode.Range(line, 0, line, 0);
                editor.selection = new vscode.Selection(range.start, range.start);
                editor.revealRange(range, vscode.TextEditorRevealType.InCenter);
            }
        }
    });
    buildInspector();
}

// ---- export and import navigation (Phase 31) -------------------------------------------

async function exportBinary() {
    const entry = activeEntry();
    if (!entry) {
        vscode.window.showWarningMessage("iprop: open a .ips file first");
        return;
    }
    try {
        const compiled = await compileEntry(entry);
        const target = await vscode.window.showSaveDialog({
            defaultUri: vscode.Uri.file(entry.replace(/\.ips$/, ".iprop")),
        });
        if (!target) return;
        fs.copyFileSync(compiled, target.fsPath);
        const validator = toolBinary("validatorPath", "ipval");
        if (validator) {
            execFile(validator, [target.fsPath], { timeout: 15000 }, (error, stdout) => {
                if (!error) {
                    vscode.window.showInformationMessage("iprop: exported " + target.fsPath + " — ipval: valid");
                } else {
                    vscode.window.showErrorMessage("iprop: " + target.fsPath + " failed validation: " + (stdout || error.message));
                }
            });
        } else {
            vscode.window.showInformationMessage("iprop: exported " + target.fsPath);
        }
    } catch (message) {
        vscode.window.showErrorMessage(String(message));
    }
}

async function exportPng() {
    const entry = activeEntry();
    if (!entry) {
        vscode.window.showWarningMessage("iprop: open a .ips file first");
        return;
    }
    try {
        const compiled = await compileEntry(entry);
        const cell = await renderCell(compiled, previewState.seed >>> 0,
            previewState.variant, previewState.archetype, previewState.uniforms);
        const target = await vscode.window.showSaveDialog({
            defaultUri: vscode.Uri.file(
                entry.replace(/\.ips$/, "-" + (previewState.seed >>> 0) + ".png")),
        });
        if (!target) return;
        fs.writeFileSync(target.fsPath, Buffer.from(cell.png, "base64"));
        vscode.window.showInformationMessage("iprop: exported " + target.fsPath);
    } catch (message) {
        vscode.window.showErrorMessage(String(message));
    }
}

async function exportSheet() {
    const entry = activeEntry();
    if (!entry) {
        vscode.window.showWarningMessage("iprop: open a .ips file first");
        return;
    }
    try {
        const compiled = await compileEntry(entry);
        const probe = JSON.parse((await renderCell(compiled, 0, 0, 0, {})).json);
        const seedsPick = await vscode.window.showQuickPick(["4", "8", "12"], {
            placeHolder: "seeds per variant (the grid columns)",
        });
        if (!seedsPick) return;
        const seeds = parseInt(seedsPick, 10);
        const target = await vscode.window.showSaveDialog({
            defaultUri: vscode.Uri.file(entry.replace(/\.ips$/, "-sheet.png")),
        });
        if (!target) return;
        const tool = toolBinary("sheetPath", "ipsheet");
        if (!tool) {
            vscode.window.showErrorMessage("iprop: ipsheet not found — set iprop.sheetPath");
            return;
        }
        execFile(tool, [compiled, "--variants", String(probe.variants),
            "--seeds", String(seeds), "--columns", String(seeds), "-o", target.fsPath],
            { timeout: 30000 }, (error, _stdout, stderr) => {
                if (error) {
                    vscode.window.showErrorMessage("iprop: sheet export failed: " + (stderr || error.message));
                } else {
                    vscode.window.showInformationMessage("iprop: exported " + target.fsPath);
                }
            });
    } catch (message) {
        vscode.window.showErrorMessage(String(message));
    }
}

// Lists the imports of the active file and opens the picked one.
async function openImport() {
    const editor = vscode.window.activeTextEditor;
    if (!editor || !editor.document.fileName.endsWith(".ips")) {
        vscode.window.showWarningMessage("iprop: open a .ips file first");
        return;
    }
    const imports = [...editor.document.getText().matchAll(/^import\s+(\w+)\s+from\s+"([^"]+)"/gm)]
        .map((m) => ({ alias: m[1], path: m[2] }));
    if (imports.length === 0) {
        vscode.window.showInformationMessage("iprop: this file imports nothing");
        return;
    }
    const picked = await vscode.window.showQuickPick(
        imports.map((imp) => ({ label: imp.alias, description: imp.path })), {
            placeHolder: "open an imported part",
        });
    if (!picked) return;
    const uri = vscode.Uri.file(path.resolve(path.dirname(editor.document.fileName), picked.description));
    try {
        const document = await vscode.workspace.openTextDocument(uri);
        await vscode.window.showTextDocument(document);
    } catch {
        vscode.window.showErrorMessage("iprop: cannot open import '" + picked.description + "' — is the part file present?");
    }
}

// ---- activation ---------------------------------------------------------------------

// A clickable "preview" lens above every `asset <name> {` declaration.
class IpsCodeLensProvider {
    provideCodeLenses(document) {
        const lenses = [];
        for (let i = 0; i < document.lineCount; ++i) {
            const match = document.lineAt(i).text.match(/^\s*asset\s+(\w+)\s*\{/);
            if (match) {
                lenses.push(new vscode.CodeLens(new vscode.Range(i, 0, i, 0), {
                    title: "$(play) Preview " + match[1],
                    command: "iprop.preview",
                }));
            }
        }
        return lenses;
    }
}

function activate(context) {
    log = vscode.window.createOutputChannel("iprop");
    context.subscriptions.push(log);
    diagnostics = vscode.languages.createDiagnosticCollection("iprop");
    context.subscriptions.push(diagnostics);

    context.subscriptions.push(
        vscode.commands.registerCommand("iprop.compile", compileActive));
    context.subscriptions.push(
        vscode.commands.registerCommand("iprop.applyCompilerFix", applyCompilerFix));
    context.subscriptions.push(
        vscode.commands.registerCommand("iprop.showCompilerSuggestion", suggestion =>
            vscode.window.showInformationMessage(suggestion)));
    context.subscriptions.push(
        vscode.languages.registerCodeActionsProvider({ language: "ips" }, new IpsCodeActionProvider(),
            { providedCodeActionKinds: [vscode.CodeActionKind.QuickFix] }));
    context.subscriptions.push(
        vscode.commands.registerCommand("iprop.preview", openPreview));
    context.subscriptions.push(
        vscode.languages.registerCodeLensProvider({ language: "ips" },
            new IpsCodeLensProvider()));
    context.subscriptions.push(
        vscode.commands.registerCommand("iprop.atlas", openAtlas));
    context.subscriptions.push(
        vscode.commands.registerCommand("iprop.inspector", openInspector));
    context.subscriptions.push(
        vscode.commands.registerCommand("iprop.exportBinary", exportBinary));
    context.subscriptions.push(
        vscode.commands.registerCommand("iprop.exportPng", exportPng));
    context.subscriptions.push(
        vscode.commands.registerCommand("iprop.exportSheet", exportSheet));
    context.subscriptions.push(
        vscode.commands.registerCommand("iprop.openImport", openImport));
    context.subscriptions.push(
        vscode.window.onDidChangeTextEditorSelection((e) => {
            if (inspectorPanel && e.textEditor.document.fileName.endsWith(".ips")) {
                inspectorPanel.webview.postMessage({
                    type: "cursor",
                    line: e.selections[0].active.line + 1,
                });
            }
        }));
    context.subscriptions.push(
        vscode.commands.registerCommand("iprop.reloadPreview", () => {
            log.appendLine("manual reload requested");
            if (previewPanel) compileAndRender();
            else openPreview();
        }));

    let timer = null;
    const schedule = () => {
        invalidateDiagnostics();
        clearTimeout(timer);
        timer = setTimeout(() => {
            compileActive();
            if (previewPanel) compileAndRender();
            if (inspectorPanel) buildInspector();
            if (atlasPanel) buildAtlas();
        }, 500);
    };
    context.subscriptions.push(vscode.workspace.onDidChangeTextDocument((e) => {
        if (e.document.fileName.endsWith(".ips")) schedule();
    }));
    context.subscriptions.push(
        vscode.window.onDidChangeActiveTextEditor(editor => {
            // A Problems quick fix may open its owning import. This internal
            // activation must not cancel the same version/hash-guarded edit.
            if (editor?.document !== fixOpeningDocument) schedule();
        }));
    context.subscriptions.push(vscode.workspace.onDidSaveTextDocument(document => {
        if (document.languageId === "ips") schedule();
    }));
    context.subscriptions.push(vscode.workspace.onDidCloseTextDocument(document => {
        if (document.languageId === "ips") schedule();
    }));
    context.subscriptions.push({ dispose() { clearTimeout(timer); deactivate(); } });

    // External edits (CLI agents writing files on disk) also recompile.
    const watcher = vscode.workspace.createFileSystemWatcher("**/*.ips");
    context.subscriptions.push(watcher);
    watcher.onDidChange(schedule);
    watcher.onDidCreate(schedule);
    watcher.onDidDelete(schedule);

    if (vscode.window.activeTextEditor?.document.languageId === "ips") {
        compileActive();
    }
}

function deactivate() {
    ++diagnosticGeneration;
    diagnosticRequest?.abort();
    diagnosticRequest = null;
}

module.exports = { activate, deactivate };
