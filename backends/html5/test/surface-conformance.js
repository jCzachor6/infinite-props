"use strict";
const {renderSource, renderArguments} = require("./authored-fixtures.js");

const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path");
const crypto = require("node:crypto"), {execFileSync, spawnSync} = require("node:child_process");
const {decodePng} = require("./png.js");
const {sections, instructions} = require("./sampler-conformance.js");
const repo = path.resolve(__dirname,"../../..");
const sha = bytes => crypto.createHash("sha256").update(bytes).digest("hex");
const bits = result => result.outputs.map(slot => ({name:slot.name,kind:["point","rect","circle"][slot.kind],list:slot.isList,values:slot.valueBits}));
function canonical(value) {
    if(Array.isArray(value)) return value.map(canonical);
    return value && typeof value === "object" ? Object.fromEntries(Object.keys(value).sort().map(key=>[key,canonical(value[key])])) : value;
}
function run(iprop,tool,directory) {
    const base=path.join(directory,"surfaces"), seeds=[0,1,7,26,0xffffffff];
    let comparisons=0,traps=0,rejections=0,pins=0;
    const preserved=JSON.parse(fs.readFileSync(path.join(repo,"tests/fixtures/frozen-format-v0.6-stage7.json"),"utf8"));
    const panel=preserved.cases.find(c=>c.source.endsWith("direct-panel-reference"));
    function compile(source,opt=true,suffix="") {
        const input=base+suffix+".ips",binary=base+suffix+".iprop";fs.writeFileSync(input,source);
        execFileSync(tool("ipc"),[input,...(opt?[]:["--no-opt"]),"--no-compress","-o",binary]);return binary;
    }
    function generate(binary,seed=26,options={}) {
        const png=base+".png",flags=[];
        if(options.sizeX)flags.push("--size",`${options.sizeX}x${options.sizeY}`);
        for(const [name,value] of Object.entries(options.uniforms || {})) flags.push("--uniform",name+"="+value);
        const native=JSON.parse(execFileSync(tool("ipgen"),[binary,"--seed",String(seed),"--float-bits",...flags,"-o",png],{encoding:"utf8"}));
        const image=decodePng(fs.readFileSync(png)),wasm=iprop.generate(new Uint8Array(fs.readFileSync(binary)),{seed,...options});
        assert.deepEqual(bits(wasm),native.outputs,"surface output bits");
        assert.deepEqual(Buffer.from(wasm.data),Buffer.from(image.data),"surface RGBA");
        assert.deepEqual({x:wasm.width,y:wasm.height},native.size,"surface dimensions");
        ++comparisons;return {rgba_sha256:sha(image.data),outputs_sha256:sha(JSON.stringify(canonical(native.outputs))),outputs:native.outputs,data:image.data};
    }
    // Expected pixels/outputs come from unmodified stage-7 tools, not the new
    // capture/compositor. Every compiler configuration and IPA uses those pins.
    for(const opt of [false,true])for(const debug of [false,true])for(const compressed of [false,true]) {
        const binary=base+"-panel.iprop";
        execFileSync(tool("ipc"),[path.join(repo,"examples/language/captured-panels.ips"),...(opt?[]:["--no-opt"]),debug?"--debug":"--strip",compressed?"--compress":"--no-compress","-o",binary]);
        for(const expected of panel.generations) {
            const actual=generate(binary,expected.seed);
            assert.equal(actual.rgba_sha256,expected.rgba_sha256,"preserved direct panel pixels");
            assert.equal(actual.outputs_sha256,expected.outputs_sha256,"preserved direct panel outputs/RNG");++pins;
        }
        generate(binary,26,{sizeX:16,sizeY:48});
        fs.writeFileSync(base+".ipa",execFileSync(tool("ipdis"),[binary]));execFileSync(tool("ipa"),[base+".ipa","-o",base+"-restored.iprop"]);
        assert.deepEqual(generate(binary),generate(base+"-restored.iprop"),"surface IPA/IPDIS");
    }
    const part=preserved.cases.find(c=>c.source.endsWith("part-targets"));
    const tree=path.join(directory,"surface-tree");fs.mkdirSync(tree);
    function captured(text) {
        // Re-author the archived selection as an ordinary declared input.
        text=text.replace(/\bvariants\s+\d+/g,"input i32 v=0").replace(/\barchetypes\s+\d+/g,"")
            .replace(/\.draw\(seed,0,0,/g,".draw(seed,");
        const start=text.indexOf("draw{");return text.slice(0,start)+"draw{layer image{"+text.slice(start+5,-2)+"}surface.paint(image)}}";}
    for(const opt of [false,true]) {
        for(const [name,text] of Object.entries(part.source_texts))fs.writeFileSync(path.join(tree,name),captured(text));
        const binary=path.join(tree,"tree.iprop");execFileSync(tool("ipc"),[path.join(tree,"root.ips"),...(opt?[]:["--no-opt"]),"-o",binary]);
        for(const expected of part.generations) {
            const actual=generate(binary,expected.seed,{sizeX:expected.arguments.sizeX,sizeY:expected.arguments.sizeY,uniforms:{v:expected.arguments.variant}});
            assert.equal(actual.rgba_sha256,expected.rgba_sha256,"preserved part/D4/scale pixels");
            assert.equal(actual.outputs_sha256,expected.outputs_sha256,"preserved part outputs/isolation");++pins;
        }
    }
    const cases=[
        ["layer image{pixel(0,0,#fff)}pixel(1,1,#0000ff)","pixel(1,1,#0000ff)"],
        ["layer image{}surface.paint(image)",""],
        ["layer first{pixel(0,0,#929ca8)}copy=echo(first) layer first{pixel(1,0,#343a42)}surface.paint(first)pixel(0,0,#fff)surface.paint(copy)","pixel(0,0,#929ca8)pixel(1,0,#343a42)"],
        ["layer outer{layer inner{pixel(2,0,#c09060)}surface.paint(inner)}surface.paint(outer)","pixel(2,0,#c09060)"],
        ["for i in 0..3{layer image{pixel(i,0,#ff0000)if(i==0){continue}if(i==1){break}}}surface.paint(image)pixel(3,0,#0000ff)","pixel(1,0,#ff0000)pixel(3,0,#0000ff)"],
        ["for i in 0..2{layer outer{pixel(i,0,#00ff00)layer inner{pixel(2,0,#fff)continue}}}surface.paint(outer)surface.paint(inner)","pixel(1,0,#00ff00)pixel(2,0,#fff)"],
        ["abandon()pixel(3,0,#0000ff)","pixel(3,0,#0000ff)"],
        ["surface.paint(blend=surface.legacy,image=makeImage())", "w=1 to 3 p={w,0}pixel(w,0,#929ca8)"],
        ["layer image{rect(0,0,4,4,#929ca8)line(0,0,3,3,1,#343a42)oval(1,1,1,1,#c09060)curve({0,3},{1,0},{3,3},1.0,#fff)poly([{0,0},{1,0},{0,1}],#123456)weather(7,20)}surface.paint(image)",
            "rect(0,0,4,4,#929ca8)line(0,0,3,3,1,#343a42)oval(1,1,1,1,#c09060)curve({0,3},{1,0},{3,3},1.0,#fff)poly([{0,0},{1,0},{0,1}],#123456)weather(7,20)"],
        ["pixel(0,0,#11223380)pixel(1,0,#112233)layer image{pixel(0,0,#ff000080)pixel(1,0,#0000ff00)}surface.paint(image)","pixel(0,0,#11223380)pixel(1,0,#112233)"],
    ];
    const helpers="echo(image:layer)->layer{return image} abandon(){layer abandoned{pixel(0,1,#fff)return;}} makeImage()->layer{layer image{w=1 to 3 p={w,0}pixel(w,0,#929ca8)}return image}";
    const asset=(body,extra="")=>"asset t{size 4x4 output list point p output point state "+extra+"draw{"+body+"state={seed&65535,seed>>16}}}";
    for(const [body,reference] of cases) {
        const oracle=compile(asset(reference),false,"-reference");
        for(const opt of [false,true]) {
            const binary=compile(asset(body,helpers),opt);
            for(const seed of seeds)assert.deepEqual(generate(binary,seed),generate(oracle,seed),"surface semantics equal explicit reference");
        }
    }
    generate(compile("asset t{size 1024x1024 echo(image:layer)->layer{return image}draw{layer image{}for i in 0..10000{copy=echo(image)}}}",false,"-shared"));
    function trap(binary,message) {
        const png=base+"-trap.png",native=spawnSync(tool("ipgen"),[binary,"-o",png],{encoding:"utf8"});
        assert.equal(native.status,1,native.stderr);assert.equal(native.signal,null);assert.match(native.stderr,message);assert.equal(fs.existsSync(png),false);
        assert.throws(()=>iprop.generate(new Uint8Array(fs.readFileSync(binary)),{}),message);++traps;
    }
    for(const opt of [false,true])for(const source of ["asset t{size 1x1 draw{if(false){layer image{}}surface.paint(image)}}",
        "asset t{input i32 v=0 size 1x1 draw{layer image{x=1/(v-v)}}}","asset t{size 2048x2048 draw{layer image{}surface.paint(image)}}"])
        trap(compile(source,opt),/layer is unassigned|divide by zero|fuel exhausted/);
    function assembly(text) {fs.writeFileSync(base+".ipa",text);execFileSync(tool("ipa"),[base+".ipa","-o",base+"-asm.iprop"]);return base+"-asm.iprop";}
    trap(assembly("meta: size=1x1\ntypes: layer\nfn draw (  ) -> ( ):\ncall nested\nreturn\nendfn\nfn nested ( ) -> ( ):\nlocals: layer\nlayer.begin 0\ncall nested\nend\nreturn\nendfn\n"),/surface scope depth exceeds 32/);
    trap(assembly("meta: size=1024x1024 fuel=100000000\ntypes: layer\nfn draw (  ) -> ( ):\nlocals: layer\n"+"layer.begin 0\n".repeat(16)+"end\n".repeat(16)+"return\nendfn\n"),/64 MiB/);
    function reject(data) {const binary=base+"-bad.iprop";fs.writeFileSync(binary,data);const native=spawnSync(tool("ipval"),[binary],{encoding:"utf8"});
        assert.equal(native.status,1,native.stderr);assert.equal(native.signal,null);assert.throws(()=>iprop.generate(new Uint8Array(data),{}));++rejections;}
    const valid=Buffer.from(fs.readFileSync(compile("asset t{input i32 v=0 size 1x1 draw{layer image(alpha=surface.legacy){}surface.paint(image,blend=surface.legacy)}}",false,"-wire")));
    const parsed=instructions(valid),types=sections(valid).get(6);
    for(const [at,value] of [[types+4,255],[types+5,0],[types+6,1],[types+7,1]]){const bad=Buffer.from(valid);bad[at]=value;reject(bad);}
    for(const at of parsed)if(valid[at]===0xc6) {
        for(const operand of [0,2,65535]){const bad=Buffer.from(valid);bad.writeUInt16LE(operand,at+2);reject(bad);}
        const bad=Buffer.from(valid);bad[at+1]=3;reject(bad);
    } else if(valid[at]===0xc7) {
        const bad=Buffer.from(valid);bad.writeUInt16LE(3,at+2);reject(bad);
        const aux=Buffer.from(valid);aux[at+1]=1;reject(aux);
    }
    const old=Buffer.from(valid);old.writeUInt16LE(5,6);reject(old);
    // Dead-code version gates without a Types section, and a wrong paint type.
    for(const opcode of [0xc6,0xc7]) {
        const binary=assembly("meta: size=1x1\nfn draw (  ) -> ( ):\nreturn\nnop\nendfn\n");
        const data=Buffer.from(fs.readFileSync(binary)),dead=instructions(data).find(i=>data[i]===0);
        data[dead]=opcode;
        if(opcode===0xc7)data.writeUInt16LE(3,dead+2); // unknown mode, including unreachable code
        reject(data);
    }
    const wrong=Buffer.from(valid),paint=instructions(wrong).find(i=>wrong[i]===0xc7),get=instructions(wrong).find(i=>wrong[i]===0x13);
    assert.ok(paint && get);wrong.writeUInt16LE(0,get+2);reject(wrong);
    generate(compile("asset t{input i32 v=0 size 1x1 draw{layer image{pixel(0,0,#fff)}surface.paint(image)}}",true,"-recovery"));
    return {comparisons,traps,rejections,pins};
}
module.exports={run};
