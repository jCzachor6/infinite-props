"use strict";

const { createHash } = require("node:crypto");
const { execFile } = require("node:child_process");
const path = require("node:path");

// Compiler columns count Unicode codepoints; conversion belongs at the editor
// boundary. The JSON parser and saved fixes keep the original UTF-8 positions.
function utf16Column(line, column) {
    let remaining = Math.max(0, column - 1);
    let offset = 0;
    for (const character of line) {
        if (remaining-- <= 0) break;
        offset += character.length;
    }
    return offset;
}

const { readDiagnostics } = require("./contracts");

function parseJsonDiagnostics(stdout, exitCode) {
    let result;
    try { result = JSON.parse(stdout); }
    catch { throw new Error("compiler returned invalid diagnostic JSON"); }
    return readDiagnostics(result, exitCode);
}

function sourceContentId(source) {
    return "sha256:" + createHash("sha256").update(source).digest("hex");
}

function displayPosition(prefix) {
    // VS Code removes a leading BOM and represents every physical newline as
    // one model newline, including mixed CRLF/LF and standalone CR files.
    const lines = prefix.replace(/^\ufeff/, "").replace(/\r\n|\r/g, "\n").split("\n");
    return { line: lines.length - 1, character: lines.at(-1).length };
}

function editorPosition(source, point) {
    const bytes = Buffer.from(source, "utf8");
    if (point.byte_offset > bytes.length || (point.byte_offset < bytes.length &&
        (bytes[point.byte_offset] & 0xc0) === 0x80)) return null;
    const prefix = bytes.subarray(0, point.byte_offset).toString("utf8");
    const lines = prefix.split("\n");
    const last = lines[lines.length - 1];
    if (point.line !== lines.length || point.column !== Array.from(last).length + 1) return null;
    // A position in the middle of CRLF cannot be represented by a VS Code
    // TextDocument position and must never be used for a source edit.
    if (prefix.endsWith("\r") && bytes[point.byte_offset] === 0x0a) return null;
    return displayPosition(prefix);
}

function editorRange(source, range, location = null) {
    if (range) {
        const start = editorPosition(source, range.start);
        const end = editorPosition(source, range.end);
        return start && end ? { start, end } : null;
    }
    const line = location ? location.line - 1 : 0;
    const lines = source.split("\n");
    if (line < 0 || line >= lines.length) return null;
    const character = utf16Column(lines[line], location ? location.column : 1);
    const start = displayPosition(lines.slice(0, line).map(text => text + "\n").join("") + lines[line].slice(0, character));
    return { start, end: start };
}

// All fixes from one diagnostic form one transaction. Reject changed source,
// inconsistent UTF-8 positions and overlapping replacements instead of guessing.
function fixEdits(source, diagnostic) {
    if (!diagnostic.fix_its.length || !diagnostic.source_id || sourceContentId(source) !== diagnostic.source_id) return null;
    const edits = [];
    for (const fix of diagnostic.fix_its) {
        if (fix.source_id !== diagnostic.source_id) return null;
        // The editor's text model omits the BOM. Text edits cannot change its
        // encoding metadata, so a fix touching those bytes is unsupported.
        if (source.startsWith("\ufeff") && fix.range.start.byte_offset < 3) return null;
        const range = editorRange(source, fix.range);
        if (!range) return null;
        edits.push({ range, replacement: fix.replacement,
            start: fix.range.start.byte_offset, end: fix.range.end.byte_offset });
    }
    edits.sort((a, b) => a.start - b.start || a.end - b.end);
    for (let i = 1; i < edits.length; ++i) {
        if (edits[i].start < edits[i - 1].end || edits[i].start === edits[i - 1].start) return null;
    }
    return edits;
}

async function runCompilerDiagnostics(binary, entry, { signal, execute = execFile, timeout = 15000 } = {}) {
    const cwd = path.dirname(entry);
    const run = args => new Promise((resolve, reject) => {
        if (signal?.aborted) { reject(new Error("diagnostic request cancelled")); return; }
        execute(binary, args, { cwd, timeout, signal, encoding: "utf8", maxBuffer: 4 * 1024 * 1024 }, (error, stdout, stderr) => {
            if (signal?.aborted || (error && (!Number.isInteger(error.code) || error.killed || error.signal))) {
                reject(error || new Error("diagnostic request cancelled"));
            } else resolve({ exitCode: error ? error.code : 0, stdout, stderr });
        });
    });
    const result = await run([entry, "--check", "--diagnostics=json"]);
    // A compiler without JSON diagnostics (such as 0.4) explains itself on stderr.
    if (!result.stdout.trim()) throw new Error(result.stderr.trim() || "compiler returned no diagnostic JSON");
    return parseJsonDiagnostics(result.stdout, result.exitCode);
}

module.exports = { parseJsonDiagnostics, utf16Column, editorRange,
    sourceContentId, fixEdits, runCompilerDiagnostics };
