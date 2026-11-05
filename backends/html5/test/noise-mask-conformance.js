"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path");
const {execFileSync,spawnSync}=require("node:child_process");
const {decodePng}=require("./png.js");
const {replaceFloatConstants}=require("./vector-conformance.js"),reference=require("./noise-mask-reference.js");
const B=require("./noise-v09-reference.js").bits,repo=path.resolve(__dirname,"../../..");
const {instructions,outputBits,sections}=require("./harness.js");
const numeric=n=>Number.isInteger(n)?n+".0":Math.fround(n).toPrecision(9);
const vector=p=>"{"+p.map(numeric).join(",")+"}";
const pin=r=>({...r,coverage:Buffer.from(r.coverage).toString("hex"),visible:Buffer.from(r.visible).toString("hex")});
function run(iprop,tool,directory){
    const cases=JSON.parse(fs.readFileSync(path.join(repo,"tests/fixtures/noise-mask-v0.9.json"))).cases;
    const base=path.join(directory,"noise-mask-v09");let generations=0,pins=0,traps=0,rejections=0,compilerErrors=0;
    function compile(source,flags=[]){
        fs.writeFileSync(base+".ips",source);
        execFileSync(tool("ipc"),[base+".ips",...flags,"-o",base+".iprop"],{stdio:"pipe"});
        return Buffer.from(fs.readFileSync(base+".iprop"));
    }
    function assemble(source){
        fs.writeFileSync(base+".ipa",source);execFileSync(tool("ipa"),[base+".ipa","-o",base+".iprop"],{stdio:"pipe"});
        return Buffer.from(fs.readFileSync(base+".iprop"));
    }
    function generate(bytes,options={},expected){
        options={seed:26,...options};
        fs.writeFileSync(base+".iprop",bytes);
        const args=[base+".iprop","--float-bits","--seed",String(options.seed??26),"-o",base+".png"];
        if(options.sizeX)args.push("--size",`${options.sizeX}x${options.sizeY}`);
        const output=JSON.parse(execFileSync(tool("ipgen"),args,{encoding:"utf8"})),image=decodePng(fs.readFileSync(base+".png"));
        if(expected){
            const alpha=Array.from(image.data).filter((_,i)=>i%4===3);
            assert.equal(Buffer.from(alpha).toString("hex"),expected,"independent A8 image");
            pins+=alpha.length;
        }
        if(iprop){
            const wasm=iprop.generate(new Uint8Array(bytes),options);
            assert.deepEqual(Buffer.from(wasm.data),Buffer.from(image.data),"noise mask native/WASM pixels");
            assert.deepEqual(outputBits(wasm.outputs),output.outputs,"noise mask native/WASM RNG bits");
        }
        ++generations;return {image,output};
    }
    function reject(bytes,message,validation=false,options={}){
        fs.writeFileSync(base+".iprop",bytes);const png=base+"-failed.png";fs.rmSync(png,{force:true});
        const result=spawnSync(tool(validation?"ipval":"ipgen"),[base+".iprop",...(validation?[]:["-o",png]),
            ...(options.workLimit?["--work-limit",String(options.workLimit)]:[])],{encoding:"utf8"});
        assert.equal(result.signal,null);assert.equal(result.status,1,result.stderr);assert.match(result.stderr,message);
        assert.equal(fs.existsSync(png),false,"failed mask generation must not publish a PNG");
        if(iprop)assert.throws(()=>iprop.generate(new Uint8Array(bytes),options),message);
        if(validation)++rejections;else ++traps;
    }
    const controls=new Map();for(const seed of [0,26,4294967295])controls.set(seed,generate(compile("asset t{size 1x1 output point state draw{state={1,unit()}}}"),{seed}).output.outputs);
    for(const v of cases){
        assert.deepEqual(pin(reference.raster(v)),v.expected,"frozen independent A8 pin "+v.name);
        const initial=[vector(v.origin),vector(v.size),String(v.kind),vector(v.frequency),numeric(v.threshold),String(v.seed)];
        const tail=[String(v.octaves),numeric(v.softness),String(v.comparison),numeric(v.warp)];
        const defaults=v.name.endsWith("defaults");
        const short=`noise.mask(${initial.join(",")})`,full=`noise.mask(${initial.concat(tail).join(",")})`;
        const use=v.offset?`mask.offset(m,${v.offset.join(",")})`:"m";
        const source=`asset t{size ${v.asset.join("x")} output point state draw{before=seed m=${defaults?short:full} surface.paint(${use},#fff,blend=surface.source_over)state={seed==before,unit()}}}`;
        for(const opt of [false,true])for(const compressed of [false,true]){
            const bytes=compile(source,[...(opt?[]:["--no-opt"]),...(compressed?[]:["--no-compress"]),opt?"--debug":"--strip"]);
            const options={seed:opt?26:4294967295,sizeX:v.host[0],sizeY:v.host[1]};
            const actual=generate(bytes,options,v.expected.visible);assert.deepEqual(actual.output.outputs,controls.get(options.seed),"mask preserves RNG");
            if(!compressed){
                const text=execFileSync(tool("ipdis"),[base+".iprop"],{encoding:"utf8"});
                assert.match(text,defaults?/\bnoise\.mask\s*\n/:/\bnoise\.mask_config\s*\n/);
                generate(assemble(text),options,v.expected.visible);
                if(defaults){
                    const other=compile(source.replace(short,full),opt?[]:["--no-opt"]);
                    assert.deepEqual(generate(other,options,v.expected.visible).image.data,actual.image.data,"short defaults equal full call");
                }
            }
        }
    }
    // White source-over paint and white primitive clipping must expose the
    // exact same A8, including scaled and off-target views.
    for(const v of cases.filter(v=>v.kind===4&&!v.offset)){
        const call=`noise.mask(${vector(v.origin)},${vector(v.size)},4,${vector(v.frequency)},${numeric(v.threshold)},${v.seed},${v.octaves},${numeric(v.softness)},${v.comparison},${numeric(v.warp)})`;
        for(const body of ["surface.paint(mask.intersect(m,m),#fff,blend=surface.source_over)",
            "layer clipped(alpha=surface.source_over){clip(m){rect(0,0,7,5,#fff)}}surface.paint(clipped,blend=surface.source_over)", "surface.paint(mask.expand(mask.shrink(m,0),0).blur(0),#fff,blend=surface.source_over)"]){
            const bytes=compile(`asset t{size 7x5 draw{m=${call} ${body}}}`);
            generate(bytes,{sizeX:v.host[0],sizeY:v.host[1]},v.expected.visible);
        }
    }
    const good=[['v',[0,0]],['v',[1,1]],['i',0],['v',[1,1]],['f',0],['i',7],['i',4],['f',0],['i',0],['f',0]];
    function raw(args=good,full=true){
        const floats=[];const constant=x=>`const.f32 ${100+floats.push(B(x))-1}\n`;
        const push=([kind,x])=>kind==='v'?constant(x[0])+constant(x[1])+"vec2.make\n":kind==='i'?`const.i32 ${x}\n`:constant(x);
        const bytes=assemble(`meta: size=1x1 fuel=1000000\nfn draw ( ) -> ( ):\n${args.slice(0,full?10:6).map(push).join('')}noise.${full?'mask_config':'mask'}\ndrop\nendfn\n`);
        replaceFloatConstants(bytes,floats);return bytes;
    }
    const modified=(i,kind,value)=>good.map((arg,j)=>j===i?[kind,value]:arg);
    for(const i of [0,1,3])for(const component of [0,1])for(const bad of [Infinity,-Infinity,NaN]){
        const v=[...good[i][1]];v[component]=bad;reject(raw(modified(i,'v',v)),i===3?/frequency/:/rectangle/);
    }
    for(const i of [1,3])for(const component of [0,1])for(const bad of [0,-0,-1]){
        const v=[...good[i][1]];v[component]=bad;reject(raw(modified(i,'v',v)),i===3?/frequency/:/rectangle/);
    }
    for(const bad of [-1,6,2147483647])reject(raw(modified(2,'i',bad)),/kind/);
    for(const kind of [0,1,2,3,4,5])for(const bad of [-1,0,9,2147483647]){
        const args=modified(6,'i',bad);args[2]=['i',kind];reject(raw(args),/octaves/);
    }
    for(const bad of [Infinity,-Infinity,NaN])for(const [i,message] of [[4,/threshold/],[7,/softness/],[9,/warp amount/]])reject(raw(modified(i,'f',bad)),message);
    reject(raw(modified(7,'f',-1)),/softness/);for(const bad of [-1,2,2147483647])reject(raw(modified(8,'i',bad)),/comparison/);
    for(const kind of [0,1,2,3,4,5]){
        for(const component of [0,1]){
            const args=modified(2,'i',kind),freq=[1,1];freq[component]=3.4028234663852886e38;args[3]=['v',freq];reject(raw(args),/noise coordinate out of range/);
        }
        const args=modified(2,'i',kind);args[9]=['f',3.4028234663852886e38];reject(raw(args),/noise coordinate out of range/);
    }
    reject(raw(modified(0,'v',[2147483648,0])),/rectangle|mapping/);
    // Exact fuel boundary includes every VM instruction and the whole native
    // reservation. Invalid samples distinguish preflight from post-work bills.
    const fuel=(bytes,n)=>{const copy=Buffer.from(bytes);copy.writeBigUInt64LE(BigInt(n),sections(copy).get(1)+16);return copy;};
    for(const kind of [0,1,2,3,4,5])for(const n of [1,4,8])for(const warp of [0,0.3]){
        const args=modified(2,'i',kind);args[6]=['i',n];args[9]=['f',warp];const bytes=raw(args);
        const work=2+(kind===3||kind===4?25:kind===2||kind===5?4*n:4)+(warp===0?0:32);
        const total=instructions(bytes).length-1+Math.ceil((1+work)/5); // whole opcode at 20%
        generate(fuel(bytes,total));reject(fuel(bytes,total-1),/fuel exhausted/);
        const invalid=args.map((arg,i)=>i===3?['v',[3.4028234663852886e38,1]]:arg);
        reject(fuel(raw(invalid),total-2),/fuel exhausted/);
    }
    // Retain distinct mask locals until they collectively exceed the shared
    // arena. Raise fuel and work only in this test to isolate memory preflight.
    const many=compile(`asset t{size 1x1 draw{${Array.from({length:65},(_,i)=>`m${i}=noise.mask({0,0},{1024,1024},0,{0.01,0.01},0.5,7)`).join(' ')}}}`,['--no-opt','--no-compress']);
    reject(fuel(many,500000000),/generation data exceeds 64 MiB/,false,{workLimit:1000000000});
    for(const full of [false,true]){
        const valid=raw(good,full),opcode=full?0xe4:0xe3;
        const ops=instructions(valid);let cursor=0;
        for(let i=0;i<(full?10:6);++i){
            cursor+=good[i][0]==='v'?3:1;
            const bad=Buffer.from(valid),at=ops[cursor-1];bad[at]=good[i][0]==='i'?0x11:0x10;bad.writeUInt16LE(0,at+2);
            reject(bad,/type mismatch/,true);
        }
        const at=instructions(valid).find(at=>valid[at]===opcode);
        const aux=Buffer.from(valid);aux[at+1]=1;reject(aux,/aux byte must be zero/,true);
        const dead=assemble(`meta: size=1x1\nfn draw ( ) -> ( ):\nreturn\nnoise.${full?'mask_config':'mask'}\nendfn\n`);generate(dead);
        const deadAt=instructions(dead).find(at=>dead[at]===opcode),bad=Buffer.from(dead);bad[deadAt+1]=1;reject(bad,/aux byte must be zero/,true);
    }
    for(const body of ['const mask m=noise.mask({0,0},{1,1},0,{1,1},0,7)',
        'draw{m=noise.mask({0,0},{1,1},0,{1,1},0,7,4)}','draw{m=noise.mask({0,0},{1,1},0.5,{1,1},0,7)}']){
        fs.writeFileSync(base+'.ips',`asset t{size 1x1 ${body}}`);fs.rmSync(base+'.iprop',{force:true});
        const result=spawnSync(tool('ipc'),[base+'.ips','-o',base+'.iprop'],{encoding:'utf8'});
        assert.equal(result.status,1);assert.equal(fs.existsSync(base+'.iprop'),false);++compilerErrors;
    }
    for(const opt of [false,true])generate(compile(fs.readFileSync(path.join(repo,'examples/language/noise-mask.ips'),'utf8'),opt?[]:['--no-opt']));
    return {generations,pins,vectors:cases.length,traps,rejections,compilerErrors,wasm:Boolean(iprop)};
}
module.exports={run};
if(require.main===module){
    const os=require('node:os'),directory=fs.mkdtempSync(path.join(os.tmpdir(),'iprop-noise-mask-'));
    const root=path.resolve(process.argv[2]||'build/tools'),tool=name=>path.join(root,name+(process.platform==='win32'?'.exe':''));
    try{console.log(JSON.stringify(run(null,tool,directory)));}finally{fs.rmSync(directory,{recursive:true,force:true});}
}
