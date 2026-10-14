import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import test from "node:test";
import { renderBook, syncDocumentation } from "../scripts/build-docs.mjs";

const headings = [{ title: "Book", anchor: "book", level: 1 }, { title: "Calls", anchor: "calls", level: 2 }];

test("documentation sync retires stale payloads and preserves authored navigation", () => {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), "iprop-doc-sync-"));
    const bundle = path.join(directory, "bundle"), publicDir = path.join(directory, "public");
    const write = (base, file, content) => {
        const target = path.join(base, file);
        fs.mkdirSync(path.dirname(target), { recursive: true });
        fs.writeFileSync(target, content);
    };
    try {
        for (const file of ["docs/language-v0.7.md", "docs/language/migration-v0.7.md", "spec/iprop-v0.7.md", "examples/language/new.ips", "LLM_REFERENCE.md"])
            write(bundle, file, "current");
        for (const file of ["docs/validation-v0.7-stage1.md", "docs/language-v0.6.md", "docs/language/migration-v0.6.md", "spec/iprop-v0.6.md", "reference/optimizations-v0.4.md", "examples/language/old.ips"])
            write(publicDir, file, "stale");
        write(publicDir, "docs/nav.js", "authored navigation");
        write(publicDir, "favicon.svg", "authored icon");
        for (let i = 0; i < 2; i++) {
            syncDocumentation(bundle, publicDir);
            assert.equal(fs.readFileSync(path.join(publicDir, "docs/nav.js"), "utf8"), "authored navigation");
            assert.equal(fs.readFileSync(path.join(publicDir, "favicon.svg"), "utf8"), "authored icon");
            assert.deepEqual(fs.readdirSync(path.join(publicDir, "spec")), ["iprop-v0.7.md"]);
            assert.ok(!fs.existsSync(path.join(publicDir, "reference")));
            assert.deepEqual(fs.readdirSync(path.join(publicDir, "docs/language")), ["migration-v0.7.md"]);
            assert.ok(!fs.existsSync(path.join(publicDir, "docs/validation-v0.7-stage1.md")));
            assert.ok(!fs.existsSync(path.join(publicDir, "docs/language-v0.6.md")));
            assert.deepEqual(fs.readdirSync(path.join(publicDir, "examples/language")), ["new.ips"]);
        }
    } finally { fs.rmSync(directory, { recursive: true, force: true }); }
});

test("rendered Markdown preserves tables, escapes raw HTML and generates navigation", () => {
    const value = renderBook("# Book\n\n<script>alert(1)</script>\n\n## Calls\n\n| Type | Value |\n| --- | --- |\n| f32 | 1.0 |\n\n```ips\n#fff\n```\n", headings, os.tmpdir());
    assert.ok(value.html.includes('id="calls"'));
    assert.ok(value.html.includes("<table>"));
    assert.ok(value.html.includes("&lt;script&gt;"));
    assert.ok(!value.html.includes("<script>"));
    assert.deepEqual(value.search, [{ t: "Language: Calls", p: "language.html", a: "calls" }]);
});

test("missing in-page anchors cannot enter the website", () => {
    assert.throws(() => renderBook("# Book\n\n[bad](#absent)\n\n## Calls\n", headings, os.tmpdir()), /Missing handbook anchor/);
});

test("missing and escaping local links cannot enter the website", () => {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), "iprop-doc-links-"));
    try {
        fs.mkdirSync(path.join(directory, "docs"));
        assert.throws(() => renderBook("# Book\n\n[bad](absent.md)\n\n## Calls\n", headings, directory), /handbook link/);
        assert.throws(() => renderBook("# Book\n\n[bad](../../outside.md)\n\n## Calls\n", headings, directory), /handbook link/);
    } finally { fs.rmSync(directory, { recursive: true, force: true }); }
});

test("headings and search anchors must match the compiler-backed book manifest", () => {
    assert.throws(() => renderBook("# Book\n\n## Changed\n", headings, os.tmpdir()), /headings differ/);
    assert.throws(() => renderBook("# Book\n", headings, os.tmpdir()), /headings are incomplete/);
});

test("website rejects a rehashed catalog missing current protocol fields before rendering", async () => {
    const { verifyBundle } = await import("../scripts/build-docs.mjs");
    const { createHash } = await import("node:crypto");
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), "iprop-doc-contract-"));
    const hash = data => createHash("sha256").update(data).digest("hex");
    try {
        const payload = {
            "docs/language.json": JSON.stringify({ schema_version: 3, product_version: "0.4.0",
                builtins: [{ name: "unit", overloads: [] }] }),
            "docs/language-v0.4.md": "# Book\n",
            "docs/language/index.md": "# Book\n",
            "examples/language/cases.json": '{"cases":[{"file":"one.ips","expect":"pass"}]}'
        };
        for (const [name, data] of Object.entries(payload)) {
            const target = path.join(directory, name);
            fs.mkdirSync(path.dirname(target), { recursive: true });
            fs.writeFileSync(target, data);
        }
        const info = { schema_version: 2, product_version: "0.4.0", source_commit: null,
            language_version: "0.5", format_version: "0.4", c_api_version: "0.3", c_abi_soversion: "0.4",
            working_tree_dirty: false, status: "development / release candidate", tag: null,
            builtin_count: 1, example_count: 1, headings: [{ title: "Book", anchor: "book", level: 1 }],
            source_files: { "docs/language/index.md": hash(payload["docs/language/index.md"]) },
            files: Object.fromEntries(Object.entries(payload).map(([name, data]) => [name, hash(data)])) };
        fs.writeFileSync(path.join(directory, "docs/language-manifest.json"), JSON.stringify(info));
        assert.throws(() => verifyBundle(directory), /language required fields/);
        info.schema_version = 3;
        fs.writeFileSync(path.join(directory, "docs/language-manifest.json"), JSON.stringify(info));
        assert.throws(() => verifyBundle(directory), /manifest schema version/);
        assert.ok(!fs.existsSync(path.join(directory, "language.html")));
    } finally { fs.rmSync(directory, { recursive: true, force: true }); }
});
