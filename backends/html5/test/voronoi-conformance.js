"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),os=require("node:os"),path=require("node:path"),crypto=require("node:crypto");
const {execFileSync,spawnSync}=require("node:child_process");
const reference=require("../../../tests/reference/voronoi-reference.js");
const {sections}=require("./sampler-conformance.js");
const repo=path.resolve(__dirname,"../../.."),fixturePath=path.join(repo,"tests/fixtures/voronoi-v0.9.json");
const digest=v=>crypto.createHash("sha256").update(JSON.stringify(v)).digest("hex");
const vec=v=>"{"+v.join(",")+"}";
function argumentsOf(c){
    const initial=[c.sites?"explicit":"generated",...c.origin.map(reference.bits),...c.size.map(reference.bits)];
    if(c.sites)return [...initial,c.relax||0,...c.sites.flat().map(reference.bits)].map(String);
    const o={...reference.defaults,...c.options};
    return [...initial,o.count,o.seed>>>0,o.method,reference.bits(o.jitter),o.relax,reference.bits(o.anisotropy),...o.direction.map(reference.bits),
        reference.bits(o.rowBias),reference.bits(o.rowJitter),reference.bits(o.rowOffset),1000000000].map(String);
}
function expression(c){
    const first=[vec(c.origin),vec(c.size)];
    if(c.sites)return `voronoi(${[...first,"["+c.sites.map(vec).join(",")+"]",...(c.relax===undefined?[]:[c.relax])].join(",")})`;
    const o={...reference.defaults,...c.options},args=[...first,o.count,o.seed,o.method,o.jitter,o.relax,o.anisotropy,vec(o.direction),o.rowBias,o.rowJitter,o.rowOffset];
    return "voronoi("+args.slice(0,c.arity||12).join(",")+")";
}
function source(c){return `asset t{size 1x1 output list point sites output list point links output point state
    echo(d:diagram2d)->diagram2d{return d}draw{before=seed d=echo(${expression(c)})
    for i in 0..diagram.count(d){sites=diagram.site(d,i) ns=diagram.neighbors(d,i) for j in 0..list.len(ns){links={i,ns[j]}}}
    state={seed==before,unit()}pixel(0,0,#123)}}`;}
