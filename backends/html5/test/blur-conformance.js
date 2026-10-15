"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path");
const {execFileSync, spawnSync} = require("node:child_process");
const {decodePng} = require("./png.js");
const {sections, instructions} = require("./sampler-conformance.js");

// Independent full 2D convolution with host Math.exp, no runtime kernel or
// horizontal intermediate. Fixed impulse/edge pins below also guard the oracle.
function reference(pixels, width, height, sigma, mask) {
    if (sigma === 0) return Buffer.from(pixels.flatMap(c => mask ? (c[3] ? [255,255,255,c[3]] : [0,0,0,0]) : c));
    const radius = Math.ceil(3*sigma), weights = [];
    for (let j=-radius; j<=radius; ++j) weights.push(Math.exp(-j*j/(2*sigma*sigma)));
    const total = weights.reduce((a,b)=>a+b,0);
    for (let j=0;j<weights.length;++j) weights[j]/=total;
    const output = [], byte = x => Math.floor(Math.max(0,Math.min(255,x))+0.5);
    for(let y=0;y<height;++y)for(let x=0;x<width;++x) {
        const sum=[0,0,0,0];
        for(let dy=-radius;dy<=radius;++dy)for(let dx=-radius;dx<=radius;++dx) {
            const sx=Math.max(0,Math.min(width-1,x+dx)),sy=Math.max(0,Math.min(height-1,y+dy));
            const c=pixels[sy*width+sx],w=weights[dy+radius]*weights[dx+radius];
            for(let k=0;k<3;++k)sum[k]+=c[k]*c[3]*w;
            sum[3]+=c[3]*w;
        }
        const a=byte(sum[3]);
        output.push(...(a ? [mask?255:byte(sum[0]/sum[3]),mask?255:byte(sum[1]/sum[3]),mask?255:byte(sum[2]/sum[3]),a] : [0,0,0,0]));
    }
    return Buffer.from(output);
}

