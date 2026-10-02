"use strict";

// Canonical CLI form: ipc: file:N:M: error: text. Also accept the earlier
// two-line form; snippets and carets are deliberately ignored.
function parseDiagnostics(stderr, entry) {
    const byFile = new Map();
    let currentFile = entry;
    for (const line of stderr.split(/\r?\n/)) {
        const complete = line.match(/^ipc: (.+):(\d+):(\d+): error: (.*)$/);
        const header = line.match(/^ipc: (.+):$/);
        if (header) {
            currentFile = header[1];
            continue;
        }
        const located = complete ? complete.slice(2) : line.match(/^(\d+):(\d+): error: (.*)$/)?.slice(1);
        if (!located) continue;
        const file = complete ? complete[1] : currentFile;
        const list = byFile.get(file) ?? [];
        list.push({ line: Number(located[0]), column: Number(located[1]), message: located[2] });
        byFile.set(file, list);
    }
    return byFile;
}

module.exports = { parseDiagnostics };
