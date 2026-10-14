"use strict";

const assert = require("node:assert/strict");
const {execFileSync, spawnSync} = require("node:child_process");
const fs = require("node:fs");
const path = require("node:path");
const {decodePng} = require("./png.js");
const {replaceFloatConstants} = require("./vector-conformance.js");

const floatBits = value => {
    const bytes = Buffer.alloc(4);bytes.writeFloatLE(value);return bytes.readUInt32LE();
};
const points = values => values.map(([x,y]) => ({x:floatBits(x),y:floatBits(y)}));

function run(iprop, tool, directory) {
    const base = path.join(directory,"list-methods");
    let comparisons=0,pins=0,traps=0,rejections=0;
    function generate(binary, seed=26, expected) {
        const native=JSON.parse(execFileSync(tool("ipgen"),[binary,"--seed",String(seed),"--float-bits","-o",base+".png"],{encoding:"utf8"}));
        const image=decodePng(fs.readFileSync(base+".png"));
        const wasm=iprop.generate(new Uint8Array(fs.readFileSync(binary)),{seed});
        assert.deepEqual(Buffer.from(wasm.data),Buffer.from(image.data),"list method RGBA");
        assert.deepEqual(wasm.outputs.map(slot=>slot.valueBits),native.outputs.map(slot=>slot.values),"list method native/WASM bits");
        if(expected){assert.deepEqual(native.outputs[0].values,expected,"independent list method sequence");pins+=expected.length;}
        ++comparisons;return native.outputs;
    }
    function compile(source, flags=[]) {
        fs.writeFileSync(base+".ips",source);
        execFileSync(tool("ipc"),[base+".ips",...flags,"-o",base+".iprop"]);
        return base+".iprop";
    }
    function assemble(source) {
        fs.writeFileSync(base+".ipa",source);
        execFileSync(tool("ipa"),[base+".ipa","-o",base+".iprop"]);
        return base+".iprop";
    }
    function trap(binary, message) {
        const output=base+"-trap.png";
        const native=spawnSync(tool("ipgen"),[binary,"-o",output],{encoding:"utf8"});
        assert.equal(native.status,1);assert.equal(native.signal,null);assert.match(native.stderr,message);
        assert.equal(fs.existsSync(output),false);
        assert.throws(()=>iprop.generate(new Uint8Array(fs.readFileSync(binary)),{}),message);++traps;
    }
    const cases=[
        {type:"i32",items:"[3,1,3,2]",unique:[[3,0],[1,0],[2,0]],sorted:[[1,0],[2,0],[3,0],[3,0]],count:4},
        {type:"f32",items:"[2.0,-0.0,2.0,0.0,-1.0]",unique:[[2,0],[-0,0],[-1,0]],sorted:[[-1,0],[-0,0],[0,0],[2,0],[2,0]],count:5},
        {type:"color",items:"[#010203ff,#01020300,#00ffffff,#010203ff]",
            unique:[[1,2],[3,255],[1,2],[3,0],[0,255],[255,255]],
            sorted:[[0,255],[255,255],[1,2],[3,0],[1,2],[3,255],[1,2],[3,255]],count:4},
        {type:"vec2",items:"[{1,3},{-1,5},{1,2},{1,3}]",unique:[[1,3],[-1,5],[1,2]],sorted:[[-1,5],[1,2],[1,3],[1,3]],count:4},
        {type:"distribution",items:"[weighted{1:2,2:3},weighted{1:2,2:3},weighted{1.0:2,2:3}]",unique:[],count:3,uniqueCount:2},
        {type:"sampler<i32>",items:"[sampler(3),sampler(1),sampler(3)]",unique:[[3,0],[1,0]],count:3},
        {type:"sampler<f32>",items:"[sampler(3.0),sampler(1.0),sampler(3.0)]",unique:[[3,0],[1,0]],count:3},
        {type:"sampler<color>",items:"[sampler(#010203ff),sampler(#01020300),sampler(#010203ff)]",unique:[[1,2],[3,255],[1,2],[3,0]],count:3},
        {type:"sampler<vec2>",items:"[sampler({1,3}),sampler({1,2}),sampler({1,3})]",unique:[[1,3],[1,2]],count:3}
    ];
    function observation(type, name) {
        if(type==="distribution")return "";
        const sampler=type.startsWith("sampler<");const item=sampler?type.slice(8,-1):type;
        const value=`${name}[i]${sampler?".pick()":""}`;
        const emit=item==="color"?"values={value.r,value.g};values={value.b,value.a}":item==="vec2"?"values=value":"values={value,0}";
        return `for i in 0..list.len(${name}){value=${value};${emit}}`;
    }
    for(const entry of cases) for(const optimized of [false,true]) for(const debug of [false,true]) for(const compressed of [false,true]) {
        const {type,items,unique,sorted,count}=entry;
        const length=entry.uniqueCount ?? unique.length/(type.includes("color")?2:1);
        const source=`asset t{size 1x1 output list point values output point state
            const list<${type}> fixed=${items}.distinct()
            const list<${type}> nothing=[].distinct()${sorted?".sort()":""}
            ${sorted?`const list<${type}> ordered=${items}.sort()`:""}
            echo(xs:list<${type}>)->list<${type}>{return xs}
            draw{original=${items};alias=original;unique=echo(original).distinct();original.distinct()
              values={list.len(original),list.len(alias)};values={list.len(unique),list.len(fixed)};values={list.len(nothing),0}
              ${observation(type,"unique")}${observation(type,"fixed")}
              ${sorted?`sorted=original.sort();original.sort();${observation(type,"sorted")}${observation(type,"ordered")}`:""}
              state={seed&65535,seed>>16}}}`;
        const expected=points([[count,count],[length,length],[0,0],...unique,...unique,...(sorted||[]),...(sorted||[])]);
        const flags=[...(optimized?[]:["--no-opt"]),debug?"--debug":"--strip",compressed?"--compress":"--no-compress"];
        const binary=compile(source,flags);
        for(const seed of [0,26,0xffffffff]) {
            const actual=generate(binary,seed,expected);
            assert.deepEqual(actual[1].values,points([[seed&65535,seed>>>16]]),"descriptions and constant picks do not draw");++pins;
        }
        fs.writeFileSync(base+".ipa",execFileSync(tool("ipdis"),[binary]));
        execFileSync(tool("ipa"),[base+".ipa","-o",base+"-restored.iprop"]);
        generate(base+"-restored.iprop",26,expected);
        execFileSync(tool("ipc"),[base+".ips","--format",base+"-formatted.ips","--format-only"]);
        execFileSync(tool("ipc"),[base+"-formatted.ips",...flags,"-o",base+"-formatted.iprop"]);
        generate(base+"-formatted.iprop",26,expected);
    }
    // Receivers run once, including helpers with draws; methods never draw.
    const receiver=`asset t{size 1x1 output list point values output point state
        make()->list<i32>{x=unit();return [3,1,3,2]}
        draw{values={list.len(make().distinct().sort()),0};state={seed&65535,seed>>16}}}`;
    const receiverBinary=compile(receiver);const receiverBytes=fs.readFileSync(receiverBinary);
    const reference=compile("asset t{size 1x1 output point state draw{x=unit();state={seed&65535,seed>>16}}}");
    for(const seed of [0,26,0xffffffff]) {
        const expected=generate(reference,seed)[0].values;
        fs.writeFileSync(base+"-receiver.iprop",receiverBytes);
        const actual=generate(base+"-receiver.iprop",seed,points([[3,0]]));assert.deepEqual(actual[1].values,expected);++pins;
    }
    // Full signed i32 range must not lose precision through f32 conversion.
    generate(compile("asset t{size 1x1 output point p draw{xs=[16777218,16777217,2147483647,-2147483648].sort();p={xs[1]==16777217,xs[2]==16777218}}}"),26,points([[1,1]]));
    // Maximum list length, immutable input, and sorted distinct chaining.
    const maximum="["+Array.from({length:256},(_,i)=>255-i).join(",")+"]";
    generate(compile(`asset t{size 1x1 output list point p draw{xs=${maximum};ys=xs.sort().distinct();for i in 0..256{p={xs[i],ys[i]}}}}`),26,points(Array.from({length:256},(_,i)=>[255-i,i])));

    // Direct IPA transports original exceptional float encodings through lists.
    const encodings=[0x7fc01234,0x80000000,0x3f800000,0xff800000,0,0x7f800000,0xffc05678,1,0x80000001,0x3f800000];
    const distinctBits=[0x7fc01234,0x80000000,0x3f800000,0xff800000,0x7f800000,1,0x80000001];
    const sortedBits=[0xff800000,0x80000001,0x80000000,0,1,0x3f800000,0x3f800000,0x7f800000,0x7fc01234,0xffc05678];
    let assembly="meta: size=1x1\ntypes: list<f32>\noutput: list point p\nfn draw (  ) -> ( ):\nlocals: list<f32> list<f32> list<f32>\nconst.i32 10\nlist.begin 0\n";
    for(let i=0;i<encodings.length;++i)assembly+=`const.f32 ${100+i}\nlist.append 0\n`;
    assembly+="list.end 0\nlocal.set 0\nlocal.get 0\nlist.distinct 0\nlocal.set 1\nlocal.get 0\nlist.sort 0\nlocal.set 2\n";
    for(const [local,expected] of [[0,encodings],[1,distinctBits],[2,sortedBits]])
        for(let i=0;i<expected.length;++i)assembly+=`local.get ${local}\nconst.i32 ${i}\nlist.get 0\nconst.f32 0\nout.list.point p\n`;
    assembly+="endfn\n";assemble(assembly);
    const exceptional=fs.readFileSync(base+".iprop");replaceFloatConstants(exceptional,encodings);fs.writeFileSync(base+".iprop",exceptional);
    generate(base+".iprop",26,[...encodings,...distinctBits,...sortedBits].map(x=>({x,y:0})));

    for(const body of ["if(false){xs=[1]}xs.distinct()","if(false){xs=[1]}xs.sort()",
        "if(false){s=sampler(1)}xs=[s];xs.distinct()","if(false){d=gaussian(0,1)}xs=[d];xs.distinct()"])
        for(const optimized of [false,true])trap(compile(`asset t{size 1x1 draw{${body}}}`,optimized?[]:["--no-opt"]),/handle is unassigned/);
    const construct="const.i32 4\nlist.begin 0\n"+[3,1,3,2].map(n=>`const.i32 ${n}\nlist.append 0\n`).join("")+"list.end 0\n";
    trap(assemble("meta: size=1x1 fuel=29\ntypes: list<i32>\nfn draw (  ) -> ( ):\n"+construct+"list.distinct 0\ndrop\nendfn\n"),/fuel exhausted/);
    const reversed=Array.from({length:256},(_,i)=>`const.i32 ${255-i}\nlist.append 0\n`).join("");
    trap(assemble("meta: size=1x1 fuel=100000000\ntypes: list<i32>\nfn draw (  ) -> ( ):\nlocals: list<i32>\n"+
        "const.i32 256\nlist.begin 0\n"+reversed+"list.end 0\nlocal.set 0\nloop\nlocal.get 0\nlist.sort 0\ndrop\nbr 0\nend\nendfn\n"),/generation data exceeds 64 MiB/);

    function reject(bytes, message) {
        fs.writeFileSync(base+"-invalid.iprop",bytes);
        const native=spawnSync(tool("ipgen"),[base+"-invalid.iprop","-o",base+"-invalid.png"],{encoding:"utf8"});
        assert.equal(native.status,1);assert.equal(native.signal,null);
        if(message)assert.match(native.stderr,message);
        assert.throws(()=>iprop.generate(new Uint8Array(bytes),{}),message);++rejections;
    }
    const sections = bytes => {
        const result=new Map();for(let i=0;i<bytes.readUInt32LE(12);++i)result.set(bytes.readUInt32LE(20+i*12),bytes.readUInt32LE(24+i*12));return result;
    };
    assemble("meta: size=1x1\ntypes: list<i32> list<distribution> sampler<i32>\nfn draw (  ) -> ( ):\nreturn\nlist.distinct 0\nlist.sort 0\nendfn\n");
    const gate=fs.readFileSync(base+".iprop"),dead=sections(gate).get(3)+26;
    assert.equal(gate[dead],0xd6);assert.equal(gate[dead+4],0xd7);generate(base+".iprop");
    for(const at of [dead,dead+4]) for(const operand of [2,65535]){const bad=Buffer.from(gate);bad.writeUInt16LE(operand,at+2);reject(bad);}
    const unsupported=Buffer.from(gate);unsupported.writeUInt16LE(1,dead+6);reject(unsupported,/list.sort does not support/);
    for(const at of [dead,dead+4]){const bad=Buffer.from(gate);bad[at+1]=1;reject(bad,/aux/);}
    assemble("meta: size=1x1\nfn draw (  ) -> ( ):\nreturn\nnop\nendfn\n");
    const old=fs.readFileSync(base+".iprop"),unreachable=sections(old).get(3)+26;
    assert.equal(old[unreachable],0);
    for(const opcode of [0xd6,0xd7]) for(let minor=0;minor<7;++minor){const bad=Buffer.from(old);bad[unreachable]=opcode;bad.writeUInt16LE(minor,6);reject(bad,/unsupported format minor version/);}
    // Failed generations do not poison later list operations.
    generate(compile("asset t{size 1x1 output point p draw{xs=[3,1,3].distinct().sort();p={xs[0],xs[1]}}}"),26,points([[1,3]]));
    return {comparisons,pins,traps,rejections,signatures:13};
}
module.exports={run};
