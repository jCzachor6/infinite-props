"use strict";
const {renderSource, renderArguments} = require("./authored-fixtures.js");
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path");
const { execFileSync, spawnSync } = require("node:child_process");
const { decodePng } = require("./png.js");
const { sections } = require("./sampler-conformance.js");
const repo = path.resolve(__dirname,"../../..");
const bits = value => { const b=Buffer.alloc(4);b.writeFloatLE(value);return b.readUInt32LE(); };
const outputBits = result => result.outputs.map(slot=>({name:slot.name,kind:["point","rect","circle"][slot.kind],list:slot.isList,values:slot.valueBits}));
function instructions(data) {
    const start=sections(data).get(3), functions=[];let at=start+4;
    for(let i=0;i<data.readUInt32LE(start);++i){
        functions.push({offset:data.readUInt32LE(at+4),length:data.readUInt32LE(at+8)});at+=12;
        for(let slot=0;slot<3;++slot){const count=data.readUInt16LE(at);at+=2;for(let n=0;n<count;++n){if(data[at++]===0x80)at+=2;}}
    }
    return functions.flatMap(fn=>Array.from({length:fn.length/4},(_,i)=>({at:at+fn.offset+i*4,op:data[at+fn.offset+i*4]})));
}

function run(iprop,tool,directory) {
    const base=path.join(directory,"resource-exports"), tree=base+"-tree";fs.mkdirSync(tree);
    let comparisons=0,pins=0,traps=0,rejections=0;
    function compile(source, flags=[]) {
        const input=base+".ips",binary=base+".iprop";fs.writeFileSync(input,source);
        execFileSync(tool("ipc"),[input,"--no-compress",...flags,"-o",binary]);return binary;
    }
    function generate(binary,seed=26,options={}) {
        const png=base+".png",flags=options.sizeX?["--size",`${options.sizeX}x${options.sizeY}`]:[];
        const native=JSON.parse(execFileSync(tool("ipgen"),[binary,"--seed",String(seed),"--float-bits",...flags,"-o",png],{encoding:"utf8"}));
        const rgba=Buffer.from(decodePng(fs.readFileSync(png)).data);
        const wasm=iprop.generate(new Uint8Array(fs.readFileSync(binary)),{seed,...options});
        assert.deepEqual(Buffer.from(wasm.data),rgba,"export native/WASM pixels");assert.deepEqual(outputBits(wasm),native.outputs,"export output bits");
        ++comparisons;return {rgba,outputs:native.outputs,fuel:native.fuel};
    }
    const oracle=JSON.parse(fs.readFileSync(path.join(repo,"tests/fixtures/resource-exports-v0.6.json"),"utf8"));
    for(const expected of oracle.cases) for(const opt of [false,true]) {
        for(const [name,source] of Object.entries(expected.source_texts)) fs.writeFileSync(path.join(tree,name),renderSource(source));
        const binary=path.join(tree,"tree.iprop");execFileSync(tool("ipc"),[path.join(tree,"root.ips"),"--no-compress",...(opt?[]:["--no-opt"]),"-o",binary]);
        for(const generation of expected.generations) for(const seed of [0,1,7,26,0xffffffff]) {
            const result=generate(binary,seed,renderArguments(generation.arguments));
            assert.deepEqual(result.rgba,Buffer.from(generation.rgba_base64,"base64"),expected.name);
            assert.deepEqual(result.outputs,[{name:"state",kind:"point",list:false,values:[{x:bits(seed&65535),y:bits(seed>>>16)}]}]);++pins;
        }
        const assembly=base+".ipa",restored=base+"-restored.iprop";fs.writeFileSync(assembly,execFileSync(tool("ipdis"),[binary]));
        execFileSync(tool("ipa"),[assembly,"-o",restored]);assert.deepEqual(generate(restored),generate(binary),"exports IPA/IPDIS");
    }
    // The complete compiler matrix also checks a mixed geometry/resource table.
    for(const opt of [false,true]) for(const debug of [false,true]) for(const compressed of [false,true]) {
        const binary=base+"-example.iprop";
        execFileSync(tool("ipc"),[path.join(repo,"examples/language/resource-exports.ips"),...(opt?[]:["--no-opt"]),debug?"--debug":"--strip",compressed?"--compress":"--no-compress","-o",binary]);
        const result=generate(binary);assert.equal(result.outputs.length,4);assert.equal(result.outputs[0].values[0].x,bits(8));assert.equal(result.outputs[0].values[0].y,bits(6));++pins;
    }
    const leaf='asset t{size 2x1 output layer image output mask region draw{if(seed!=3){layer captured{pixel(0,0,rgb(seed*50,0,0))}image=captured region=mask.from(captured)}}}';
    fs.writeFileSync(path.join(tree,"leaf.ips"),leaf);
    const snapshot='import leaf from "leaf.ips" asset t{size 8x1 draw{leaf.draw(1,at={1,0})first=leaf.outputs.image old=leaf.outputs.region leaf.draw(2,at={4,0})second=leaf.outputs.image leaf.draw(3,at={7,0})surface.paint(first)surface.paint(second)surface.paint(leaf.outputs.image)clip(old){rect(0,0,8,1,#00ff00)}}}';
    for(const opt of [false,true]) {
        fs.writeFileSync(path.join(tree,"root.ips"),snapshot);const binary=path.join(tree,"snapshot.iprop");execFileSync(tool("ipc"),[path.join(tree,"root.ips"),...(opt?[]:["--no-opt"]),"-o",binary]);
        const result=generate(binary), expected=Buffer.alloc(32);expected.set([0,255,0,255],4);expected.set([100,0,0,255],16);assert.deepEqual(result.rgba,expected);++pins;
    }
    function trap(binary,pattern) {
        const native=spawnSync(tool("ipgen"),[binary,"--seed","26","-o",base+"-trap.png"],{encoding:"utf8"});
        assert.equal(native.status,1);assert.match(native.stderr,pattern);assert.throws(()=>iprop.generate(new Uint8Array(fs.readFileSync(binary)),{seed:26}),pattern);++traps;
    }
    const unset=base+"-unset.iprop";execFileSync(tool("ipc"),[path.join(repo,"examples/language/traps/export-unset.ips"),"-o",unset]);trap(unset,/part resource export not written/);
    const wrong='import first from "leaf.ips" import second from "leaf.ips" asset t{size 4x1 draw{first.draw(1,at={0,0})second.draw(1,at={2,0})surface.paint(first.outputs.image)}}';
    fs.writeFileSync(path.join(tree,"root.ips"),wrong);const wrongBinary=path.join(tree,"wrong.iprop");execFileSync(tool("ipc"),[path.join(tree,"root.ips"),"-o",wrongBinary]);trap(wrongBinary,/last called part/);
    // Repeated replacement is bounded by live handles, rather than arena history.
    const reusable=compile('asset t{size 1024x1024 draw{layer image{}region=mask.from(image)for i in 0..64{region=mask.invert(region)}}}',["--no-opt"]);
    const assembly=base+"-large.ipa",large=base+"-large.iprop";fs.writeFileSync(assembly,execFileSync(tool("ipdis"),[reusable],{encoding:"utf8"}).replace(/fuel=\d+/,'fuel=200000000'));execFileSync(tool("ipa"),[assembly,"-o",large]);generate(large);
    // A real snapshot survives slot replacement and keeps its allocation alive.
    fs.writeFileSync(path.join(tree,"leaf.ips"),'asset t{size 1024x1024 output layer image draw{layer captured{}image=captured}}');
    const many=Array.from({length:16},(_,i)=>`leaf.draw(seed,at={0,0})snapshot${i}=leaf.outputs.image`).join(';');
    fs.writeFileSync(path.join(tree,"root.ips"),'import leaf from "leaf.ips" asset t{size 1x1 draw{'+many+'}}');
    const quota=path.join(tree,"quota.iprop");execFileSync(tool("ipc"),[path.join(tree,"root.ips"),"--no-opt","--no-compress","-o",quota]);
    fs.writeFileSync(assembly,execFileSync(tool("ipdis"),[quota],{encoding:"utf8"}).replace(/fuel=\d+/,'fuel=200000000'));execFileSync(tool("ipa"),[assembly,"-o",large]);trap(large,/generation data exceeds 64 MiB/);
    const valid=base+"-example.iprop";execFileSync(tool("ipc"),[path.join(repo,"examples/language/resource-exports.ips"),"--no-opt","--no-compress","-o",valid]);
    const bytes=fs.readFileSync(valid), section={offset:sections(bytes).get(10)};
    function reject(broken) {
        const binary=base+"-bad.iprop";fs.writeFileSync(binary,broken);const native=spawnSync(tool("ipval"),[binary],{encoding:"utf8"});assert.equal(native.status,1);assert.throws(()=>iprop.generate(new Uint8Array(broken),{seed:26}));++rejections;
    }
    for(const [offset,value] of [[0,65537],[4,1000],[8,100000],[4+12*5,0]]){const broken=Buffer.from(bytes);broken.writeUInt32LE(value,section.offset+offset);reject(broken);}
    for(const [offset,value] of [[12,65535],[14,1]]){const broken=Buffer.from(bytes);broken.writeUInt16LE(value,section.offset+offset);reject(broken);}
    for(const ins of instructions(bytes).filter(i=>i.op>=0xd2&&i.op<=0xd5)){
        for(const field of ['slot','aux','type']){const broken=Buffer.from(bytes);if(field==='slot')broken.writeUInt16LE(65535,ins.at+2);if(field==='aux')broken[ins.at+1]=1;if(field==='type')broken[ins.at]=ins.op===0xd2?0xd3:ins.op===0xd3?0xd2:ins.op===0xd4?0xd5:0xd4;reject(broken);}
    }
    for(let minor=1;minor<6;++minor){const broken=Buffer.from(bytes);broken.writeUInt16LE(minor,6);reject(broken);}
    return {comparisons,pins,traps,rejections,trees:oracle.cases.length,oracle_images:216};
}
module.exports={run};
