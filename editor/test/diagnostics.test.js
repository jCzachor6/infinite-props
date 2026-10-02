"use strict";
const { test } = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { spawnSync } = require("node:child_process");
const { parseDiagnostics, utf16Column } = require("../diagnostics");

test("parses canonical and legacy diagnostics with paths containing colons", () => {
    const parsed = parseDiagnostics("ipc: C:\\a b\\t.ips:12:3: error: bad\r\nsource\r\n  ^\r\n" +
        "ipc: part.ips:\n2:8: error: other\n", "entry.ips");
    assert.deepEqual(parsed.get("C:\\a b\\t.ips"), [{ line:12,column:3,message:"bad" }]);
    assert.deepEqual(parsed.get("part.ips"), [{ line:2,column:8,message:"other" }]);
});

test("maps codepoint columns to VS Code UTF-16 positions", () => {
    assert.equal(utf16Column("a📦ąx", 1), 0);
    assert.equal(utf16Column("a📦ąx", 3), 3);
    assert.equal(utf16Column("a📦ąx", 5), 5);
    assert.equal(utf16Column("a📦ąx", 100), 5);
    assert.equal(utf16Column("a\tb", 3), 2);
});

test("real compiler errors retain source ownership through imports and lowering", () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "iprop-diagnostics-"));
    const ipc = process.env.IPROP_COMPILER || path.resolve(__dirname, "../../build/tools/ipc");
    const asset = "asset t { size 8x8 draw { } }";
    const run = (files, expectedFile, message) => {
        for (const [name, source] of Object.entries(files)) fs.writeFileSync(path.join(dir,name),source);
        const result = spawnSync(ipc,["entry.ips"],{cwd:dir,encoding:"utf8"});
        assert.equal(result.status,1,result.stderr);
        const errors = parseDiagnostics(result.stderr,"entry.ips");
        assert.equal(errors.size,1,result.stderr);
        const [error] = errors.get(expectedFile) || [];
        assert.ok(error,result.stderr);
        assert.ok(error.line > 0 && error.column > 0);
        assert.match(error.message,message);
        assert.ok(result.stderr.includes("^"),result.stderr);
        return error;
    };
    try {
        run({"entry.ips":'import tile from "tile.ips"\nasset t { size 8x8 draw {\n' +
            'tile.draw(1,2,3,at={0,0},rot=45)\n} }', "tile.ips":asset},"entry.ips",/rot transform/);
        run({"entry.ips":'import tile from "tile.ips"\n'+asset, "tile.ips":"asset bad {\n @ }"},
            "tile.ips",/unexpected/);
        run({"tile.ips":'import missing from "missing.ips"\n'+asset},"tile.ips",/cannot load/);
        run({"tile.ips":'import back from "entry.ips"\n'+asset},"tile.ips",/import cycle/);
        // Nonconstant origin is diagnosed by the assembly stage.
        run({"tile.ips":"asset t { size 8x8 origin unit(),0 draw { } }"},
            "tile.ips",/origin must be a constant/);
        const unicode = "/* 📦ż */ asset t { size 8x8 draw { smooh(0.5) } }";
        const error = run({"entry.ips":unicode}, "entry.ips", /did you mean 'smooth'/);
        const offset = unicode.indexOf("smooh");
        assert.equal(error.column, Array.from(unicode.slice(0,offset)).length + 1);
        assert.equal(utf16Column(unicode,error.column), offset);
    } finally { fs.rmSync(dir,{recursive:true,force:true}); }
});
