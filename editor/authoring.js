"use strict";
const { execFile } = require("child_process");
const { compilerIdentity } = require("./formatting");

// What a hover and a completion say about a name that only an asset with render has.
const RENDER_ONLY = "// in an asset with render";

// Signatures and namespace constants come from the selected compiler, never a second catalogue.
function entries(catalogue) {
    if (catalogue.schema_version !== 8) throw new Error("unsupported language catalogue");
    const signature = (name, o, variadic) =>
        `${name}(${o.parameters.map(p => `${p.name}: ${p.type}`).join(", ")}${variadic ? ", ..." : ""}) -> ${o.result}`;
    // Models (a preview): the builtins of render assets, and the overloads
    // such an asset adds to builtins of the 2D table.
    const models = catalogue.source_rules.models;
    const extended = new Map(models.extended_builtins.map(b => [b.name, b.overloads]));
    const result = catalogue.builtins.map(b => ({ name: b.name, signatures: [
        ...b.overloads.map(o => signature(b.name, o)),
        ...(b.variadic ? [signature(b.name, b.variadic, true)] : []),
        ...(extended.get(b.name) || []).map(o => signature(b.name, o) + "  " + RENDER_ONLY)] }));
    models.builtins.forEach(b => result.push({ name: b.name, note: RENDER_ONLY,
        signatures: b.overloads.map(o => signature(b.name, o)) }));
    const constants = new Set(catalogue.source_rules.surfaces?.alpha_modes || []);
    function visit(value) {
        if (!value || typeof value !== "object") return;
        for (const [key, item] of Object.entries(value)) {
            if (key === "constants") Object.keys(item).filter(k => k.includes(".")).forEach(k => constants.add(k));
            else visit(item);
        }
    }
    visit(catalogue.source_rules);
    constants.forEach(name => result.push({ name, signatures: [], kind: "Constant" }));
    catalogue.source_rules.types.storable.forEach(name => result.push({ name, signatures: [], kind: "TypeParameter" }));
    models.types.forEach(type => result.push({ name: type.name, note: RENDER_ONLY, signatures: [], kind: "TypeParameter" }));
    return result;
}

// The lines a completion or a hover shows for an entry.
function describe(entry) {
    return [...(entry.note ? [entry.note] : []), ...entry.signatures].join("\n");
}

function registerAuthoring(vscode, context, compiler) {
    let cached;
    async function catalogue(document) {
        const cwd = require("path").dirname(document.uri.fsPath);
        const identity = compilerIdentity(compiler(), cwd);
        if (cached?.key !== identity.sourceId) {
            const value = new Promise((resolve, reject) => execFile(identity.executable, ["--describe-language"],
                { cwd, encoding: "utf8", timeout: 15000, maxBuffer: 4 * 1024 * 1024 },
                (error, stdout) => { try { if (error) throw error; resolve(entries(JSON.parse(stdout))); } catch (e) { reject(e); } }));
            cached = { key: identity.sourceId, value };
            value.catch(() => { if (cached?.value === value) cached = undefined; });
        }
        return cached.value;
    }
    const selector = { language: "ips", scheme: "file" }, word = /[A-Za-z_][\w.]*/;
    context.subscriptions.push(vscode.languages.registerCompletionItemProvider(selector, {
        async provideCompletionItems(document, position, token) {
            try {
                const items = await catalogue(document);
                if (token.isCancellationRequested) return [];
                return items.map(entry => {
                    const item = new vscode.CompletionItem(entry.name, vscode.CompletionItemKind[entry.kind || "Function"]);
                    item.detail = describe(entry);
                    item.range = document.getWordRangeAtPosition(position, word);
                    return item;
                });
            } catch { return []; }
        }
    }, "."), vscode.languages.registerHoverProvider(selector, {
        async provideHover(document, position, token) {
            try {
                const range = document.getWordRangeAtPosition(position, word);
                if (!range) return;
                const item = (await catalogue(document)).find(item => item.name === document.getText(range));
                if (!item || token.isCancellationRequested) return;
                const text = new vscode.MarkdownString();
                text.appendCodeblock(describe(item) || item.name, "ips");
                return new vscode.Hover(text, range);
            } catch { return; }
        }
    }));
}
module.exports = { entries, describe, registerAuthoring };