function run(iprop,tool,directory,probes={}){
    const owned=!directory;if(owned)directory=fs.mkdtempSync(path.join(os.tmpdir(),"iprop-voronoi-"));
    const base=path.join(directory,"voronoi"),cases=JSON.parse(fs.readFileSync(fixturePath)).cases;
    const nativeProbe=probes.native||path.resolve(path.dirname(tool("ipc")),"../tests/voronoi_probe"+(process.platform==="win32"?".exe":""));
    const hasNative=fs.existsSync(nativeProbe);let generations=0,traps=0,rejections=0,compilerErrors=0,topologies=0;
    const nativeRun=(name,args)=>execFileSync(tool(name),args,{encoding:"utf8",stdio:"pipe"});
    function compile(text,flags=[]){fs.writeFileSync(base+".ips",text);nativeRun("ipc",[base+".ips",...flags,"-o",base+".iprop"]);return fs.readFileSync(base+".iprop");}
    function assemble(text){fs.writeFileSync(base+".ipa",text);nativeRun("ipa",[base+".ipa","-o",base+".iprop"]);return fs.readFileSync(base+".iprop");}
    function generate(bytes){
        fs.writeFileSync(base+".iprop",bytes);const native=JSON.parse(nativeRun("ipgen",[base+".iprop","--float-bits","--seed","26","-o",base+".png"]));
        if(iprop){const wasm=iprop.generate(new Uint8Array(bytes),{seed:26});
            assert.deepEqual(wasm.outputs.map(s=>({name:s.name,kind:["point","rect","circle"][s.kind],list:s.isList,values:s.valueBits})),native.outputs,"Voronoi public native/WASM bits");
            assert.deepEqual([...wasm.data],[17,34,51,255]);
        }
        ++generations;return native.outputs;
    }
    function reject(bytes,message,validation=false){
        fs.writeFileSync(base+".iprop",bytes);fs.rmSync(base+"-failed.png",{force:true});
        const r=spawnSync(tool(validation?"ipval":"ipgen"),[base+".iprop",...(validation?[]:["-o",base+"-failed.png"])],{encoding:"utf8"});
        assert.equal(r.signal,null);assert.equal(r.status,1,r.stderr);assert.match(r.stderr,message);
        if(iprop)assert.throws(()=>iprop.generate(new Uint8Array(bytes),{}),message);
        assert.equal(fs.existsSync(base+"-failed.png"),false);if(validation)++rejections;else ++traps;
    }
    try{
        const control=generate(compile("asset t{size 1x1 output point state draw{state={1,unit()}pixel(0,0,#123)}}"))[0].values;
        for(const c of cases){
            let data;
            if(hasNative){data=JSON.parse(execFileSync(nativeProbe,argumentsOf(c),{encoding:"utf8"}));reference.verify(c,data);assert.equal(digest(data),c.geometry_sha256,c.name+" complete topology pin");++topologies;}
            if(probes.wasm){const wasm=JSON.parse(execFileSync(process.execPath,[probes.wasm,...argumentsOf(c)],{encoding:"utf8"}));reference.verify(c,wasm);
                assert.equal(digest(wasm),c.geometry_sha256,c.name+" WASM complete topology pin");if(data)assert.deepEqual(wasm,data,c.name+" every geometry record, fuel and quota");}
            for(const optimize of [false,true])for(const debug of [false,true])for(const compressed of [false,true]){
                let bytes=compile(source(c),[...(optimize?[]:["--no-opt"]),debug?"--debug":"--strip",...(compressed?[]:["--no-compress"])]);
                if(c.budget)bytes=assemble(nativeRun("ipdis",[base+".iprop"]).replace(/fuel=\d+/,"fuel="+c.budget));
                const outputs=generate(bytes);
                assert.equal(digest(outputs.slice(0,2)),c.public_sha256,c.name+" public site/neighbor pin");assert.deepEqual(outputs[2].values,control,"Voronoi preserves active RNG");
                if(data){assert.deepEqual(outputs[0].values,data.sites.map(([x,y])=>({x,y})));assert.deepEqual(outputs[1].values,data.regions.flatMap((r,i)=>r[8].map(j=>({x:reference.bits(i),y:reference.bits(j)}))));}
                const text=nativeRun("ipdis",[base+".iprop"]);const roundTrip=assemble(text);assert.deepEqual(generate(roundTrip),outputs,"Voronoi assembly round trip");
            }
        }
        const maximum=cases.find(c=>c.name==="maximum-grid");
        const explicitMaximum={...maximum,sites:reference.sites(maximum)};
        assert.equal(digest(generate(compile(source(explicitMaximum))).slice(0,2)),maximum.public_sha256,"256 explicit sites use the same complete diagram");
        for(const c of cases.filter(c=>c.budget)) {
            const bytes=compile(source(c),["--no-compress"]);
            const spent=JSON.parse(nativeRun("ipgen",[base+".iprop","-o",base+".png"])).fuel.spent;
            bytes.writeBigUInt64LE(BigInt(spent-1),sections(bytes).get(1)+16);
            reject(bytes,/fuel exhausted/);
        }
        for(const [call,message] of [
            ["voronoi({0,0},{1,1},0,0)",/count must be in/],["voronoi({0,0},{1,1},257,0)",/count must be in/],
            ["voronoi({0,0},{0,1},1,0)",/rectangle/],["voronoi({0,0},{1,1},[{0,0},{0,0}])",/distinct/],
            ["voronoi({0,0},{1,1},[{-1,0}])",/inside/],["voronoi({0,0},{1,1},1,0,2,1,0)",/method/],
            ["voronoi({0,0},{1,1},1,0,0,2,0)",/jitter/],["voronoi({0,0},{1,1},1,0,0,1,5)",/relax/],
            ["voronoi({0,0},{1,1},1,0,0,1,0,0,{1,0})",/anisotropy/],
            ["voronoi({0,0},{1,1},1,0,0,1,0,1,{0,0})",/direction/],
            ["voronoi({0,0},{1,1},1,0,0,1,0,1,{1,0},2,0,0.5)",/row parameters/],
            ["diagram.site(voronoi({0,0},{1,1},1,0),1)",/index out of range/],
            ["diagram.neighbors(voronoi({0,0},{1,1},1,0),-1)",/index out of range/]])
            reject(compile(`asset t{size 1x1 draw{${call}}}`),message);
        for(const type of ["sites","count","site","neighbors"]){
            const text=type==="sites"?"list<vec2> d=[{0,0}]":"diagram2d d=voronoi({0,0},{1,1},1,0)";
            reject(compile(`asset t{size 1x1 draw{if(false){${text}} ${type==="sites"?"voronoi({0,0},{1,1},d)":`diagram.${type}(d${type==="count"?"":",0"})`}}}`),/unassigned/);
        }
        const dead=assemble("meta: size=1x1\nfn draw ( ) -> ( ):\nreturn\nvoronoi\ndrop\nendfn\n");
        const code=sections(dead).get(3);
        // Locate a complete instruction by decoding the disassembler's sole
        // 0xe5 record after the function header (not an arbitrary global byte).
        let at=-1;for(let i=code;i<dead.length-3;++i)if(dead[i]===0xe5&&dead[i+1]===0&&dead[i+2]===0&&dead[i+3]===0){at=i;break;}assert(at>=0);
        for(const [delta,value] of [[1,1],[2,1],[3,1]]){const bad=Buffer.from(dead);bad[at+delta]=value;reject(bad,/aux|operand|zero|reserved/,true);}
        for(const body of ["d=voronoi({0,0},{1,1},[1])","diagram.count(1)","d=voronoi({0,0},{1,1},1)",
            "d=voronoi({0,0},{1,1},sites=[{0,0}],seed=1)","const diagram2d d=voronoi({0,0},{1,1},1,0)"]){
            fs.writeFileSync(base+".ips",`asset t{size 1x1 draw{${body}}}`);const r=spawnSync(tool("ipc"),[base+".ips","--check"],{encoding:"utf8"});assert.equal(r.status,1,r.stderr);++compilerErrors;
        }
        return {passed:true,vectors:cases.length,generations,topologies,traps,rejections,compilerErrors,nativeRepresentation:hasNative,wasmRepresentation:!!probes.wasm,wasm:!!iprop};
    }finally{if(owned)fs.rmSync(directory,{recursive:true,force:true});}
}
module.exports={run,argumentsOf,expression,source,digest};
if(require.main===module){const tools=path.resolve(process.argv[2]);console.log(JSON.stringify(run(null,n=>path.join(tools,n),undefined,{native:process.argv[3],wasm:process.argv[4]})));}