function run(iprop,tool,directory) {
    const base=path.join(directory,"blur");
    let comparisons=0,pins=0,traps=0,rejections=0;
    function compile(source,flags=[]) {
        fs.writeFileSync(base+".ips",source);
        execFileSync(tool("ipc"),[base+".ips","--no-compress",...flags,"-o",base+".iprop"]);
        return Buffer.from(fs.readFileSync(base+".iprop"));
    }
    function assemble(text) {
        fs.writeFileSync(base+".ipa",text);execFileSync(tool("ipa"),[base+".ipa","-o",base+".iprop"]);
        return Buffer.from(fs.readFileSync(base+".iprop"));
    }
    function generate(data,expected,options={}) {
        fs.writeFileSync(base+".iprop",data);
        const flags=["--seed",String(options.seed ?? 26),"--float-bits"];
        if(options.sizeX)flags.push("--size",`${options.sizeX}x${options.sizeY}`);
        const native=JSON.parse(execFileSync(tool("ipgen"),[base+".iprop",...flags,"-o",base+".png"],{encoding:"utf8"}));
        const rgba=Buffer.from(decodePng(fs.readFileSync(base+".png")).data);
        const wasm=iprop.generate(new Uint8Array(data),{seed:26,...options});
        assert.deepEqual(Buffer.from(wasm.data),rgba,"blur native/WASM RGBA");
        assert.deepEqual({x:wasm.width,y:wasm.height},native.size,"blur dimensions");
        assert.deepEqual(wasm.outputs.map(s=>({name:s.name,kind:["point","rect","circle"][s.kind],list:s.isList,values:s.valueBits})),native.outputs,"blur output bits");
        ++comparisons;
        if(expected) {assert.deepEqual(rgba,expected,"blur independent 2D reference");++pins;}
        return {rgba,outputs:native.outputs};
    }
    function authored(pixels,w,h,sigma,mask) {
        const draw=pixels.map((c,i)=>`pixel(${i%w},${Math.floor(i/w)},#${c.map(x=>x.toString(16).padStart(2,"0")).join("")})`).join(";");
        return `asset t{size ${w}x${h} draw{layer image(alpha=surface.source_over){${draw}}
            surface.paint(${mask?"mask.from(image)":"image"}.blur(sigma=${sigma}),${mask?"#ffffff,":""}blend=surface.source_over)}}`;
    }
    const impulse=Array.from({length:7},(_,i)=>i===3?[255,80,40,255]:[0,0,0,0]);
    const oracle=reference(impulse,7,1,1,false);
    assert.deepEqual(Array.from(oracle).filter((_,i)=>i%4===3),[1,14,62,102,62,14,1]);
    generate(compile(authored(impulse,7,1,1,false)),oracle);
    const edge=Array.from({length:9},(_,i)=>i===0?[255,255,255,255]:[0,0,0,0]);
    const edgeOracle=reference(edge,3,3,1,true);
    assert.deepEqual(Array.from(edgeOracle).filter((_,i)=>i%4===3),[125,54,10,54,23,4,10,4,1]);
    generate(compile(authored(edge,3,3,1,true)),edgeOracle);

    for(const [w,h] of [[1,1],[1,7],[7,1],[3,5],[5,17]])for(const sigma of [0,0.25,0.75,1,2,64]) {
        if(sigma===64&&w*h>20)continue;
        const pixels=Array.from({length:w*h},(_,i)=>{
            const x=i%w,y=Math.floor(i/w),a=(x*71+y*53+128)%256;
            return a?[(17+x*37+y*11)%256,(x*83+y*57)%256,(255-x*19-y*3+512)%256,a]:[0,0,0,0];});
        for(const mask of [false,true])for(const optimize of [false,true])
            generate(compile(authored(pixels,w,h,sigma,mask),optimize?[]:["--no-opt"]),reference(pixels,w,h,sigma,mask));
    }
    const pixels=Array.from({length:15},(_,i)=>[255,80,40,i===7?128:0]);
    for(const mask of [false,true])for(const optimize of [false,true])for(const debug of [false,true])for(const compress of [false,true]) {
        const data=compile(authored(pixels,5,3,1,mask),[...(optimize?[]:["--no-opt"]),debug?"--debug":"--strip",compress?"--compress":"--no-compress"]);
        const expected=reference(pixels,5,3,1,mask);
        generate(data,expected);
        fs.writeFileSync(base+".ipa",execFileSync(tool("ipdis"),[base+".iprop"]));
        execFileSync(tool("ipa"),[base+".ipa","-o",base+"-restored.iprop"]);
        generate(fs.readFileSync(base+"-restored.iprop"),expected);
    }
    // Receiver and sigma effects run once, in order, with no intrinsic RNG.
    const effectful=`asset t{size 7x1 output list point order output point state
      receiver()->layer{order={1,seed&65535}x=0 to 10 layer image(alpha=surface.source_over){pixel(2,0,#ff0000)}return image}
      sigma()->f32{order={2,seed&65535}x=0 to 10 return 1}
      echo(x:layer)->layer{return x.blur(0)} blur(x:i32)->i32{return x}
      draw{image=receiver().blur(sigma()).blur(0)alias=image;layer image{}image.blur(2);x=blur(1)
      layer limit{rect(3,0,4,1,#fff)}clip(mask.from(limit)){surface.paint(echo(alias).blur(0),blend=surface.source_over)}
      state={seed&65535,seed>>16}}}`;
    const float = bits => new Float32Array(Uint32Array.of(bits).buffer)[0];
    for(const opt of [false,true])for(const seed of [0,26,0xffffffff]) {
        const generated=generate(compile(effectful,opt?[]:["--no-opt"]),null,{seed});
        assert.deepEqual(Array.from(generated.rgba.slice(12,16)),[255,0,0,62]);
        assert.deepEqual(Array.from(generated.rgba.slice(8,12)),[0,0,0,0]);
        const once=(seed+0x9e3779b9)>>>0,twice=(once+0x9e3779b9)>>>0;
        assert.equal(float(generated.outputs[0].values[0].x),1);assert.equal(float(generated.outputs[0].values[0].y),seed&65535);
        assert.equal(float(generated.outputs[0].values[1].x),2);assert.equal(float(generated.outputs[0].values[1].y),once&65535);
        assert.equal(float(generated.outputs[1].values[0].x),twice&65535);assert.equal(float(generated.outputs[1].values[0].y),twice>>>16);
    }
    // D4 and scale mapping compare blur-before-export with blur-after-export.
    const child=path.join(directory,"blur-child.ips");
    for(const kind of [0,90,180,270])for(const mirror of [0,1])for(const mask of [false,true]) {
        const root=`import child from "blur-child.ips" asset t{size 12x10 draw{
          child.draw(seed,at={6,5},rot=${kind},mirror_x=${mirror})rect(0,0,12,10,#182332)
          surface.paint(child.outputs.${mask?"region":"image"}BLUR,${mask?"#ffffff,":""}blend=surface.source_over)}}`;
        const childSource=blur=>`asset child{size 5x3 origin 2,1 output layer image output mask region draw{
          layer captured(alpha=surface.source_over){pixel(0,0,#ff802080)pixel(4,2,#2040ffff)}
          image=captured${blur?".blur(1)":""};region=mask.from(captured)${blur?".blur(1)":""}}}`;
        fs.writeFileSync(child,childSource(true));const before=compile(root.replace("BLUR",""));
        fs.writeFileSync(child,childSource(false));const after=compile(root.replace("BLUR",".blur(1)"));
        for(const [sizeX,sizeY] of [[12,10],[24,10],[12,20]])
            assert.deepEqual(generate(after,null,{sizeX,sizeY}),generate(before,null,{sizeX,sizeY}),"blur preserves exported D4/scale mapping");
    }
    function trap(data,pattern) {
        fs.writeFileSync(base+"-trap.iprop",data);
        const png=base+"-failed.png",native=spawnSync(tool("ipgen"),[base+"-trap.iprop","-o",png],{encoding:"utf8"});
        assert.equal(native.status,1,native.stderr);assert.equal(native.signal,null);assert.match(native.stderr,pattern);
        assert.equal(fs.existsSync(png),false,"blur failure published image");
        assert.throws(()=>iprop.generate(new Uint8Array(data),{}),pattern);++traps;
    }
    for(const mask of [false,true])for(const opt of [false,true]) {
        const receiver=mask?"mask.from(image)":"image";
        for(const [decl,value] of [["input f32 sigma=-1","sigma"],["input f32 sigma=65","sigma"],
            ["input f32 zero=0","zero/zero"],["input f32 zero=0","1.0/zero"],["input f32 zero=0","-1.0/zero"]])
            trap(compile(`asset t{size 1x1 ${decl} draw{layer image{}x=${receiver}.blur(${value})}}`,opt?[]:["--no-opt"]),/blur sigma must be finite and in 0..64/);
        trap(compile(`asset t{size 1x1 draw{if(false){layer image{}${mask?"m=mask.from(image)":""}}${mask?"m":"image"}.blur(0)}}`,opt?[]:["--no-opt"]),/is unassigned/);
        trap(compile(`asset t{size 128x128 draw{layer image{}${receiver}.blur(64)}}`,opt?[]:["--no-opt"]),/fuel exhausted/);
        const dimensions=mask?"16384x512":"8192x385";
        const quota=compile(`asset t{size ${dimensions} draw{layer image{}${receiver}.blur(64)}}`,opt?[]:["--no-opt"]);
        quota.writeBigUInt64LE(20000000000n,sections(quota).get(1)+16);
        trap(quota,/64 MiB/);
    }
    function reject(data) {
        fs.writeFileSync(base+"-bad.iprop",data);
        const native=spawnSync(tool("ipval"),[base+"-bad.iprop"],{encoding:"utf8"});
        assert.equal(native.status,1,native.stderr);assert.equal(native.signal,null);
        assert.throws(()=>iprop.generate(new Uint8Array(data),{}));++rejections;
    }
    const wire=compile("asset t{size 1x1 draw{layer image{}x=image.blur(1);y=mask.from(image).blur(2)}}",["--no-opt"]);
    for(const at of instructions(wire))if(wire[at]===0xdd||wire[at]===0xde) {
        for(const relative of [1,2,3]) {const bad=Buffer.from(wire);bad[at+relative]=1;reject(bad);}
        const bad=Buffer.from(wire);bad[at]=wire[at]===0xdd?0xde:0xdd;reject(bad);
    }
    const dead=assemble("meta: size=1x1\nfn draw ( ) -> ( ):\nreturn\nlayer.blur\nmask.blur\nendfn\n");
    for(const at of instructions(dead))if(dead[at]===0xdd||dead[at]===0xde)
        for(const relative of [1,2]) {const bad=Buffer.from(dead);bad[at+relative]=1;reject(bad);}
    for(let minor=1;minor<=7;++minor) {const bad=Buffer.from(dead);bad.writeUInt16LE(minor,6);reject(bad);}
    // The module must still generate after traps and malformed files.
    generate(compile(authored(impulse,7,1,1,false)),oracle);
    return {comparisons,pins,traps,rejections};
}
module.exports={run,reference};
