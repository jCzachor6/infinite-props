"use strict";
const assert=require("node:assert/strict");
const {execFileSync,spawnSync}=require("node:child_process");
const fs=require("node:fs"),path=require("node:path"),crypto=require("node:crypto");
const {decodePng}=require("./png.js");
const {sections,instructions}=require("./sampler-conformance.js");
const repo=path.resolve(__dirname,"../../..");
const sha=data=>crypto.createHash("sha256").update(data).digest("hex");
function canonical(value){
    if(Array.isArray(value))return value.map(canonical);
    if(value && typeof value==="object")return Object.fromEntries(Object.keys(value).sort().map(key=>[key,canonical(value[key])]));
    return value;
}
function replaceSections(original,replacements){
    const entries=[];
    for(let i=0;i<original.readUInt32LE(12);++i){
        const id=original.readUInt32LE(20+i*12),at=original.readUInt32LE(24+i*12),size=original.readUInt32LE(28+i*12);
        entries.push([id,replacements.get(id)||original.subarray(at,at+size)]);
    }
    const header=Buffer.from(original.subarray(0,20)),table=Buffer.alloc(entries.length*12);
    let offset=20+table.length;
    for(let i=0;i<entries.length;++i){table.writeUInt32LE(entries[i][0],i*12);table.writeUInt32LE(offset,i*12+4);
        table.writeUInt32LE(entries[i][1].length,i*12+8);offset+=entries[i][1].length;}
    return Buffer.concat([header,table,...entries.map(entry=>entry[1])]);
}
function run(iprop,tool,directory){
    const base=path.join(directory,"sampling-palette"),seeds=[0,1,7,26,0xffffffff];
    let comparisons=0,traps=0,rejections=0;
    function compile(source,optimized=true,suffix=""){
        const input=base+suffix+".ips",binary=base+suffix+".iprop";fs.writeFileSync(input,source);
        execFileSync(tool("ipc"),[input,...(optimized?[]:["--no-opt"]),"--no-compress","-o",binary]);return binary;
    }
    function generate(binary,seed,size={}){
        const png=base+".png",flags=size.sizeX?["--size",`${size.sizeX}x${size.sizeY}`]:[];
        const native=JSON.parse(execFileSync(tool("ipgen"),[binary,"--seed",String(seed),"--float-bits",...flags,"-o",png],{encoding:"utf8"}));
        const image=decodePng(fs.readFileSync(png)),wasm=iprop.generate(new Uint8Array(fs.readFileSync(binary)),{seed,...size});
        const outputs=wasm.outputs.map(slot=>({name:slot.name,kind:["point","rect","circle"][slot.kind],list:slot.isList,values:slot.valueBits}));
        assert.deepEqual(outputs,native.outputs,"palette output bits");assert.equal(wasm.width,image.width);assert.equal(wasm.height,image.height);
        assert.deepEqual(Buffer.from(wasm.data),Buffer.from(image.data),"palette RGBA");++comparisons;
        return {rgba_sha256:sha(image.data),outputs_sha256:sha(JSON.stringify(canonical(native.outputs)))};
    }
    const fixture=JSON.parse(fs.readFileSync(path.join(repo,"tests/fixtures/frozen-format-v0.6-stage4.json"),"utf8"));
    const oracle=fixture.cases.find(entry=>entry.source==="inline:sampling-palette-runtime-reference");
    for(const optimized of [false,true])for(const debug of [false,true])for(const compressed of [false,true]){
        const binary=base+"-example.iprop",flags=[...(optimized?[]:["--no-opt"]),debug?"--debug":"--strip",compressed?"--compress":"--no-compress"];
        execFileSync(tool("ipc"),[path.join(repo,"examples/language/sampling-palettes.ips"),...flags,"-o",binary]);
        for(const seed of seeds){const actual=generate(binary,seed),expected=oracle.generations.find(entry=>entry.seed===seed);
            assert.equal(actual.rgba_sha256,expected.rgba_sha256,"stage-4 oracle pixels");assert.equal(actual.outputs_sha256,expected.outputs_sha256,"stage-4 oracle draws and RNG");}
        generate(binary,26,{sizeX:64,sizeY:12});
        fs.writeFileSync(base+".ipa",execFileSync(tool("ipdis"),[binary]));execFileSync(tool("ipa"),[base+".ipa","-o",base+"-restored.iprop"]);
        assert.deepEqual(generate(base+"-restored.iprop",26),generate(binary,26),"constant sampler IPA/IPDIS");
    }
    const cases=[
        ["i32","16777217","values={value&65535,value>>16}"], ["f32","-0.0","values={value,value}"],
        ["color","#12345678","values={value.r,value.g} values={value.b,value.a}"], ["vec2","{3,4}","values=value"],
        ["i32","10 to 15","values={value,value}"], ["i32","-2147483648 to 0","values={value&65535,value>>16}"],
        ["i32","-2147483648 to 2147483647","values={value&65535,value>>16}"], ["i32","40 to 40","values={value,value}"],
        ["f32","-3 to 4.5","values={value,value}"], ["f32","-0.0 to 0.0","values={value,value}"],
        ["f32","(-1.1754943508222875e-38*0.5) to (1.1754943508222875e-38*0.5)","values={value,value}"],
        ["i32","10 to 30 with gaussian(20,3)","values={value,value}"], ["f32","0.0 to 1.0 with triangular(0,0.75,1)","values={value,value}"],
        ["f32","0.0 to 1.0 with beta(2,3)","values={value,value}"],
        ["color","#12345678 to #fedcba98","values={value.r,value.g} values={value.b,value.a} pixel(i%16,i/16,value)"],
        ["color","#12345678 to #fedcba98 with gaussian(0.5,0.2)","values={value.r,value.g} values={value.b,value.a} pixel(i%16,i/16,value)"],
        ["vec2","{-3,4} to {5,-6}","values=value"], ["vec2","{-3,4} to {5,-6} with beta(2,3)","values=value"],
    ];
    const asset=(declarations,body,helpers="")=>"asset t{size 16x16 output list point values output point state "+declarations+helpers+" draw{"+body+" state={seed&65535,seed>>16}}}";
    for(const [type,expression,publish] of cases)for(const count of [0,1,3,256]){
        const reference=compile(asset("",`for i in 0..${count}{value=${expression} ${publish}}`),false,"-reference");
        const expected=seeds.map(seed=>generate(reference,seed));
        const declarations=[`const sampler<${type}> description=sampler(${expression})`,
            `palette specs=[sampler(${expression})] const sampler<${type}> description=specs[-1]`,
            `const list<sampler<${type}>> specs=[sampler(${expression})] const sampler<${type}> description=specs[0]`];
        for(const optimized of [false,true])for(const declaration of declarations){
            const helpers=`echo(s:sampler<${type}>)->sampler<${type}>{return s}`;
            const binary=compile(asset(declaration,`s=echo(description) items=s.pick(${count}) for i in 0..list.len(items){value=items[i] ${publish}}`,helpers),optimized);
            for(let i=0;i<seeds.length;++i)assert.deepEqual(generate(binary,seeds[i]),expected[i],"const/palette/list sampler equals manual draws");
        }
    }
    const widths="const sampler<i32> fixed=sampler(40) const distribution d=gaussian(25,2) palette widths=[10 to 15,20 to 30 with d,fixed]";
    for(const index of [-2147483648,-1,0,1,2,3,2147483647])for(const optimized of [false,true]){
        const binary=compile(asset(widths,`before=seed s=widths[${index}] copy=s values={seed==before,0} items=copy.pick(3) for i in 0..3{values={items[i],items[i]}}`),optimized);
        const selected=index===0?"10 to 15":index===1?"20 to 30 with gaussian(25,2)":"40";
        const reference=compile(asset("",`values={1,0} for i in 0..3{value=${selected} values={value,value}}`),false,"-reference");
        for(const seed of seeds)assert.deepEqual(generate(binary,seed),generate(reference,seed),"outer palette index fallback");
    }
    const effectful=compile(asset(widths,"items=widths[select()].pick(count()) for i in 0..list.len(items){values={items[i],items[i]}}",
        "select()->i32{pixel(0,0,#f00) return index(5)-1} count()->i32{pixel(1,0,#0f0) return 1 to 3}"));
    const ordered=compile(asset("","pixel(0,0,#f00) k=index(5)-1 pixel(1,0,#0f0) n=1 to 3 for i in 0..n{value=40 if(k==0){value=10 to 15} if(k==1){value=20 to 30 with gaussian(25,2)} values={value,value}}"),false,"-ordered");
    for(const seed of seeds)assert.deepEqual(generate(effectful,seed),generate(ordered,seed),"index before count, each once");
    const sameNames="copy(widths:list<sampler<i32>>)->list<sampler<i32>>{return widths} "+
        "selected(widths:list<sampler<i32>>,i:i32)->sampler<i32>{return widths[i]}";
    for(const optimized of [false,true]){
        const binary=compile(asset(widths,"before=seed items=copy([sampler(7)]) values={selected(items,0).pick(),widths[-1].pick()} values={seed==before,list.len(items)}",sameNames),optimized);
        const reference=compile(asset("","values={7,40} values={1,1}"),false,"-reference");
        for(const seed of seeds)assert.deepEqual(generate(binary,seed),generate(reference,seed),"list parameter sharing palette name");
    }
    for(const [type,value,publish] of cases.slice(0,4)){
        const entries="["+Array.from({length:256},()=>`sampler(${value})`).join(",")+"]";
        const binary=compile(asset(`palette specs=${entries} const list<sampler<${type}>> copy=specs const list<sampler<${type}>> empty=[]`,
            `before=seed items=echo(copy) value=items[255].pick() ${publish} values={seed==before,list.len(empty)}`,
            `echo(items:list<sampler<${type}>>)->list<sampler<${type}>>{return items}`),false);
        for(const seed of seeds)generate(binary,seed);
    }
    // Repeated const/list reads must share storage. An incorrect per-read
    // charge would exhaust the arena long before 5,000 reads of 256 entries.
    const entries="["+Array.from({length:256},()=>"sampler(40)").join(",")+"]";
    generate(compile(asset(`const list<sampler<i32>> specs=${entries}`,"before=seed for i in 0..5000{items=specs s=items[255] value=s.pick()} values={seed==before,value}"),false),26);
    function trap(binary,message){
        const png=base+"-trap.png",native=spawnSync(tool("ipgen"),[binary,"-o",png],{encoding:"utf8"});
        assert.equal(native.status,1,native.stderr);assert.equal(native.signal,null);assert.match(native.stderr,message);
        assert.equal(fs.existsSync(png),false,"trap published a successful artifact");
        assert.throws(()=>iprop.generate(new Uint8Array(fs.readFileSync(binary)),{}),message);++traps;
    }
    for(const [declaration,body,message] of [
        [widths,"items=widths value=items[-1].pick()",/list index out of range/],
        [widths,"value=selected(widths,3).pick()",/list index out of range/],
        ["palette specs=[0.0 to 1.0 with triangular(10,15,20),0.5]","empty=specs[0].pick(0) value=specs[0].pick()",/random range has no probability/],
        [widths,"n=-1 items=widths[0].pick(n)",/result count must be between/],
        [widths,"n=257 items=widths[0].pick(n)",/result count must be between/],
    ])for(const optimized of [false,true])trap(compile(asset(declaration,body,"selected(s:list<sampler<i32>>,i:i32)->sampler<i32>{return s[i]}"),optimized),message);
    for(const optimized of [false,true])trap(compile(asset(widths,"value=selected(widths,-1).pick()",sameNames),optimized),/list index out of range/);
    fs.writeFileSync(base+".ipa","meta: size=1x1 fuel=100000000\ntypes: sampler<i32>\nsampler: sampler<i32> 000028000000\nfn draw ( i32, i32 ) -> ( ):\nloop\nconst.sampler 0\nconst.i32 256\nsampler.pick.list 0\ndrop\nbr 0\nend\nreturn\nendfn\n");
    execFileSync(tool("ipa"),[base+".ipa","-o",base+"-quota.iprop"]);trap(base+"-quota.iprop",/generation data exceeds 64 MiB/);
    function reject(data,message){
        fs.writeFileSync(base+"-bad.iprop",data);const native=spawnSync(tool("ipval"),[base+"-bad.iprop"],{encoding:"utf8"});
        assert.equal(native.status,1,native.stderr);assert.equal(native.signal,null);if(message)assert.match(native.stderr,message);
        assert.throws(()=>iprop.generate(new Uint8Array(data),{}),message);++rejections;
    }
    const valid=compile("asset t{size 1x1 const sampler<f32> s=sampler(0.0 to 1.0 with beta(2,3)) const list<sampler<f32>> specs=[s] draw{items=specs x=s}}",false);
    const original=fs.readFileSync(valid),offsets=sections(original),pool=offsets.get(8),lists=offsets.get(7);
    const mutations=[data=>data.writeUInt32LE(65537,pool),data=>data.writeUInt16LE(65535,pool+4),data=>data.writeUInt16LE(1,pool+4),
        data=>{data[pool+6]=3;},data=>{data[pool+7]=1;},data=>data.writeUInt32LE(0x7f800000,pool+8),data=>data.writeUInt32LE(0x7fc00000,pool+8),
        data=>data.writeFloatLE(2,pool+8),data=>{data[pool+16]=0;},data=>{data[pool+16]=255;},data=>data.writeFloatLE(0,pool+17),
        data=>data.writeFloatLE(Infinity,pool+21),data=>data.writeUInt32LE(0,pool),data=>data.writeUInt16LE(65535,lists+8),
        data=>data.writeUInt16LE(257,lists+6),data=>data.writeUInt16LE(0,lists+4),data=>data.writeUInt16LE(0,lists+6)];
    for(const mutate of mutations){const bad=Buffer.from(original);mutate(bad);reject(bad);}
    const mismatch=fs.readFileSync(compile("asset t{size 1x1 const sampler<i32> intSpec=sampler(1) const sampler<f32> floatSpec=sampler(1.0) const list<sampler<i32>> specs=[intSpec] draw{items=specs s=floatSpec}}",false));
    const wrongType=Buffer.from(mismatch);wrongType.writeUInt16LE(1,sections(mismatch).get(7)+8);reject(wrongType,/type mismatch/);
    for(let minor=1;minor<=5;++minor){const bad=Buffer.from(original);bad.writeUInt16LE(minor,6);reject(bad);}
    fs.writeFileSync(base+"-dead.ipa","meta: size=1x1\ntypes: sampler<i32>\nsampler: sampler<i32> 000028000000\nfn draw ( i32, i32 ) -> ( ):\nreturn\nconst.sampler 0\nendfn\n");
    execFileSync(tool("ipa"),[base+"-dead.ipa","-o",base+"-dead.iprop"]);const dead=fs.readFileSync(base+"-dead.iprop"),at=instructions(dead)[1];
    let bad=Buffer.from(dead);bad[at+1]=1;reject(bad);bad=Buffer.from(dead);bad.writeUInt16LE(65535,at+2);reject(bad);
    fs.writeFileSync(base+"-gate.ipa","meta: size=1x1\nfn draw ( i32, i32 ) -> ( ):\nreturn\nnop\nendfn\n");
    execFileSync(tool("ipa"),[base+"-gate.ipa","-o",base+"-gate.iprop"]);const gate=fs.readFileSync(base+"-gate.iprop"),gateAt=instructions(gate)[1];
    for(let minor=1;minor<=5;++minor){bad=Buffer.from(gate);bad[gateAt]=0xc1;bad.writeUInt16LE(minor,6);reject(bad);}
    // File-level quota includes sampler descriptions and list buffers together.
    const samplerRecord=original.subarray(pool+4,pool+33),samplerPool=Buffer.alloc(4+65536*samplerRecord.length);samplerPool.writeUInt32LE(65536);
    for(let i=0;i<65536;++i)samplerRecord.copy(samplerPool,4+i*samplerRecord.length);
    const count=Math.floor((67108864-65536*256)/(128+256*64))+1,listPool=Buffer.alloc(4+count*(4+256*2));listPool.writeUInt32LE(count);
    for(let i=0;i<count;++i){listPool.writeUInt16LE(1,4+i*516);listPool.writeUInt16LE(256,6+i*516);}
    reject(replaceSections(original,new Map([[8,samplerPool],[7,listPool]])),/collection pool exceeds 64 MiB/);
    generate(valid,26); // caught failures must leave the WASM module usable
    return {comparisons,traps,rejections};
}
module.exports={run};
