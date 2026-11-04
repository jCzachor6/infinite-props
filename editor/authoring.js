"use strict";
const { execFile } = require("child_process");
const { compilerIdentity } = require("./formatting");

// Signatures and namespace constants come from the selected compiler, never a second catalogue.
function entries(catalogue) {
    if (catalogue.schema_version !== 6) throw new Error("unsupported language catalogue");
    const result = catalogue.builtins.map(b => ({ name: b.name, signatures: [...b.overloads, ...(b.variadic ? [b.variadic] : [])].map(o =>
        `${b.name}(${o.parameters.map(p => `${p.name}: ${p.type}`).join(", ")}${o === b.variadic ? ", ..." : ""}) -> ${o.result}`) }));
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
    return result;
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
                    item.detail = entry.signatures.join("\n");
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
                text.appendCodeblock(item.signatures.join("\n") || item.name, "ips");
                return new vscode.Hover(text, range);
            } catch { return; }
        }
    }));
}
module.exports = { entries, registerAuthoring };
