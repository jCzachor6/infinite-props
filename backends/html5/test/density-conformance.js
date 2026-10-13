"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path");
const {execFileSync,spawnSync}=require("node:child_process");
const {decodePng}=require("./png.js");
const {sections,instructions}=require("./sampler-conformance.js");
function run(iprop,tool,directory){
    const base=path.join(directory,"density"),seeds=[0,1,7,26,0x61c88647,0x37c2760b,0xffffffff];
    let comparisons=0,traps=0,rejections=0;
    const asset=(declarations,body,helpers="")=>"asset t{size 16x16 variants 2 output list point p output point state "+declarations+helpers+
        " draw{"+body+" state={seed&65535,seed>>16}}}";
    function compile(source,opt=true,suffix=""){
        const input=base+suffix+".ips",binary=base+suffix+".iprop";fs.writeFileSync(input,source);
        execFileSync(tool("ipc"),[input,...(opt?[]:["--no-opt"]),"--no-compress","-o",binary]);return binary;
    }
    function generate(binary,seed,variant=0){
        const png=base+".png",native=JSON.parse(execFileSync(tool("ipgen"),[binary,"--seed",String(seed),"--variant",String(variant),"--float-bits","-o",png],{encoding:"utf8"}));
        const image=decodePng(fs.readFileSync(png)),wasm=iprop.generate(new Uint8Array(fs.readFileSync(binary)),{seed,variant});
        const outputs=wasm.outputs.map(slot=>({name:slot.name,kind:["point","rect","circle"][slot.kind],list:slot.isList,values:slot.valueBits}));
        assert.deepEqual(outputs,native.outputs,"density output bits");assert.deepEqual(Buffer.from(wasm.data),Buffer.from(image.data),"density RGBA");
        ++comparisons;return outputs;
    }
    // Expected values were generated independently in Python; exact CDFs were
    // cross-checked with 90-digit Decimal arithmetic, including raw hash edges.
    const vectors=JSON.parse(fs.readFileSync(path.resolve(__dirname,"../../../tests/fixtures/density-v0.6-vectors.json"),"utf8"));
    for(const opt of [false,true])for(const c of vectors.cases){
        const binary=compile(asset(`const sampler<f32> s=sampler(${c.expression})`,"p={s.pick(),0}"),opt);
        for(const r of c.rows){const out=generate(binary,r.seed);assert.equal(out[0].values[0].x,r.value_bits,c.name+" independent bits");
            assert.deepEqual(out[1].values,[{x:r.state_bits[0],y:r.state_bits[1]}],c.name+" independent stream");}
    }
    const cases=[
        ["f32","0.0 to 1.0 with density{0:1,1:1}","p={value,value}"],
        ["f32","0.0 to 1.0 with density{0:0,1:2}","p={value,value}"],
        ["f32","0.0 to 1.0 with density{0:2,1:0}","p={value,value}"],
        ["f32","1.0 to 3.0 with density{0:0,4:2}","p={value,value}"],
        ["f32","1.0 to 3.0 with density{0:2,4:0}","p={value,value}"],
        ["f32","0.0 to 4.0 with density{0:0,1:1,3:1,4:0}","p={value,value}"],
        ["f32","0.0 to 4.0 with density{0:0,1:0,2:1,3:0,4:0}","p={value,value}"],
        ["f32","0.0 to (1.1754943508222875e-38 * 1.1920928955078125e-7) with density{0.0:(1.1754943508222875e-38 * 1.1920928955078125e-7),(1.1754943508222875e-38 * 1.1920928955078125e-7):(1.1754943508222875e-38 * 1.1920928955078125e-7)}","p={value,value}"],
        ["f32","(-3.4028234663852886e38) to 3.4028234663852886e38 with density{(-3.4028234663852886e38):3.4028234663852886e38,3.4028234663852886e38:3.4028234663852886e38}","p={value,value}"],
        ["f32","14.0 to 26.0 with density{14.0:0.0,18.0:1.0,22.0:0.6,26.0:0.0}","p={value,value}"],
        ["f32","0.5 to 0.5 with density{0:0,1:0,2:1}","p={value,value}"],
        ["i32","(-2147483648) to 2147483647 with density{(-2147483648):1,2147483647:1}","p={value&65535,value>>16}"],
        ["i32","16777217 to 16777219 with density{16777217:1,16777218.0:1,16777219:1}","p={value&65535,value>>16}"],
        ["i32","(-2) to 2 with density{(-2):1,2:1}","p={value,value}"],
        ["color","#12345678 to #fedcba98 with density{0:0,0.5:1,1:0}","p={value.r,value.g} p={value.b,value.a} pixel(i%16,i/16,value)"],
        ["vec2","{-3,4} to {5,-6} with density{0:1,1:0}","p=value"],
    ];
    for(const [type,expression,publish] of cases)for(const count of [0,1,3,256]){
        const reference=compile(asset("",`for i in 0..${count}{value=${expression} ${publish}}`),false,"-reference");
        const expected=seeds.map(seed=>generate(reference,seed));
        for(const opt of [false,true])for(const declaration of [
            `const sampler<${type}> s=sampler(${expression})`,
            `palette specs=[sampler(${expression})] const sampler<${type}> s=specs[-1]`,
            `const list<sampler<${type}>> specs=[sampler(${expression})] const sampler<${type}> s=specs[0]`]){
            const binary=compile(asset(declaration,`values=s.pick(${count}) for i in 0..list.len(values){value=values[i] ${publish}}`),opt);
            for(let i=0;i<seeds.length;++i)assert.deepEqual(generate(binary,seeds[i]),expected[i],"density batch/palette/list equals manual draws");
        }
    }
    for(const opt of [false,true])for(const debug of [false,true])for(const compressed of [false,true]){
        const binary=base+"-example.iprop";
        execFileSync(tool("ipc"),[path.resolve(__dirname,"../../../examples/language/point-distributions.ips"),...(opt?[]:["--no-opt"]),debug?"--debug":"--strip",compressed?"--compress":"--no-compress","-o",binary]);
        for(const seed of seeds)generate(binary,seed);
        const pin=generate(binary,26);assert.deepEqual(pin.map(slot=>slot.values[0]),[
            {x:0x40000000,y:0x40000000},{x:0x41a085d5,y:0x41b6b498},{x:0x4766fe00,y:0x46f1ba00}],"independent example pin");
        fs.writeFileSync(base+".ipa",execFileSync(tool("ipdis"),[binary]));execFileSync(tool("ipa"),[base+".ipa","-o",base+"-restored.iprop"]);
        assert.deepEqual(generate(binary,26),generate(base+"-restored.iprop",26));
    }
    const snapshots=compile(asset("const distribution shape=density{0:0,1:2,2:0} const list<distribution> shapes=[weighted{0:1,1:1,2:1},shape,beta(2,3,0,2)]",
        "before=seed copy=echo(shapes[1]) p={seed==before,0} for i in 0..3{p={0.0 to 2.0 with shapes[i],0}} p={0.0 to 2.0 with copy,0}","echo(d:distribution)->distribution{return d}"),false,"-mixed");
    const manual=compile(asset("","p={1,0} p={0.0 to 2.0 with weighted{0:1,1:1,2:1},0} p={0.0 to 2.0 with density{0:0,1:2,2:0},0} p={0.0 to 2.0 with beta(2,3,0,2),0} p={0.0 to 2.0 with density{0:0,1:2,2:0},0}"),false,"-mixed-reference");
    for(const seed of seeds)assert.deepEqual(generate(snapshots,seed),generate(manual,seed));
    fs.writeFileSync(base+"-mixed.ipa",execFileSync(tool("ipdis"),[snapshots]));execFileSync(tool("ipa"),[base+"-mixed.ipa","-o",base+"-mixed-restored.iprop"]);
    assert.deepEqual(generate(snapshots,26),generate(base+"-mixed-restored.iprop",26));
    generate(compile(asset("input i32 countA=2 input i32 countB=3","for i in 0..countA{d=density{0:1,1:1} p={i,0}} for i in 0..countB{p={i,1}}")),26);
    const shared="density{"+Array.from({length:256},(_,i)=>`${i}:1`).join(",")+"}";
    generate(compile(asset(`const distribution shape=${shared}`,"before=seed for i in 0..10000{copy=shape} p={seed==before,0}"),true,"-sharing"),26);
    function trap(binary,message,variant=0){
        const png=base+"-trap.png",native=spawnSync(tool("ipgen"),[binary,"--variant",String(variant),"-o",png],{encoding:"utf8"});
        assert.equal(native.status,1,native.stderr);assert.match(native.stderr,message);assert.equal(fs.existsSync(png),false);
        assert.throws(()=>iprop.generate(new Uint8Array(fs.readFileSync(binary)),{variant}),message);++traps;
    }
    for(const body of ["d=density{0:(v-1),1:1}","d=density{v:1,0:1}","d=density{0:v,1:v}","d=density{0:float(v)/float(v),1:1}",
        "d=density{0:0,1:0,2:1} x=0.0 to 1.0 with d","d=density{0:0,1:1} x=2.0 to 2.0 with d",
        "if(v==1){d=density{0:0,1:1}} x=0.0 to 1.0 with d"])
        for(const opt of [false,true])trap(compile(asset("",body),opt),/nonnegative|strictly increasing|finite|positive total area|no probability|outside the distribution|not initialized/);
    function reject(data){fs.writeFileSync(base+"-bad.iprop",data);const native=spawnSync(tool("ipval"),[base+"-bad.iprop"],{encoding:"utf8"});
        assert.equal(native.status,1,native.stderr);assert.equal(native.signal,null);assert.throws(()=>iprop.generate(new Uint8Array(data),{}));++rejections;}
    const valid=compile(asset("const distribution shape=density{0.0:0.0,1.0:1.0} const sampler<f32> s=sampler(0.0 to 1.0 with shape) const list<distribution> shapes=[shape]","x=s.pick() items=shapes"),false,"-valid");
    const original=fs.readFileSync(valid),map=sections(original),pool=map.get(9);
    for(const [at,value] of [[pool,0],[pool,65537],[pool+4,0x00000105],[pool+4,0x00000206],[pool+4,0x00000285],
        [pool+8,2],[pool+8,0x10000],[pool+12,0x7fc00000],[pool+16,0xbf800000],[pool+24,0],[pool+28,0],[pool+28,0x7f800000]]){
        const bad=Buffer.from(original);bad.writeUInt32LE(value,at);reject(bad);
    }
    for(const id of [7,8])for(const family of [4,255]){const bad=Buffer.from(original);bad[map.get(id)+(id===7?8:16)]=family;reject(bad);}
    for(const id of [7,8]){const bad=Buffer.from(original);bad.writeUInt16LE(65535,map.get(id)+(id===7?9:17));reject(bad);}
    for(let minor=1;minor<=5;++minor){const bad=Buffer.from(original);bad.writeUInt16LE(minor,6);reject(bad);}
    fs.writeFileSync(base+"-dead.ipa","meta: size=1x1\nfn draw ( i32, i32 ) -> ( ):\nreturn\ndistribution.begin 5\ndistribution.append 0\ndistribution.end\nendfn\n");
    execFileSync(tool("ipa"),[base+"-dead.ipa","-o",base+"-dead.iprop"]);const dead=fs.readFileSync(base+"-dead.iprop"),ins=instructions(dead);
    for(const i of [1,2,3]){const bad=Buffer.from(dead);bad[ins[i]+1]=1;reject(bad);}
    for(const [i,operand] of [[1,0],[1,261],[2,4],[3,1]]){const bad=Buffer.from(dead);bad.writeUInt16LE(operand,ins[i]+2);reject(bad);}
    const many="density{"+Array.from({length:256},(_,i)=>`${i}:1`).join(",")+"}";
    generate(compile(asset(`const distribution shape=${many}`,`runtime=${many} p={0.0 to 255.0 with shape,0.0 to 255.0 with runtime}`),false,"-max"),26);
    const quota=compile(asset("",`for i in 0..9000{d=${many}}`),false,"-quota"),quotaBytes=fs.readFileSync(quota);
    quotaBytes.writeBigUInt64LE(100000000n,sections(quotaBytes).get(1)+24);fs.writeFileSync(quota,quotaBytes);trap(quota,/generation data exceeds 64 MiB/);
    generate(valid,26);return {comparisons,traps,rejections,vectors:vectors.cases.reduce((n,c)=>n+c.rows.length,0)};
}
module.exports={run};
