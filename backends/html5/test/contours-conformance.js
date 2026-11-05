"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),crypto=require("node:crypto");
const {execFileSync,spawnSync}=require("node:child_process");
const {decodePng}=require("./png.js");
const reference=require("../../../tests/reference/contours-reference.js");
const {outputBits}=require("./harness.js");
const fixture=path.resolve(__dirname,"../../../tests/fixtures/contours-v0.9.json");
const digest=d=>crypto.createHash("sha256").update(JSON.stringify(d)).digest("hex");
function argumentsOf(c,budget=1e9,occupied=0){return [c.pixels[0].length,c.pixels.length,Buffer.from(c.pixels.flat()).toString("hex"),
    ...[c.threshold??.5,c.simplify||0,c.minArea||0].map(reference.bits),c.connectivity||0,...(c.scale||[1,1]).map(reference.bits),c.d4||0,
    ...(c.origin||[0,0]).map(x=>x>>>0),budget,occupied].map(String);}
function source(c,arity=5,a8=false){
    const pixels=c.pixels.map((row,y)=>row.map((a,x)=>`pixel(${x},${y},#ffffff${a.toString(16).padStart(2,"0")})`).join(" ")).join(" ");
    return `asset t{size ${c.pixels[0].length}x${c.pixels.length} output point state
    echo(c:contours2d)->contours2d{return c}draw{before=seed layer image{${pixels}} m=mask.from(image)
    ${a8?"m=mask.union(m,m)":""} c=echo(contour(${["m",c.threshold??.5,c.simplify||0,c.minArea||0,c.connectivity||0].slice(0,arity).join(",")})) state={seed==before,unit()}pixel(0,0,#123)}}`;
}
function run(iprop,tool,directory,probes={}){
    const owned=!directory;if(owned)directory=fs.mkdtempSync(path.join(os.tmpdir(),"iprop-contours-"));
    const base=path.join(directory,"contours"),corpus=JSON.parse(fs.readFileSync(fixture));
    const nativeProbe=probes.native||path.resolve(path.dirname(tool("ipc")),"../tests/contours_probe"+(process.platform==="win32"?".exe":""));
    const hasNative=fs.existsSync(nativeProbe);let generations=0,topologies=0,traps=0,rejections=0,compilerErrors=0;
    const resourceBoundaries=[];let mappedNestedCases=0;
    const nativeRun=(name,args)=>execFileSync(tool(name),args,{encoding:"utf8",stdio:"pipe"});
    function compile(text,flags=[]){fs.writeFileSync(base+".ips",text);nativeRun("ipc",[base+".ips",...flags,"-o",base+".iprop"]);return fs.readFileSync(base+".iprop");}
    function assemble(text){fs.writeFileSync(base+".ipa",text);nativeRun("ipa",[base+".ipa","-o",base+".iprop"]);return fs.readFileSync(base+".iprop");}
    function generate(bytes){
        fs.writeFileSync(base+".iprop",bytes);const native=JSON.parse(nativeRun("ipgen",[base+".iprop","--float-bits","--seed","26","-o",base+".png"]));
        const image=decodePng(fs.readFileSync(base+".png"));
        assert.deepEqual([...image.data.slice(0,4)],[17,34,51,255]);
        assert([...image.data.slice(4)].every(x=>x===0),"contour does not paint its source or geometry");
        if(iprop){const wasm=iprop.generate(new Uint8Array(bytes),{seed:26});
            assert.deepEqual(outputBits(wasm.outputs),native.outputs,"contour public native/WASM execution and RNG");
            assert.deepEqual([...wasm.data],[...image.data],"entire canvas, including untouched pixels");}
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
        const control=generate(compile("asset t{size 1x1 output point state draw{state={1,unit()}pixel(0,0,#123)}}"));
        for(const c of corpus.cases){
            let data;
            if(hasNative){data=JSON.parse(execFileSync(nativeProbe,argumentsOf(c),{encoding:"utf8"}));reference.verify(c,data);assert.equal(digest(data),c.geometry_sha256,c.name+" complete topology pin");++topologies;}
            if(probes.wasm){const wasm=JSON.parse(execFileSync(process.execPath,[probes.wasm,...argumentsOf(c)],{encoding:"utf8"}));reference.verify(c,wasm);
                assert.equal(digest(wasm),c.geometry_sha256,c.name+" complete WASM pin");if(data)assert.deepEqual(wasm,data,c.name+" geometry, hierarchy, metadata, fuel and arena");}
            for(const [optimize,debug,compressed,a8] of [[true,false,true,false],[false,true,false,false],[true,true,false,true],[false,false,true,true]]){
                const flags=[...(optimize?[]:["--no-opt"]),debug?"--debug":"--strip",...(compressed?[]:["--no-compress"])];
                const bytes=compile(source(c,5,a8),flags);assert.deepEqual(generate(bytes),control,"no RNG consumption");
                assert.deepEqual(generate(assemble(nativeRun("ipdis",[base+".iprop"]))),control,"assembly round trip");
            }
        }
        if(hasNative)for(let d4=0;d4<8;++d4){
            const c={...corpus.cases.find(c=>c.name==="island-in-hole"),name:"mapped-nested-"+d4,d4,scale:[2,4],origin:[-11,17],simplify:0.75};
            const data=JSON.parse(execFileSync(nativeProbe,argumentsOf(c)));reference.verify(c,data);
            if(probes.wasm){const wasm=JSON.parse(execFileSync(process.execPath,[probes.wasm,...argumentsOf(c)]));reference.verify(c,wasm);assert.deepEqual(wasm,data,"nested reflected/rotated simplification");}
            ++mappedNestedCases;
        }
        if(hasNative)for(const name of ["empty","nested-simplify-1000","seeded-gray-7"]){
            const c=corpus.cases.find(c=>c.name===name),args=argumentsOf(c),normal=JSON.parse(execFileSync(nativeProbe,args));
            const probe=(budget,occupied,wasm=false)=>spawnSync(wasm?process.execPath:nativeProbe,
                [...(wasm?[probes.wasm]:[]),...argumentsOf(c,budget,occupied)],{encoding:"utf8"});
            const exact=probe(normal.fuel,0);assert.equal(exact.status,0,exact.stderr);assert.deepEqual(JSON.parse(exact.stdout),normal);
            const oneLess=probe(normal.fuel-1,0);assert.equal(oneLess.status,4);assert.match(oneLess.stderr,/fuel exhausted/);
            let lo=0,hi=64*1024*1024-(128+c.pixels.length*c.pixels[0].length);
            while(lo+1<hi){const mid=Math.floor((lo+hi)/2),r=probe(1e9,mid);if(r.status===0)lo=mid;else{assert.equal(r.status,4);assert.match(r.stderr,/64 MiB/);hi=mid;}}
            for(const wasm of probes.wasm?[false,true]:[false]){
                const fits=probe(1e9,lo,wasm),fails=probe(1e9,lo+1,wasm);assert.equal(fits.status,0,fits.stderr);assert.deepEqual(JSON.parse(fits.stdout),normal);
                assert.equal(fails.status,4);assert.match(fails.stderr,/64 MiB/);
                const fuelFails=probe(normal.fuel-1,0,wasm);assert.equal(fuelFails.status,4);assert.match(fuelFails.stderr,/fuel exhausted/);
            }
            resourceBoundaries.push({name,fuel:normal.fuel,minimumFreeBytes:64*1024*1024-lo});
        }
        const empty=corpus.cases[0];assert.deepEqual(generate(compile(source(empty,2))),control,"short overload");
        for(const [tail,message] of [["-1",/threshold/],["2",/threshold/],["0.5,-1,0,0",/simplify/],["0.5,0,-1,0",/min_area/],["0.5,0,0,2",/connectivity/]])
            reject(compile(`asset t{size 1x1 draw{layer l{} m=mask.from(l) contour(m,${tail})}}`),message);
        for(const optimize of [false,true])reject(compile("asset t{size 1x1 draw{if(false){layer l{} m=mask.from(l)} contour(m,0.5)}}",optimize?[]:["--no-opt"]),/unassigned/);
        reject(compile("asset t{size 1x1 draw{layer l{} m=mask.from(l) clip(m){contour(m,2)}}}"),/threshold/);
        reject(compile("asset t{size 1x1 draw{layer l{pixel(0,0,#fff)} m=mask.offset(mask.from(l),2147483647,0) contour(m,0.5)}}"),/collapses/);
        for(const op of ["contour","contour.config"]){
            const dead=assemble(`meta: size=1x1\nfn draw ( ) -> ( ):\nreturn\n${op}\ndrop\nendfn\n`);
            const {sections}=require("./harness.js"),code=sections(dead).get(3),id=op==="contour"?0xee:0xef;
            let at=-1;for(let i=code;i<dead.length-3;++i)if(dead[i]===id&&dead[i+1]===0&&dead[i+2]===0&&dead[i+3]===0){at=i;break;}assert(at>=0);
            for(const delta of [1,2,3]){const bad=Buffer.from(dead);bad[at+delta]=1;reject(bad,/aux|operand|zero|reserved/,true);}
        }
        for(const body of ["contour(1,0.5)","layer l{} contour(l,0.5)","layer l{} m=mask.from(l) contour(m)",
            "layer l{} m=mask.from(l) contour(m,0.5,connectivity=0)","const contours2d c=contour(1,0.5)","layer l{} m=mask.from(l) diagram.count(contour(m,0.5))"]){
            fs.writeFileSync(base+".ips",`asset t{size 1x1 draw{${body}}}`);const r=spawnSync(tool("ipc"),[base+".ips","--check"],{encoding:"utf8"});assert.equal(r.status,1,r.stderr);++compilerErrors;}
        return {passed:true,vectors:corpus.cases.length,generations,topologies,mappedNestedCases,traps,rejections,compilerErrors,resourceBoundaries,nativeRepresentation:hasNative,wasmRepresentation:!!probes.wasm,wasm:!!iprop};
    }finally{if(owned)fs.rmSync(directory,{recursive:true,force:true});}
}
module.exports={run,argumentsOf,source,digest};
if(require.main===module){const tools=path.resolve(process.argv[2]);console.log(JSON.stringify(run(null,n=>path.join(tools,n),undefined,{native:process.argv[3],wasm:process.argv[4]})));}
