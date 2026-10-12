"use strict";
const assert=require("node:assert/strict");
const {execFileSync,spawnSync}=require("node:child_process");
const fs=require("node:fs");
const path=require("node:path");
const crypto=require("node:crypto");
const {decodePng}=require("./png.js");
const repo=path.resolve(__dirname,"../../..");
const sha=data=>crypto.createHash("sha256").update(data).digest("hex");
function canonical(value){
    if(Array.isArray(value))return value.map(canonical);
    if(value && typeof value==="object")return Object.fromEntries(Object.keys(value).sort().map(key=>[key,canonical(value[key])]));
    return value;
}
function sections(data){
    const result=new Map();
    for(let i=0;i<data.readUInt32LE(12);++i)result.set(data.readUInt32LE(20+i*12),data.readUInt32LE(24+i*12));
    return result;
}
function instructions(data){
    const start=sections(data).get(3), functions=[];let at=start+4;
    function type(){const value=data[at++];if(value===0x80)at+=2;}
    for(let i=0;i<data.readUInt32LE(start);++i){
        functions.push({offset:data.readUInt32LE(at+4),length:data.readUInt32LE(at+8)});at+=12;
        for(let slot=0;slot<3;++slot){const count=data.readUInt16LE(at);at+=2;for(let n=0;n<count;++n)type();}
    }
    const end=functions.at(-1).offset+functions.at(-1).length;
    assert.equal(data.readUInt32LE(at+end),0,"sampler fixture has no parts");
    return functions.flatMap(fn=>Array.from({length:fn.length/4},(_,i)=>at+fn.offset+4*i));
}
function run(iprop,tool,directory){
    const base=path.join(directory,"sampler"), seeds=[0,1,7,26,0xffffffff];
    let comparisons=0,traps=0,rejections=0;
    function compile(source,optimized=true,suffix=""){
        const input=base+suffix+".ips",binary=base+suffix+".iprop";fs.writeFileSync(input,source);
        execFileSync(tool("ipc"),[input,...(optimized?[]:["--no-opt"]),"--no-compress","-o",binary]);return binary;
    }
    function generate(binary,seed,size={}){
        const png=base+".png",flags=size.sizeX?["--size",size.sizeX+"x"+size.sizeY]:[];
        const native=JSON.parse(execFileSync(tool("ipgen"),[binary,"--seed",String(seed),"--float-bits",...flags,"-o",png],{encoding:"utf8"}));
        const image=decodePng(fs.readFileSync(png));
        const wasm=iprop.generate(new Uint8Array(fs.readFileSync(binary)),{seed,...size});
        const outputs=wasm.outputs.map(slot=>({name:slot.name,kind:["point","rect","circle"][slot.kind],list:slot.isList,values:slot.valueBits}));
        assert.deepEqual(outputs,native.outputs,"sampler output bits");
        assert.equal(wasm.width,image.width);assert.equal(wasm.height,image.height);
        assert.deepEqual(Buffer.from(wasm.data),Buffer.from(image.data),"sampler RGBA");++comparisons;
        return {rgba_sha256:sha(image.data),outputs_sha256:sha(JSON.stringify(canonical(native.outputs)))};
    }
    const frozen=JSON.parse(fs.readFileSync(path.join(repo,"tests/fixtures/frozen-format-v0.6-stage3.json"),"utf8"));
    const oracle=frozen.cases.find(entry=>entry.source==="inline:sampler-runtime-reference");
    for(const optimized of [false,true])for(const debug of [false,true])for(const compressed of [false,true]){
        const flags=[...(optimized?[]:["--no-opt"]),debug?"--debug":"--strip",compressed?"--compress":"--no-compress"];
        const binary=base+"-example.iprop";
        execFileSync(tool("ipc"),[path.join(repo,"examples/language/samplers.ips"),...flags,"-o",binary]);
        for(const seed of seeds){const actual=generate(binary,seed),expected=oracle.generations.find(entry=>entry.seed===seed);
            assert.equal(actual.rgba_sha256,expected.rgba_sha256,"preserved pre-sampler pixels");
            assert.equal(actual.outputs_sha256,expected.outputs_sha256,"preserved pre-sampler draws and state");}
        generate(binary,26,{sizeX:64,sizeY:12});
        fs.writeFileSync(base+".ipa",execFileSync(tool("ipdis"),[binary]));
        execFileSync(tool("ipa"),[base+".ipa","-o",base+"-restored.iprop"]);
        assert.deepEqual(generate(base+"-restored.iprop",26),generate(binary,26),"sampler IPA/IPDIS");
    }
    const cases=[
        ["i32","16777217","values={value&65535,value>>16}"],
        ["f32","-0.0","values={value,value}"],
        ["color","#12345678","values={value.r,value.g} values={value.b,value.a}"],
        ["vec2","{3,4}","values=value"],
        ["i32","-2147483648 to 0","values={value&65535,value>>16}"],
        ["i32","-2147483648 to 2147483647","values={value&65535,value>>16}"],
        ["i32","40 to 40","values={value,value}"],
        ["f32","-3 to 4.5","values={value,value}"],
        ["f32","-0.0 to 0.0","values={value,value}"],
        ["f32","(-1.1754943508222875e-38*0.5) to (1.1754943508222875e-38*0.5)","values={value,value}"],
        ["i32","10 to 30 with gaussian(20,3)","values={value,value}"],
        ["f32","0.0 to 1.0 with triangular(0,0.75,1)","values={value,value}"],
        ["f32","0.0 to 1.0 with beta(2,3)","values={value,value}"],
        ["color","#12345678 to #fedcba98","values={value.r,value.g} values={value.b,value.a} pixel(i%16,i/16,value)"],
        ["color","#12345678 to #fedcba98 with gaussian(0.5,0.2)","values={value.r,value.g} values={value.b,value.a} pixel(i%16,i/16,value)"],
        ["vec2","{-3,4} to {5,-6}","values=value"],
        ["vec2","{-3,4} to {5,-6} with beta(2,3)","values=value"],
    ];
    const asset=(body,helpers="")=>"asset t{size 16x16 output list point values output point state "+helpers+" draw{"+body+" state={seed&65535,seed>>16}}}";
    for(const [type,range,publish] of cases)for(const count of [0,1,3,256]){
        const reference=compile(asset(`for i in 0..${count}{value=${range} ${publish}}`),false,"-reference");
        for(const optimized of [false,true]){
            const helpers=`echo(s:sampler<${type}>)->sampler<${type}>{return s}`;
            const batch=compile(asset(`s=echo(sampler((${range}))) items=s.pick(n=${count}) for i in 0..list.len(items){value=items[i] ${publish}}`,helpers),optimized);
            for(const seed of seeds)assert.deepEqual(generate(batch,seed),generate(reference,seed),"batch equals manual range loop");
        }
    }
    for(const [type,value,publish] of [
        ["i32","16777217","values={value==16777217,value==16777217}"],
        ["f32","-0.0","values={value,value}"],
        ["color","#12345678","values={value.r,value.a}"],
        ["vec2","{3,4}","values=value"]]){
        const descriptions="["+Array.from({length:256},()=>`sampler(${value})`).join(",")+"]";
        const helpers=`echo(items:list<sampler<${type}>>)->list<sampler<${type}>>{return items} empty()->list<sampler<${type}>>{return []}`;
        const binary=compile(asset(`before=seed items=echo(${descriptions}) copy=items items=[] value=copy[255].pick() ${publish} values={seed==before,list.len(empty())}`,helpers),false);
        for(const seed of seeds)generate(binary,seed);
    }
    const effectful=compile(asset("items=description().pick(count()) for i in 0..list.len(items){value=items[i] values={value,value}}",
        "description()->sampler<i32>{return sampler((10 to 15) to (20 to 30) with gaussian(float(15 to 18),2))} count()->i32{return 1 to 3}"));
    const ordered=compile(asset("lo=10 to 15 hi=20 to 30 d=gaussian(float(15 to 18),2) n=1 to 3 for i in 0..n{value=lo to hi with d values={value,value}}"),false,"-order");
    for(const seed of seeds)assert.deepEqual(generate(effectful,seed),generate(ordered,seed),"receiver and count evaluate once in order");
    function trap(binary,message){
        const png=base+"-rejected.png";
        const native=spawnSync(tool("ipgen"),[binary,"-o",png],{encoding:"utf8"});
        assert.equal(native.status,1,native.stderr);assert.equal(native.signal,null);assert.match(native.stderr,message);
        assert.equal(fs.existsSync(png),false,"trap wrote a successful artifact");
        assert.throws(()=>iprop.generate(new Uint8Array(fs.readFileSync(binary)),{}),message);++traps;
    }
    for(const [body,message] of [
        ["n=-1 s=sampler(10 to 15) items=s.pick(n)",/result count must be between 0 and 256/],
        ["n=257 s=sampler(10 to 15) items=s.pick(n)",/result count must be between 0 and 256/],
        ["if(false){s=sampler(10 to 15)} items=s.pick(0)",/sampler handle is unassigned/],
        ["lo=15 hi=10 s=sampler(lo to hi)",/lower bound exceeds/],
        ["lo=sqrt(-1) s=sampler(lo to 1.0)",/bounds must be finite/],
        ["if(false){d=gaussian(0.5,1)} s=sampler(0.0 to 1.0 with d)",/distribution is not initialized or invalid/],
        ["s=sampler(0.0 to 1.0 with triangular(10,15,20)) empty=s.pick(0) value=s.pick()",/random range has no probability under the distribution/],
    ])for(const optimized of [false,true])trap(compile("asset t{size 1x1 draw{"+body+"}}",optimized),message);
    // Actual quota exhaustion for descriptions and for returned batches.
    for(const body of ["const.i32 1\nsampler.value 0\ndrop\n",
        "local.get 2\nconst.i32 256\nsampler.pick.list 0\ndrop\n"]){
        const setup=body.includes("local.get")?"locals: sampler<i32>\nconst.i32 1\nsampler.value 0\nlocal.set 2\n":"";
        fs.writeFileSync(base+".ipa","meta: size=1x1 fuel=100000000\ntypes: sampler<i32>\nfn draw ( i32, i32 ) -> ( ):\n"+setup+"loop\n"+body+"br 0\nend\nreturn\nendfn\n");
        execFileSync(tool("ipa"),[base+".ipa","-o",base+".iprop"]);trap(base+".iprop",/generation data exceeds 64 MiB/);
    }
    function reject(data){
        fs.writeFileSync(base+"-bad.iprop",data);
        const native=spawnSync(tool("ipval"),[base+"-bad.iprop"],{encoding:"utf8"});
        assert.equal(native.status,1,native.stderr);assert.equal(native.signal,null);
        assert.throws(()=>iprop.generate(new Uint8Array(data),{}));++rejections;
    }
    const binary=compile(asset("items=[sampler(1),sampler(2 to 4)] value=items[0].pick() values={value,list.len(items)}"),false);
    const original=fs.readFileSync(binary),typeOffset=sections(original).get(6);
    for(const [offset,value] of [[typeOffset+4,255],[typeOffset+5,4],[typeOffset+6,1],[typeOffset+10,1],[typeOffset+10,255]]){
        const bad=Buffer.from(original);bad[offset]=value;reject(bad);
    }
    // Aux, descriptor bounds and kind are checked for all five operations,
    // also after a return where no operand stack can execute.
    fs.writeFileSync(base+"-typed-gate.ipa","meta: size=1x1\ntypes: sampler<i32> list<i32>\nfn draw ( i32, i32 ) -> ( ):\n"+
        "return\nsampler.value 0\nsampler.range 0\nsampler.with 0\nsampler.pick 0\nsampler.pick.list 0\nendfn\n");
    execFileSync(tool("ipa"),[base+"-typed-gate.ipa","-o",base+"-typed-gate.iprop"]);
    const typed=fs.readFileSync(base+"-typed-gate.iprop");
    for(const offset of instructions(typed).slice(1)){
        let bad=Buffer.from(typed);bad[offset+1]=1;reject(bad);
        bad=Buffer.from(typed);bad.writeUInt16LE(65535,offset+2);reject(bad);
        bad=Buffer.from(typed);bad.writeUInt16LE(1,offset+2);reject(bad);
    }
    // Every sampler opcode is rejected in section-free dead code on minors 1–5.
    fs.writeFileSync(base+"-gate.ipa","meta: size=1x1\nfn draw ( i32, i32 ) -> ( ):\nreturn\nnop\nendfn\n");
    execFileSync(tool("ipa"),[base+"-gate.ipa","-o",base+"-gate.iprop"]);
    const gate=fs.readFileSync(base+"-gate.iprop"),dead=instructions(gate)[1];assert.equal(gate[dead],0);
    for(const opcode of [0xbc,0xbd,0xbe,0xbf,0xc0])for(let minor=1;minor<=5;++minor){
        const bad=Buffer.from(gate);bad[dead]=opcode;bad.writeUInt16LE(minor,6);reject(bad);
    }
    // A caught trap/rejection must leave the WASM module usable.
    generate(binary,26);
    return {comparisons,traps,rejections};
}
module.exports={run};
