import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import test from "node:test";
import { renderBook } from "../scripts/build-docs.mjs";

const headings = [{ title: "Book", anchor: "book", level: 1 }, { title: "Calls", anchor: "calls", level: 2 }];

test("rendered Markdown preserves tables, escapes raw HTML and generates navigation", () => {
    const value = renderBook("# Book\n\n<script>alert(1)</script>\n\n## Calls\n\n| Type | Value |\n| --- | --- |\n| f32 | 1.0 |\n\n```ips\n#fff\n```\n", headings, os.tmpdir());
    assert.ok(value.html.includes('id="calls"'));
    assert.ok(value.html.includes("<table>"));
    assert.ok(value.html.includes("&lt;script&gt;"));
    assert.ok(!value.html.includes("<script>"));
    assert.deepEqual(value.search, [{ t: "Language — Calls", p: "language.html", a: "calls" }]);
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
