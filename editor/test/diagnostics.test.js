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

test("compiler exports canonical source with identical executable roundtrips", () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "iprop-format-"));
    const ipc = process.env.IPROP_COMPILER || path.resolve(__dirname, "../../build/tools/ipc");
    // Windows forbids filename quotes; a nested path still tests escaped imports.
    const part = process.platform === "win32" ? "parts\\tile ż.ips" : 'tile "ż".ips';
    const source = 'import tile from ' + JSON.stringify(part) + '\n' +
        'asset example { size 8x8 draw {\n' +
        '  // author comment\n  rect(color=#fff,h=4,w=4,y=0,x=0)\n' +
        '  tile.draw(seed,v,a,at={0,0})\n} }';
    const run = args => spawnSync(ipc,args,{cwd:dir,encoding:"utf8"});
    try {
        fs.mkdirSync(path.dirname(path.join(dir,part)),{recursive:true});
        fs.writeFileSync(path.join(dir,part),"asset tile { size 8x8 draw { pixel(0,0,#f00) } }");
        fs.writeFileSync(path.join(dir,"entry.ips"),source);
        let result = run(["entry.ips","-o","original.iprop","--strip","--format","canonical.ips"]);
        assert.equal(result.status,0,result.stderr);
        const formatted = fs.readFileSync(path.join(dir,"canonical.ips"),"utf8");
        assert.match(formatted,/rect\(x = 0, y = 0, w = 4, h = 4, color = #ffffff\)/);
        assert.equal(fs.readFileSync(path.join(dir,"entry.ips"),"utf8"),source);
        result = run(["canonical.ips","-o","roundtrip.iprop","--strip","--format","twice.ips"]);
        assert.equal(result.status,0,result.stderr);
        assert.deepEqual(fs.readFileSync(path.join(dir,"roundtrip.iprop")),fs.readFileSync(path.join(dir,"original.iprop")));
        assert.equal(fs.readFileSync(path.join(dir,"twice.ips"),"utf8"),formatted);
        result = run(["entry.ips","--format","entry.ips"]);
        assert.equal(result.status,1);
        assert.match(result.stderr,/distinct source output/);
        assert.equal(fs.readFileSync(path.join(dir,"entry.ips"),"utf8"),source);
        fs.linkSync(path.join(dir,"entry.ips"),path.join(dir,"source-alias.ips"));
        result = run(["entry.ips","--format","source-alias.ips"]);
        assert.equal(result.status,1);
        assert.match(result.stderr,/distinct source output/);
        assert.equal(fs.readFileSync(path.join(dir,"entry.ips"),"utf8"),source);
        const binary = fs.readFileSync(path.join(dir,"original.iprop"));
        fs.linkSync(path.join(dir,"original.iprop"),path.join(dir,"binary-alias.ips"));
        result = run(["entry.ips","-o","original.iprop","--format","binary-alias.ips"]);
        assert.equal(result.status,1);
        assert.match(result.stderr,/distinct source output/);
        assert.deepEqual(fs.readFileSync(path.join(dir,"original.iprop")),binary);
        if (fs.existsSync(path.join(dir,"ENTRY.IPS"))) {
            result = run(["entry.ips","-o","case.iprop","--format","CASE.IPROP"]);
            assert.equal(result.status,1);
            assert.match(result.stderr,/distinct source output/);
            assert.equal(fs.readFileSync(path.join(dir,"case.iprop")).subarray(0,4).toString(),"IPRP");
        }
    } finally { fs.rmSync(dir,{recursive:true,force:true}); }
});
