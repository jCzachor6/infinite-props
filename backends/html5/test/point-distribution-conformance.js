"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs"), path = require("node:path");
const {execFileSync, spawnSync} = require("node:child_process");
const {decodePng} = require("./png.js");
const {sections, instructions} = require("./sampler-conformance.js");
function hash(x) { x ^= x >>> 16; x = Math.imul(x,0x7feb352d); x ^= x >>> 15; x = Math.imul(x,0x846ca68b); return (x ^ (x >>> 16)) >>> 0; }
function bits(value) { const b=Buffer.alloc(4); b.writeFloatLE(value); return b.readUInt32LE(); }
function run(iprop, tool, directory) {
    const base=path.join(directory,"point-distribution"),seeds=[0,1,7,26,42,0x61c88647,0xffffffff];
    let comparisons=0,traps=0,rejections=0;
    function compile(source,optimized=true,suffix="") {
        const input=base+suffix+".ips",binary=base+suffix+".iprop"; fs.writeFileSync(input,source);
        execFileSync(tool("ipc"),[input,...(optimized?[]:["--no-opt"]),"--no-compress","-o",binary]); return binary;
    }
    function generate(binary,seed) {
        const png=base+".png",native=JSON.parse(execFileSync(tool("ipgen"),[binary,"--seed",String(seed),"--float-bits","-o",png],{encoding:"utf8"}));
        const image=decodePng(fs.readFileSync(png)),wasm=iprop.generate(new Uint8Array(fs.readFileSync(binary)),{seed});
        const outputs=wasm.outputs.map(slot=>({name:slot.name,kind:["point","rect","circle"][slot.kind],list:slot.isList,values:slot.valueBits}));
        assert.deepEqual(outputs,native.outputs,"point distribution output bits");
        assert.deepEqual(Buffer.from(wasm.data),Buffer.from(image.data),"point distribution RGBA");
        ++comparisons; return native.outputs;
    }
    const asset=(declarations,body,helpers="")=>"asset t{input i32 v=0 size 16x16 output list point p output point state "+declarations+helpers+
        " draw{"+body+" state={seed&65535,seed>>16}}}";
    // A separate raw-hash threshold oracle, independent of the runtime kernel.
    for(const optimized of [false,true]) {
        const binary=compile(asset("","d=weighted{1:50,2:20,3:20,4:10} for i in 0..64{p={1 to 4 with d,0}}"),optimized);
        for(const seed of seeds) {
            const outputs=generate(binary,seed); let stream=seed;
            for(const p of outputs[0].values) {
                stream=(stream+0x9e3779b9)>>>0; const u=(hash(stream)+0.5)*2**-32;
                assert.equal(p.x,bits(u<0.5?1:u<0.7?2:u<0.9?3:4),"50/20/20/10 raw RNG oracle");
            }
            assert.deepEqual(outputs[1].values,[{x:bits(stream&65535),y:bits(stream>>>16)}]);
        }
    }
    const draws=[
        ["i32","1 to 4 with weighted{1:50,2:20,3:20,4:10}","p={x,x}"],
        ["i32","16777217 to 16777219 with weighted{16777217:1,16777218.0:1,16777219:1}","p={x&65535,x>>16}"],
        ["i32","(-2147483648) to 2147483647 with weighted{(-2147483648):1,2147483647:1}","p={x&65535,x>>16}"],
        ["i32","(-2) to 2 with weighted{(-1.5):1,1.5:1}","p={x,x}"],
        ["f32","1.0 to 3.0 with weighted{1:75,2:20,3:5}","p={x,x}"],
        ["f32","(-0.0) to 0.0 with weighted{(-0.0):1}","p={x,x}"],
        ["f32","0.0 to 1.0 with weighted{0:0,0.5:1,1:0}","p={x,x}"],
        ["f32","2.0 to 3.0 with weighted{1:100,2:1,3:1,4:100}","p={x,x}"],
        ["f32","0.0 to 1.0 with weighted{0:(1.1754943508222875e-38*0.5),1:3.4028234663852886e38}","p={x,x}"],
        ["color","#12345678 to #fedcba98 with weighted{0:1,0.5:2,1:1}","p={x.r,x.g} p={x.b,x.a} pixel(i%16,i/16,x)"],
        ["vec2","{-3,4} to {5,-6} with weighted{0:1,0.25:2,1:1}","p=x"],
    ];
    for(const [type,expression,publish] of draws)for(const count of [0,1,3,256]) {
        const reference=compile(asset("",`for i in 0..${count}{x=${expression} ${publish}}`),false,"-reference");
        const expected=seeds.map(seed=>generate(reference,seed));
        for(const optimized of [false,true])for(const declaration of [
            `const sampler<${type}> s=sampler(${expression})`,
            `palette specs=[sampler(${expression})] const sampler<${type}> s=specs[-1]`,
            `const list<sampler<${type}>> specs=[sampler(${expression})] const sampler<${type}> s=specs[0]`]) {
            const binary=compile(asset(declaration,`values=s.pick(${count}) for i in 0..list.len(values){x=values[i] ${publish}}`),optimized);
            for(let i=0;i<seeds.length;++i)assert.deepEqual(generate(binary,seeds[i]),expected[i],"point sampler/list/palette equals manual draws");
        }
    }
    for(const optimized of [false,true])for(const debug of [false,true])for(const compressed of [false,true]) {
        const binary=base+"-example.iprop";
        execFileSync(tool("ipc"),[path.resolve(__dirname,"../../../examples/language/weighted.ips"),...(optimized?[]:["--no-opt"]),debug?"--debug":"--strip",compressed?"--compress":"--no-compress","-o",binary]);
        for(const seed of seeds)generate(binary,seed);
        fs.writeFileSync(base+".ipa",execFileSync(tool("ipdis"),[binary]));execFileSync(tool("ipa"),[base+".ipa","-o",base+"-restored.iprop"]);
        assert.deepEqual(generate(binary,26),generate(base+"-restored.iprop",26));
    }
    const many="weighted{"+Array.from({length:256},(_,i)=>`${16777217+i}:1`).join(",")+"}";
    generate(compile(asset("",`before=seed d=${many} copy=echo(d) p={seed==before,16777217 to 16777472 with copy}`,"echo(d:distribution)->distribution{return d}"),false),26);
    generate(compile(asset(`const distribution d=${many} const list<distribution> ds=[d,d]`,"before=seed for i in 0..5000{copy=ds shape=copy[1]} p={seed==before,0}"),false),26);
    function trap(binary,message) {
        const png=base+"-trap.png",native=spawnSync(tool("ipgen"),[binary,"-o",png],{encoding:"utf8"});
        assert.equal(native.status,1,native.stderr); assert.match(native.stderr,message);
        assert.equal(fs.existsSync(png),false); assert.throws(()=>iprop.generate(new Uint8Array(fs.readFileSync(binary)),{}),message); ++traps;
    }
    for(const body of ["d=weighted{1:(v-1),2:1}","d=weighted{v:1,0:1}","d=weighted{1:float(v)/float(v)}",
        "d=weighted{1:v}","d=weighted{1:1,2:0} x=2 to 2 with d","d=weighted{1:1} x=2 to 3 with d",
        "if(v==1){d=weighted{1:1}} x=1 to 1 with d"])
        for(const optimized of [false,true])trap(compile(asset("",body),optimized),/nonnegative|strictly increasing|finite|positive total mass|no probability|not initialized/);
    function reject(data) {
        fs.writeFileSync(base+"-bad.iprop",data);const native=spawnSync(tool("ipval"),[base+"-bad.iprop"],{encoding:"utf8"});
        assert.equal(native.status,1,native.stderr);assert.equal(native.signal,null);
        assert.throws(()=>iprop.generate(new Uint8Array(data),{}));++rejections;
    }
    const valid=compile(asset("const distribution d=weighted{1.0:1.0,2.0:2.0} const sampler<f32> s=sampler(1.0 to 2.0 with d) const list<distribution> ds=[d]","x=s.pick() items=ds"),false);
    const original=fs.readFileSync(valid),offsets=sections(original),pool=offsets.get(9);
    for(const [at,value] of [[pool,65537],[pool,0],[pool+4,0],[pool+4,0x01000204],[pool+8,2],[pool+8,0x10000],
        [pool+12,0x7fc00000],[pool+16,0xbf800000],[pool+24,bits(1)],[pool+16,0x7f800000],[pool+28,0x7f800000]]) {
        const bad=Buffer.from(original);bad.writeUInt32LE(value,at);reject(bad);
    }
    for(const id of [7,8]) {const bad=Buffer.from(original),at=offsets.get(id)+(id===7?9:17);bad.writeUInt16LE(65535,at);reject(bad);}
    for(let minor=1;minor<=5;++minor){const bad=Buffer.from(original);bad.writeUInt16LE(minor,6);reject(bad);}
    fs.writeFileSync(base+"-dead.ipa","meta: size=1x1\nfn draw (  ) -> ( ):\nreturn\nnop\nendfn\n");
    execFileSync(tool("ipa"),[base+"-dead.ipa","-o",base+"-dead.iprop"]);const dead=fs.readFileSync(base+"-dead.iprop"),at=instructions(dead)[1];
    for(const opcode of [0xc2,0xc3,0xc4,0xc5])for(let minor=1;minor<=5;++minor){const bad=Buffer.from(dead);bad[at]=opcode;bad.writeUInt16LE(minor,6);reject(bad);}
    const quota=compile(asset("",`for i in 0..9000{d=${many}}`),false,"-quota");const quotaBytes=fs.readFileSync(quota);
    quotaBytes.writeBigUInt64LE(100000000n,sections(quotaBytes).get(1)+16);fs.writeFileSync(quota,quotaBytes);
    trap(quota,/generation data exceeds 64 MiB/);
    // Valid records are individually small, but their shared file-pool charge
    // must still reject before exceeding the 64 MiB point/list/sampler quota.
    const poolSource=fs.readFileSync(compile(asset(`const distribution d=${many}`,"x=16777217 to 16777472 with d"),false,"-pool"));
    const entries=[];
    for(let i=0;i<poolSource.readUInt32LE(12);++i){const id=poolSource.readUInt32LE(20+i*12),at=poolSource.readUInt32LE(24+i*12),size=poolSource.readUInt32LE(28+i*12);entries.push([id,poolSource.subarray(at,at+size)]);}
    const pointRecord=entries.find(entry=>entry[0]===9)[1].subarray(4),count=Math.floor(67108864/(128+256*32))+1;
    const oversized=Buffer.alloc(4+count*pointRecord.length);oversized.writeUInt32LE(count);
    for(let i=0;i<count;++i)pointRecord.copy(oversized,4+i*pointRecord.length);
    entries.find(entry=>entry[0]===9)[1]=oversized;
    const table=Buffer.alloc(entries.length*12);let offset=20+table.length;
    for(let i=0;i<entries.length;++i){table.writeUInt32LE(entries[i][0],i*12);table.writeUInt32LE(offset,i*12+4);table.writeUInt32LE(entries[i][1].length,i*12+8);offset+=entries[i][1].length;}
    reject(Buffer.concat([poolSource.subarray(0,20),table,...entries.map(entry=>entry[1])]));
    generate(valid,26);
    return {comparisons,traps,rejections};
}
module.exports={run};
