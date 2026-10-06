"use strict";
const fs = require("fs");
const path = require("path");
const { randomBytes } = require("crypto");
const { execFile } = require("child_process");
const { sourceContentId } = require("./diagnostics");

function compilerIdentity(binary, cwd) {
    const candidates = path.isAbsolute(binary) ? [binary] : binary.includes(path.sep)
        ? [path.resolve(cwd, binary)]
        : (process.env.PATH || "").split(path.delimiter).map(dir => path.resolve(cwd, dir, binary));
    for (const candidate of candidates) {
        for (const file of process.platform === "win32" && !path.extname(candidate)
            ? [candidate, candidate + ".exe"] : [candidate]) {
            try {
                const executable = fs.realpathSync(file);
                return { executable, sourceId: sourceContentId(fs.readFileSync(executable)) };
            } catch { /* Continue the executable search. */ }
        }
    }
    throw new Error("compiler executable cannot be read: " + binary);
}

// These are reserved temporary directories created by ipc's atomic writer.
// They contain source.ips only while a source output is being published.
function isFormatStagingPath(uri) {
    return typeof uri?.fsPath === "string" && uri.fsPath.split(path.sep)
        .some(component => /^\.ipc-format-\d+-\d+$/.test(component));
}

async function runCompilerFormatting(binary, entry, { signal, execute = execFile,
    timeout = 15000, maxBytes = 4 * 1024 * 1024 } = {}) {
    if (signal?.aborted) throw new Error("format request cancelled");
    const parent = fs.realpathSync(path.dirname(entry));
    // The destination must have the entry's physical parent. A nested temp
    // directory would cause the CLI to rebase imports for that other directory.
    const output = path.join(parent, ".iprop-format-" + randomBytes(16).toString("hex") + ".tmp");
    const fd = fs.openSync(output, "wx", 0o600);
    try {
        fs.closeSync(fd);
        const result = await new Promise((resolve, reject) => {
            let child, result, ready = false, closed = false;
            const finish = () => {
                // AbortError can precede process exit. Keep ownership until the
                // child has closed so it cannot publish after our cleanup.
                if (ready && result && (!child?.once || closed)) resolve(result);
            };
            try {
                child = execute(binary, [entry, "--format-only", "--format", output],
                    { cwd: path.dirname(entry), timeout, signal, encoding: "utf8", maxBuffer: maxBytes },
                    (error, stdout, stderr) => { result = { error, stdout, stderr }; finish(); });
                if (child?.once) child.once("close", () => { closed = true; finish(); });
                ready = true;
                finish();
            } catch (error) { reject(error); }
        });
        if (signal?.aborted) throw new Error("format request cancelled");
        if (result.error) throw new Error(result.stderr?.trim() || result.error.message || "compiler formatting failed");
        const size = fs.statSync(output).size;
        if (!size) throw new Error("compiler did not publish formatted source");
        if (size > maxBytes) throw new Error("formatted source exceeds byte ceiling");
        const bytes = fs.readFileSync(output);
        const text = bytes.toString("utf8");
        if (!Buffer.from(text, "utf8").equals(bytes)) throw new Error("formatter returned invalid UTF-8");
        return text;
    } finally {
        // Only this exclusively reserved file belongs to the extension. ipc
        // owns its staging directories; never remove other processes' staging.
        fs.rmSync(output, { force: true });
    }
}

module.exports = { compilerIdentity, isFormatStagingPath, runCompilerFormatting };
