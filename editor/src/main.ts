// The studio renderer: the project file list, the tabbed CodeMirror editor
// with the .ips syntax and compiler diagnostics, and the embedded terminal
// for CLI agents.

import { EditorView, keymap, lineNumbers, highlightActiveLine } from "@codemirror/view";
import { EditorState } from "@codemirror/state";
import { defaultKeymap, history, historyKeymap, indentWithTab } from "@codemirror/commands";
import { linter, lintGutter, Diagnostic, setDiagnostics } from "@codemirror/lint";
import { syntaxHighlighting, defaultHighlightStyle } from "@codemirror/language";
import { oneDark } from "@codemirror/theme-one-dark";
import { ipsLanguage } from "./language";
import { Terminal } from "xterm";
import "xterm/css/xterm.css";

// ---- the bridge surface ---------------------------------------------------

declare global {
    interface Window {
        studio: {
            openProject(): Promise<{ dir: string; files: string[] } | null>;
            listProject(): Promise<{ dir: string | null; files: string[] }>;
            readFile(name: string): Promise<string>;
            writeFile(name: string, content: string): Promise<boolean>;
            newFile(name: string): Promise<string[]>;
            deleteFile(name: string): Promise<string[]>;
            compile(entry: string): Promise<{ code: number; stdout: string; stderr: string }>;
            startTerminal(cwd: string | null): Promise<boolean>;
            stopTerminal(): Promise<boolean>;
            sendTerminalData(data: string): void;
            resizeTerminal(cols: number, rows: number): void;
            onTerminalData(callback: (data: string) => void): void;
            onTerminalExit(callback: () => void): void;
        };
    }
}

const $ = (id: string) => document.getElementById(id) as HTMLElement;

// ---- app state ---------------------------------------------------------------

interface OpenFile {
    name: string;
    content: string; // the disk state
    dirty: boolean;
    diagnostics: Diagnostic[];
}

const state = {
    dir: null as string | null,
    files: [] as string[],
    open: new Map<string, OpenFile>(),
    active: null as string | null,
    terminalStarted: false,
};

const editor = new EditorView({
    parent: $("editor"),
    state: EditorState.create({
        doc: "",
        extensions: [
            lineNumbers(),
            highlightActiveLine(),
            history(),
            ipsLanguage,
            syntaxHighlighting(defaultHighlightStyle, { fallback: true }),
            oneDark,
            lintGutter(),
            linter(() => []),
            keymap.of([...defaultKeymap, ...historyKeymap, indentWithTab]),
            EditorView.updateListener.of((update) => {
                if (update.docChanged && state.active) {
                    const file = state.open.get(state.active)!;
                    file.dirty = true;
                    renderTabs();
                    scheduleCompile();
                }
            }),
        ],
    }),
});

// ---- rendering ------------------------------------------------------------------

function renderFiles() {
    const node = $("files");
    node.textContent = "";
    for (const name of state.files) {
        const row = document.createElement("div");
        row.className = "file" + (name === state.active ? " active" : "");
        row.textContent = name;
        row.addEventListener("click", () => openFile(name));
        node.appendChild(row);
    }
}

function renderTabs() {
    const node = $("tabs");
    node.textContent = "";
    for (const name of state.open.keys()) {
        const file = state.open.get(name)!;
        const tab = document.createElement("div");
        tab.className = "tab" + (name === state.active ? " active" : "");
        tab.textContent = name + (file.dirty ? " ●" : "");
        tab.addEventListener("click", () => openFile(name));
        node.appendChild(tab);
    }
}

function setStatus(text: string, isError = false) {
    const node = $("status");
    node.textContent = text;
    node.className = isError ? "error" : "";
}

// ---- files -------------------------------------------------------------------------

async function openFile(name: string) {
    if (state.active && state.open.get(state.active)!.dirty) {
        await saveFile(state.active);
    }
    if (!state.open.has(name)) {
        const content = await window.studio.readFile(name);
        state.open.set(name, { name, content, dirty: false, diagnostics: [] });
    }
    state.active = name;
    const file = state.open.get(name)!;
    editor.dispatch({
        changes: { from: 0, to: editor.state.doc.length, insert: file.content },
    });
    editor.dispatch(setDiagnostics(editor.state, file.diagnostics));
    renderFiles();
    renderTabs();
    $("empty").style.display = "none";
    scheduleCompile();
}

async function saveFile(name: string) {
    const file = state.open.get(name)!;
    if (!file.dirty) return;
    await window.studio.writeFile(name, editor.state.doc.toString());
    file.content = editor.state.doc.toString();
    file.dirty = false;
    renderTabs();
}

