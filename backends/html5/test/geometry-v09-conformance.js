"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),os=require("node:os"),path=require("node:path");
const {execFileSync,spawnSync}=require("node:child_process");
const {decodePng}=require("./png.js");
const {sections}=require("./harness.js");
const repo=path.resolve(__dirname,"../../.."),names=["diagram2d","contours2d","regions2d","part2d","parts2d"];
const bits=n=>{const b=Buffer.alloc(4);b.writeFloatLE(n);return b.readUInt32LE();};

// Independently evaluate the explicit polygon fixtures, with signed holes.
// No runtime calls or native-generated pins enter this reference.
function reference(v){
    const metric=ring=>{
        let cross=0,x=0,y=0;
        ring.vertices.forEach((p,i)=>{
            const a=v.points[p],b=v.points[ring.vertices[(i+1)%ring.vertices.length]],d=a[0]*b[1]-b[0]*a[1];
            cross+=d;x+=(a[0]+b[0])*d;y+=(a[1]+b[1])*d;
        });return {area:cross/2,x:x/6,y:y/6};
    };
    const regions=v.regions.map(r=>{
        const rings=[r.outer,...r.holes].map(i=>metric(v.rings[i]));
        const area=rings.reduce((s,r)=>s+r.area,0),center=["x","y"].map(k=>rings.reduce((s,r)=>s+r[k],0)/area);
        const points=v.rings[r.outer].vertices.map(i=>v.points[i]);
        const lo=[0,1].map(k=>Math.min(...points.map(p=>p[k]))),hi=[0,1].map(k=>Math.max(...points.map(p=>p[k])));
        return {id:r.id,bits:[area,...center,...lo,hi[0]-lo[0],hi[1]-lo[1]].map(bits),holes:r.holes,neighbors:r.neighbors};
    });
    const provenance=v.edges.map(e=>[...e,4294967295,-1,-1,0]);
    if(v.name==="adjacent")provenance[1]=[1,4,1,0,1,1];
    return {points:v.points.length,edges:v.edges.length,uses:v.rings.reduce((s,r)=>s+r.vertices.length,0),
        rings:v.rings.length,regions,topology:v.rings,edge_provenance:provenance,bytes:v.retained_bytes,fuel:v.fuel};
}

