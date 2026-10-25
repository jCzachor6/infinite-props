"use strict";

const assert = require("node:assert/strict");
const {execFileSync, spawnSync} = require("node:child_process");
const fs = require("node:fs");
const path = require("node:path");
const {decodePng} = require("./png.js");

function bits(outputs) {
    return outputs.map(s => ({name:s.name, kind:["point","rect","circle"][s.kind], list:s.isList, values:s.valueBits}));
}

function run(iprop, tool, directory) {
    const catalogue = JSON.parse(execFileSync(tool("ipc"), ["--describe-language"], {encoding:"utf8"}));
    const signatures = catalogue.builtins.flatMap(b => b.overloads.filter(o => o.expansion));
    assert.equal(signatures.length, 20, "all requested vector overloads are exported");
    // The scalar references are explicit, independent of the compiler's expansion table.
    const pairs = [
        ["rect(p,size,tint)", "rect(p.x,p.y,size.x,size.y,tint)"],
        ["rect(p,size.x,size.y,tint)", "rect(p.x,p.y,size.x,size.y,tint)"],
        ["rect(p.x,p.y,size,tint)", "rect(p.x,p.y,size.x,size.y,tint)"],
        ["line(p,size,2,tint)", "line(p.x,p.y,size.x,size.y,2,tint)"],
        ["oval(p,size,tint)", "oval(p.x,p.y,size.x,size.y,tint)"],
        ["oval(p,size.x,size.y,tint)", "oval(p.x,p.y,size.x,size.y,tint)"],
        ["oval(p.x,p.y,size,tint)", "oval(p.x,p.y,size.x,size.y,tint)"],
        ...["noise","fault","noise.grad","noise.cell"].map(name =>
            [`samples={${name}(p,seed),0}`, `samples={${name}(p.x,p.y,seed),0}`]),
        ...["noise.fbm","noise.ridge"].map(name =>
            [`samples={${name}(p,4,seed),0}`, `samples={${name}(p.x,p.y,4,seed),0}`]),
        ["samples={warp(p,0.5,seed),0}", "samples={warp(p.x,p.y,0.5,seed),0}"],
        ["pixel(p,tint)", "pixel(int(p.x),int(p.y),tint)"],
        ["samples=cell.jitter(p,seed)", "samples=cell.jitter(int(p.x),int(p.y),seed)"],
        ["samples={dither(0.4,p),0}", "samples={dither(0.4,int(p.x),int(p.y)),0}"],
        ["surface.paint(mask.offset(region,p),tint,blend=surface.source_over)",
         "surface.paint(mask.offset(region,int(p.x),int(p.y)),tint,blend=surface.source_over)"],
        ["samples={atan2(point()),0}", "a=point() samples={atan2(a.y,a.x),0}"],
        ["samples=lerp(t=unit(),b=point(),a=point())",
         "a=point() b=point() t=unit() samples={lerp(a.x,b.x,t),lerp(a.y,b.y,t)}"],
    ];
    assert.equal(pairs.length, 20);
    const wrap = body => `asset overloads {size 24x24 input f32 bias=-0.75 input color tint=#12345678
      output list point samples output point following
      point()->vec2{return {unit()*8,unit()*8}}
      draw{p={2.25+unit()+bias,3.25+unit()} size={5.5+unit(),6.5+unit()}
        layer image(alpha=surface.source_over){rect(1,1,5,6,#abcdef80)} region=mask.from(image)
        ${body} following={unit(),unit()}}}`;
    const sources = [0,1].map(i => {
        const file = path.join(directory, `vector-overloads-${i}.ips`);
        fs.writeFileSync(file, wrap(pairs.map(pair => pair[i]).join("\n")));
        return file;
    });
    let comparisons = 0, traps = 0;
    const generate = (file, seed, resized) => {
        const png = path.join(directory, "vector-overloads.png");
        const args = [file,"--seed",String(seed),"--float-bits","-o",png];
        const options = {seed};
        if (resized) {
            args.push("--size","32x20","--uniform","bias=-1.25","--uniform","tint=#ff00ff80");
            Object.assign(options, {sizeX:32,sizeY:20,uniforms:{bias:-1.25,tint:"#ff00ff80"}});
        }
        const native = JSON.parse(execFileSync(tool("ipgen"), args, {encoding:"utf8"}));
        const image = decodePng(fs.readFileSync(png));
        const actual = iprop.generate(new Uint8Array(fs.readFileSync(file)), options);
        assert.equal(actual.width, image.width);
        assert.equal(actual.height, image.height);
        assert.deepEqual(Buffer.from(actual.data), Buffer.from(image.data), "vector overload native/WASM RGBA");
        assert.deepEqual(bits(actual.outputs), native.outputs, "vector overload native/WASM bits");
        ++comparisons;
        return {rgba:Buffer.from(actual.data), outputs:bits(actual.outputs)};
    };
    for (const optimize of [false,true]) for (const debug of [false,true]) for (const compress of [false,true]) {
        const binaries = sources.map((source,i) => {
            const file = path.join(directory, `overloads-${optimize}-${debug}-${compress}-${i}.iprop`);
            execFileSync(tool("ipc"), [source,"-o",file,...(optimize?[]:["--no-opt"]),
                debug?"--debug":"--strip",compress?"--compress":"--no-compress"]);
            return file;
        });
        for (const seed of [0,1,7,26,0xffffffff]) for (const resized of [false,true])
            assert.deepEqual(generate(binaries[0],seed,resized), generate(binaries[1],seed,resized),
                "vector/scalar exact images, bits and following RNG draws");
        const assembly = binaries[0] + ".ipa", restored = binaries[0] + "-restored.iprop";
        fs.writeFileSync(assembly, execFileSync(tool("ipdis"), [binaries[0]]));
        execFileSync(tool("ipa"), [assembly,"-o",restored]);
        assert.deepEqual(generate(restored,7,false),generate(binaries[1],7,false),"vector overload IPA roundtrip");
    }
    for (const [vector,scalar,message] of [
        ["noise.grad(p,seed)","noise.grad(p.x,p.y,seed)","noise coordinate out of range"],
        ["noise.fbm(p,4,seed)","noise.fbm(p.x,p.y,4,seed)","noise coordinate out of range"],
        ["cell.jitter(p,seed)","cell.jitter(int(p.x),int(p.y),seed)","cell index is not representable as f32"],
        ["pixel(p,#fff)","pixel(int(p.x),int(p.y),#fff)","coordinate out of range"],
    ]) for (const call of [vector,scalar]) {
        const source = path.join(directory,"overload-trap.ips"), file = source + ".iprop";
        const png = source + ".png";
        fs.writeFileSync(source,`asset t{size 1x1 input f32 x=2147483648.0 draw{p={x,0} ${call}}}`);
        execFileSync(tool("ipc"),[source,"-o",file]);
        const native = spawnSync(tool("ipgen"),[file,"-o",png],{encoding:"utf8"});
        assert.equal(native.status,1);
        assert.equal(native.signal,null);
        assert.match(native.stderr,new RegExp(message));
        assert.equal(fs.existsSync(png),false);
        assert.throws(()=>iprop.generate(new Uint8Array(fs.readFileSync(file)),{}),new RegExp(message));
        ++traps;
    }
    return {signatures:signatures.length,comparisons,traps};
}

module.exports = {run};