async function openProject() {
    const project = await window.studio.openProject();
    if (!project) return;
    state.dir = project.dir;
    state.files = project.files;
    state.open.clear();
    state.active = null;
    $("dir").textContent = project.dir;
    renderFiles();
    renderTabs();
    if (project.files.length > 0) {
        await openFile(project.files[0]);
    } else {
        $("empty").style.display = "flex";
    }
}

// ---- diagnostics ---------------------------------------------------------------------

// The native ipc prints "ipc: <file>:" followed by "N:M: error: <message>"
// lines; this parses them into per-file CodeMirror diagnostics.
function parseDiagnostics(stderr: string, entry: string) {
    const byFile = new Map<string, Diagnostic[]>();
    let currentFile = entry;
    for (const line of stderr.split("\n")) {
        const fileMatch = line.match(/^ipc: (.+):$/);
        if (fileMatch) {
            currentFile = fileMatch[1];
            continue;
        }
        const errorMatch = line.match(/^(\d+):(\d+): error: (.*)$/);
        if (errorMatch) {
            const lineNumber = parseInt(errorMatch[1], 10);
            const list = byFile.get(currentFile) ?? [];
            list.push({
                from: 0,
                to: 0,
                severity: "error",
                message: errorMatch[3],
            } as Diagnostic);
            // Resolve the offsets once the document is known; approximate
            // with a line-based marker until then.
            (list[list.length - 1] as any).lineNumber = lineNumber;
            byFile.set(currentFile, list);
        }
    }
    return byFile;
}

let compileTimer: number | null = null;
function scheduleCompile() {
    if (compileTimer !== null) window.clearTimeout(compileTimer);
    compileTimer = window.setTimeout(() => { compileNow(); }, 500);
}

async function compileNow() {
    if (!state.active || !state.dir) return;
    const entry = state.active;
    setStatus("compiling…");
    const result = await window.studio.compile(entry);
    const byFile = parseDiagnostics(result.stderr, entry);

    // The active file's diagnostics go to the editor; the others wait for
    // their tab.
    for (const [name, list] of byFile) {
        const resolved = list.map((d) => {
            const lineNumber = (d as any).lineNumber ?? 1;
            const line = editor.state.doc.line(Math.min(lineNumber, editor.state.doc.lines));
            return { from: line.from, to: line.to, severity: "error" as const, message: d.message };
        });
        const file = state.open.get(name);
        if (file) {
            file.diagnostics = resolved;
        }
        if (name === state.active) {
            editor.dispatch(setDiagnostics(editor.state, resolved));
        }
    }

    if (result.code === 0) {
        setStatus("✓ compiles");
    } else {
        const count = [...byFile.values()].reduce((n, list) => n + list.length, 0);
        setStatus(`✗ ${count} error${count === 1 ? "" : "s"} — details in the gutter`, true);
    }
}

// ---- the terminal ----------------------------------------------------------------------

const terminal = new Terminal({ convertEol: true, fontFamily: "ui-monospace, Menlo, monospace", fontSize: 12 });
terminal.open($("terminal"));
terminal.onData((data) => window.studio.sendTerminalData(data));
window.studio.onTerminalData((data) => terminal.write(data));
window.studio.onTerminalExit(() => setStatus("terminal exited", true));

async function toggleTerminal() {
    const wrap = $("terminal");
    const hidden = wrap.classList.toggle("hidden");
    $("terminal-toggle").textContent = hidden ? "▸ terminal" : "▾ terminal";
    if (!hidden) {
        if (!state.terminalStarted) {
            await window.studio.startTerminal(state.dir);
            state.terminalStarted = true;
        }
        window.studio.resizeTerminal(terminal.cols, terminal.rows);
        terminal.focus();
    }
}

// ---- wiring ------------------------------------------------------------------------------

$("open").addEventListener("click", openProject);
$("compile").addEventListener("click", compileNow);
$("terminal-bar").addEventListener("click", toggleTerminal);

document.addEventListener("keydown", async (event) => {
    if ((event.metaKey || event.ctrlKey) && event.key === "s") {
        event.preventDefault();
        if (state.active) await saveFile(state.active);
    }
});

(async function boot() {
    const project = await window.studio.listProject();
    if (project.dir) {
        state.dir = project.dir;
        state.files = project.files;
        $("dir").textContent = project.dir;
        renderFiles();
        if (project.files.length > 0) await openFile(project.files[0]);
    }
})();
