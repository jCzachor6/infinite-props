"use strict";
const {renderSource, renderArguments} = require("./authored-fixtures.js");
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),crypto=require("node:crypto");
const {execFileSync,spawnSync}=require("node:child_process");
const {decodePng}=require("./png.js");
const {sections,instructions}=require("./sampler-conformance.js");
const repo=path.resolve(__dirname,"../../.."),sha=data=>crypto.createHash("sha256").update(data).digest("hex");
const outputBits=result=>result.outputs.map(slot=>({name:slot.name,kind:["point","rect","circle"][slot.kind],list:slot.isList,values:slot.valueBits}));

function run(iprop,tool,directory) {
    const base=path.join(directory,"source-over"),seeds=[0,1,7,26,0xffffffff];
    let comparisons=0,pins=0,traps=0,rejections=0;
    function compile(source,opt=true,suffix="") {
        const input=base+suffix+".ips",binary=base+suffix+".iprop";fs.writeFileSync(input,source);
        execFileSync(tool("ipc"),[input,...(opt?[]:["--no-opt"]),"--no-compress","-o",binary]);return binary;
    }
    function generate(binary,seed=26,options={}) {
        const png=base+".png",flags=[];
        if(options.sizeX)flags.push("--size",`${options.sizeX}x${options.sizeY}`);
        for(const [name,value] of Object.entries(options.uniforms || {})) flags.push("--uniform",name+"="+value);
        const native=JSON.parse(execFileSync(tool("ipgen"),[binary,"--seed",String(seed),"--float-bits",...flags,"-o",png],{encoding:"utf8"}));
        const data=decodePng(fs.readFileSync(png)).data,wasm=iprop.generate(new Uint8Array(fs.readFileSync(binary)),{seed,...options});
        assert.deepEqual(Buffer.from(wasm.data),Buffer.from(data),"source-over native/WASM RGBA");
        assert.deepEqual(outputBits(wasm),native.outputs,"source-over output bits/RNG");
        assert.deepEqual({x:wasm.width,y:wasm.height},native.size);++comparisons;
        return {data:Buffer.from(data),outputs:native.outputs,fuel:native.fuel};
    }
    const panel=JSON.parse(fs.readFileSync(path.join(repo,"tests/fixtures/source-over-panels-v0.6.json"),"utf8"));
    // Independent rational RGBA plus preserved-tool RNG pins, in every compiler configuration.
    for(const opt of [false,true])for(const debug of [false,true])for(const compressed of [false,true]) {
        const binary=base+"-panel.iprop";
        execFileSync(tool("ipc"),[path.join(repo,"examples/language/source-over-panels.ips"),...(opt?[]:["--no-opt"]),debug?"--debug":"--strip",compressed?"--compress":"--no-compress","-o",binary]);
        for(const expected of panel.generations) {
            const actual=generate(binary,expected.seed);
            assert.equal(sha(actual.data),expected.rgba_sha256);
            assert.deepEqual(actual.data,Buffer.from(expected.rgba_base64,"base64"));
            assert.deepEqual(actual.outputs.map(s=>[s.values[0].x,s.values[0].y]),expected.output_bits);++pins;
        }
        fs.writeFileSync(base+".ipa",execFileSync(tool("ipdis"),[binary]));execFileSync(tool("ipa"),[base+".ipa","-o",base+"-restored.iprop"]);
        const restored=generate(base+"-restored.iprop"),actual=generate(binary);
        assert.deepEqual(restored,actual,"source-over IPA/IPDIS");
    }
    // All 65,536 alpha pairs, calculated separately with Python Fraction.
    const grid=compile("asset t{size 256x256 draw{layer image(alpha=surface.source_over){for y in 0..256{for x in 0..256{pixel(x,y,rgba(201,4,172,y))pixel(x,y,rgba(17,213,88,x))}}}surface.paint(image,blend=surface.source_over)}}",false,"-grid");
    fs.writeFileSync(base+".ipa",execFileSync(tool("ipdis"),[grid],{encoding:"utf8"}).replace(/fuel=\d+/,"fuel=10000000"));
    execFileSync(tool("ipa"),[base+".ipa","-o",base+"-grid-full.iprop"]);
    assert.deepEqual(generate(base+"-grid-full.iprop").data,fs.readFileSync(path.join(repo,"tests/fixtures/source-over-v0.6.rgba")));++pins;

    // Opaque stage-8 part pins freeze mapping, clipping, scales and raw outputs.
    // Applying half-alpha to those disjoint pixels gives the independent oracle.
    const frozen=JSON.parse(fs.readFileSync(path.join(repo,"tests/fixtures/frozen-format-v0.6-stage8.json"),"utf8"));
    const part=frozen.cases.find(c=>c.source.endsWith("opaque-part-mapping-oracle"));
    const tree=path.join(directory,"source-over-tree");fs.mkdirSync(tree);
    for(const opt of [false,true]) {
        for(const [name,text] of Object.entries(part.source_texts)) {
            let next=renderSource(text);
            if(name==="root.ips") {const start=next.indexOf("draw{");next=next.slice(0,start)+"draw{layer image(alpha=surface.source_over){"+next.slice(start+5,-2)+"}surface.paint(image,blend=surface.source_over)}}";}
            else next=next.replace(/#ff0000\b/g,"#ff000080").replace(/#0000ff\b/g,"#0000ff80").replace(/#112233\b/g,"#11223380");
            fs.writeFileSync(path.join(tree,name),next);
        }
        const binary=path.join(tree,"tree.iprop");execFileSync(tool("ipc"),[path.join(tree,"root.ips"),...(opt?[]:["--no-opt"]),"-o",binary]);
        for(const expected of part.generations) {
            const actual=generate(binary,expected.seed,renderArguments(expected.arguments)),rgba=Buffer.from(expected.rgba_base64,"base64");
            for(let at=3;at<rgba.length;at+=4)if(rgba[at])rgba[at]=128;
            assert.deepEqual(actual.data,rgba,"frozen D4/scale geometry with inherited alpha");
            assert.deepEqual(actual.outputs,expected.outputs,"frozen part output bits/isolation");++pins;
        }
    }
    const cases=[
        "layer image(alpha=surface.source_over){pixel(0,0,#11223380)pixel(0,0,#ff000080)}surface.paint(image,blend=surface.source_over)",
        "layer image(alpha=surface.source_over){layer nested{mark(0)}surface.paint(nested);mark(1)}surface.paint(image,blend=surface.source_over);mark(2)",
        "layer image(alpha=surface.source_over){early()mark(0);for i in 0..3{layer skipped(alpha=surface.legacy){if(i==0){continue}break;}}mark(1)}surface.paint(image,blend=surface.source_over);mark(2)",
        "layer image(alpha=surface.source_over){pixel(0,0,#11223380)}surface.paint(image);surface.paint(image,blend=surface.source_over);mark(2)",
        "pixel(0,0,#11223380)layer image{}surface.paint(image,blend=surface.source_over)",
        "layer image(alpha=surface.source_over){rect(0,0,4,2,#11223380)weather(7,20)}surface.paint(image,blend=surface.source_over)",
        "layer image(alpha=surface.source_over){pixel(0,0,#11223380)pixel(0,0,#11223380)pixel(0,0,#ff000080)pixel(1,0,#11223380)layer group{pixel(1,0,#11223380)pixel(1,0,#ff000080)}surface.paint(group)}surface.paint(image,blend=surface.source_over)",
        "constValue=surface.legacy newValue=surface.source_over state={constValue,newValue}",
    ];
    for(const opt of [false,true])for(const body of cases)for(const seed of seeds)
        generate(compile("asset t{size 4x2 output point state mark(x:i32){pixel(x,0,#11223380)} early(){layer gone(alpha=surface.legacy){pixel(3,0,#fff)return;}}draw{"+body+"}}",opt),seed);
    // Mode selection has no RNG or extra fuel. Primitives continue to share
    // rasterization; fully opaque source writes match the frozen legacy path.
    const shapes="rect(0,0,2,2,#929ca8)line(2,0,4,2,1,#fff)oval(3,3,1,1,#c09060)poly(#929ca8,0,2,2,2,2,4)";
    for(const opt of [false,true]) {
        const legacy=generate(compile("asset t{size 5x5 draw{layer image(alpha=surface.legacy){"+shapes+"}surface.paint(image,blend=surface.legacy)}}",opt));
        const over=generate(compile("asset t{size 5x5 draw{layer image(alpha=surface.source_over){"+shapes+"}surface.paint(image,blend=surface.source_over)}}",opt));
        assert.deepEqual(over,legacy,"opaque rasterization and mode fuel");
    }
    function trap(binary,message) {
        const png=base+"-trap.png",native=spawnSync(tool("ipgen"),[binary,"-o",png],{encoding:"utf8"});
        assert.equal(native.status,1,native.stderr);assert.equal(native.signal,null);assert.match(native.stderr,message);assert.equal(fs.existsSync(png),false);
        assert.throws(()=>iprop.generate(new Uint8Array(fs.readFileSync(binary)),{}),message);++traps;
    }
    for(const opt of [false,true])for(const source of ["asset t{input i32 v=0 size 1x1 draw{layer image(alpha=surface.source_over){x=1/(v-v)}}}",
        "asset t{size 2048x2048 draw{layer image(alpha=surface.source_over){}surface.paint(image,blend=surface.source_over)}}"])
        trap(compile(source,opt),/divide by zero|fuel exhausted/);
    function reject(data) {
        const binary=base+"-bad.iprop";fs.writeFileSync(binary,data);const native=spawnSync(tool("ipval"),[binary],{encoding:"utf8"});
        assert.equal(native.status,1,native.stderr);assert.equal(native.signal,null);assert.throws(()=>iprop.generate(new Uint8Array(data),{}));++rejections;
    }
    const valid=Buffer.from(fs.readFileSync(compile("asset t{input i32 v=0 size 1x1 draw{layer image(alpha=surface.source_over){}surface.paint(image,blend=surface.source_over)}}",false,"-wire")));
    for(const at of instructions(valid))if(valid[at]===0xc6){const bad=Buffer.from(valid);bad[at+1]=3;reject(bad);}
    else if(valid[at]===0xc7)for(const mode of [3,257,65535]){const bad=Buffer.from(valid);bad.writeUInt16LE(mode,at+2);reject(bad);}
    const old=Buffer.from(valid);old.writeUInt16LE(5,6);reject(old);
    // Mode 2 is valid even when unreachable; the pre-0.6 gate is still enforced.
    fs.writeFileSync(base+".ipa","meta: size=1x1\ntypes: layer\nfn draw (  ) -> ( ):\nlocals: layer\nreturn\nlayer.begin 0 2\nend\nendfn\n");
    execFileSync(tool("ipa"),[base+".ipa","-o",base+"-dead.iprop"]);generate(base+"-dead.iprop");
    const dead=Buffer.from(fs.readFileSync(base+"-dead.iprop"));dead.writeUInt16LE(5,6);reject(dead);
    assert.ok(sections(valid).has(6));
    return {comparisons,pins,traps,rejections,alpha_pairs:65536};
}
module.exports={run};