function run(iprop,tool,directory,probes={}){
    const owned=!directory;if(owned)directory=fs.mkdtempSync(path.join(os.tmpdir(),"iprop-geometry-v09-"));
    const base=path.join(directory,"geometry-v09");let generations=0,rejections=0,traps=0,compilerErrors=0;
    try{
        const vectors=JSON.parse(fs.readFileSync(path.join(repo,"tests/fixtures/geometry-v0.9.json"))).cases;
        const expected={};for(const v of vectors){assert.deepEqual(reference(v),v.expected,"independent topology/metadata "+v.name);expected[v.name]=v.expected;}
        expected.released_bytes=0;expected.limits={};
        for(const v of vectors)expected.limits[v.name]={peak_bytes:v.peak_bytes,fuel_boundary:true};
        expected.handles={types:names,bytes:1800,shared:true,helper_stream:26,released_bytes:0};
        const nativeProbe=probes.native||path.resolve(path.dirname(tool("ipc")),"../tests/geometry_representation_probe"+(process.platform==="win32"?".exe":""));
        const nativeRepresentation=!!probes.native||fs.existsSync(nativeProbe);
        if(nativeRepresentation){
            const native=JSON.parse(execFileSync(nativeProbe,[],{encoding:"utf8"}));
            assert.deepEqual(native,expected,"native geometry representation pins");
        }
        if(probes.wasm){
            const wasm=JSON.parse(execFileSync(process.execPath,[probes.wasm],{encoding:"utf8"}));
            assert.deepEqual(wasm,expected,"WASM geometry representation pins");
        }
        function compile(source,flags=[]){
            fs.writeFileSync(base+".ips",source);execFileSync(tool("ipc"),[base+".ips",...flags,"-o",base+".iprop"],{stdio:"pipe"});
            return Buffer.from(fs.readFileSync(base+".iprop"));
        }
        function assemble(text){
            fs.writeFileSync(base+".ipa",text);execFileSync(tool("ipa"),[base+".ipa","-o",base+".iprop"],{stdio:"pipe"});
            return Buffer.from(fs.readFileSync(base+".iprop"));
        }
        function generate(bytes){
            fs.writeFileSync(base+".iprop",bytes);const result=JSON.parse(execFileSync(tool("ipgen"),[base+".iprop","--seed","26","-o",base+".png"],{encoding:"utf8"}));
            const image=decodePng(fs.readFileSync(base+".png"));assert.deepEqual(Array.from(image.data),[17,34,51,255]);
            if(iprop){const wasm=iprop.generate(new Uint8Array(bytes),{seed:26});assert.deepEqual(Buffer.from(wasm.data),Buffer.from(image.data));assert.deepEqual(wasm.outputs,[]);}
            assert.equal(image.width,1);assert.equal(image.height,1);assert.deepEqual(result.outputs,[]);++generations;
        }
        function reject(bytes,message,validation){
            fs.writeFileSync(base+".iprop",bytes);fs.rmSync(base+"-failed.png",{force:true});
            const r=spawnSync(tool(validation?"ipval":"ipgen"),[base+".iprop",...(validation?[]:["-o",base+"-failed.png"])],{encoding:"utf8"});
            assert.equal(r.signal,null);assert.equal(r.status,1,r.stderr);assert.match(r.stderr,message);
            if(iprop)assert.throws(()=>iprop.generate(new Uint8Array(bytes),{seed:26}),message);
            assert.equal(fs.existsSync(base+"-failed.png"),false,"failed geometry must not publish a result");
            if(validation)++rejections;else ++traps;
        }
        for(const name of names){
            const source=`asset t{size 1x1 echo(v:${name})->${name}{${name} copy=v if(true){copy=v}return copy} draw{pixel(0,0,#112233)}}`;
            for(const opt of [false,true])for(const compressed of [false,true])for(const debug of [false,true]){
                const bytes=compile(source,[...(opt?[]:["--no-opt"]),...(compressed?[]:["--no-compress"]),debug?"--debug":"--strip"]);
                generate(bytes);
                if(!compressed){
                    assert.equal(bytes.readUInt32LE(4),0);assert.equal(bytes.readUInt32LE(16),0);
                    const at=sections(bytes).get(6)+4; // existing type-descriptor section
                    assert.equal(bytes[at],15+names.indexOf(name));assert.equal(bytes[at+1],8);assert.equal(bytes.readUInt16LE(at+2),0);
                    const text=execFileSync(tool("ipdis"),[base+".iprop"],{encoding:"utf8"});assert.match(text,new RegExp(name));generate(assemble(text));
                    if(!opt&&!debug)for(const [delta,value] of [[0,20],[1,0],[2,1],[3,1]]){const bad=Buffer.from(bytes);bad[at+delta]=value;reject(bad,/invalid parameterized type descriptor/,true);}
                    if(!opt&&!debug)for(const kind of [5,10,14]){const bad=Buffer.from(bytes);bad[at]=kind;bad[at+1]=15+names.indexOf(name);reject(bad,/invalid parameterized type descriptor/,true);}
                }
            }
            const unset=assemble(`meta: size=1x1\ntypes: ${name}\nfn draw ( ) -> ( ):\nlocals: ${name}\nlocal.get 0\ndrop\nreturn\nendfn\n`);
            reject(unset,new RegExp(name+" is unassigned"),false);
            for(const declaration of [`input ${name} v=0`,`const ${name} v=0`,...['list','sampler','stepper'].map(c=>`f(v:${c}<${name}>) {}`)]){
                fs.writeFileSync(base+".ips",`asset t{size 1x1 ${declaration} draw{}}`);
                const r=spawnSync(tool("ipc"),[base+".ips","--check"],{encoding:"utf8"});assert.equal(r.status,1,r.stderr);++compilerErrors;
            }
        }
        return {passed:true,types:names.length,vectors:vectors.length,generations,rejections,traps,compilerErrors,
            nativeRepresentation,wasmRepresentation:!!probes.wasm,wasm:!!iprop};
    }finally{if(owned)fs.rmSync(directory,{recursive:true,force:true});}
}
module.exports={run,reference};
if(require.main===module){const tools=path.resolve(process.argv[2]);console.log(JSON.stringify(run(null,n=>path.join(tools,n),undefined,{native:process.argv[3]})));}
