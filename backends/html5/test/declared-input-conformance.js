"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const {execFileSync, spawnSync} = require("node:child_process");

function run(iprop, tool, directory) {
    const source=path.join(directory,"declared-inputs.ips"),binary=path.join(directory,"declared-inputs.iprop");
    fs.writeFileSync(source,"asset t{size 1x1 input i32 v=3 input f32 a=4 output point p draw{variants=v;archetypes=a;p={variants,archetypes}}}");
    execFileSync(tool("ipc"),[source,"--no-compress","-o",binary]);
    const bytes=new Uint8Array(fs.readFileSync(binary));
    assert.equal(Buffer.from(bytes).readUInt16LE(6),7);
    assert.deepEqual(iprop.generate(bytes).outputs[0].points,[{x:3,y:4}]);
    assert.deepEqual(iprop.generate(bytes,{uniforms:{v:7,a:8}}).outputs[0].points,[{x:7,y:8}]);
    for(const name of ["v","a","variant","archetype","variants","archetypes"])
        assert.throws(()=>iprop.generate(bytes,{[name]:0}),/unknown.*option/i);
    for(const flag of ["--variant","--archetype"]){
        const result=spawnSync(tool("ipgen"),[binary,flag,"0"],{encoding:"utf8"});
        assert.equal(result.status,1);assert.match(result.stderr,/unknown option/);
    }
    for(const minor of [0,1,2,3,4,5,6,8]){
        const bad=Buffer.from(bytes);bad.writeUInt16LE(minor,6);
        assert.throws(()=>iprop.generate(new Uint8Array(bad)),/unsupported format minor version/);
    }
    return {authoredInputs:2,retiredOptions:6,rejectedVersions:8};
}
module.exports={run};
