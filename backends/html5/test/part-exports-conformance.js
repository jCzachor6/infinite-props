"use strict";
// Full C++/C/native/WASM and public typed JavaScript result observations.
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),os=require('node:os'),crypto=require('node:crypto');
const {execFileSync,spawnSync}=require('node:child_process');
const {instructions,sections}=require('./sampler-conformance.js');
const {observe:observeBinding}=require('./part-binding-observation.js');
const repo=path.resolve(__dirname,'../../..');
const params=[[0,0,0],[90,0,0],[180,0,0],[270,0,0],[0,1,0],[0,0,1],[90,0,1],[90,1,0]];
const fixtures=path.join(repo,'tests/fixtures'),pins=JSON.parse(fs.readFileSync(path.join(fixtures,'part-exports-v0.9.json')));
function canonical(value){
    if(Array.isArray(value))return value.map(canonical);
    if(value&&typeof value==='object')return Object.fromEntries(Object.keys(value).sort().map(k=>[k,canonical(value[k])]));
    return value;
}
function semantic(record){const {fuel,...result}=record;return result;}
const sha=value=>crypto.createHash('sha256').update(value).digest('hex');
const digest=record=>sha(JSON.stringify(canonical(semantic(record))));
const transform=k=>`rot=${params[k][0]}, mirror_x=${params[k][1]}, mirror_y=${params[k][2]}`;
function sharedBindingCases(iprop,tool,directory) {
    const python=process.env.PYTHON||(process.platform==='win32'?'python':'python3');
    execFileSync(python,[path.join(repo,'backends/test/part_bindings.py'),path.dirname(tool('ipc')),directory],{stdio:'pipe'});
    const cases=JSON.parse(fs.readFileSync(path.join(directory,'part-bindings.json')));
    const reference=JSON.parse(fs.readFileSync(path.join(repo,'backends/test/part-bindings-v0.9.json'))).cases;
    assert.equal(cases.length,23);assert.equal(reference.length,cases.length);
    for(const [index,item] of cases.entries()) {
        const folder=path.join(directory,item.directory),bytes=fs.readFileSync(path.join(folder,'root.iprop'));
        assert.deepEqual([item.name,item.seed,item.size],[reference[index].name,reference[index].seed,reference[index].size]);
        const expected=JSON.parse(fs.readFileSync(path.join(folder,'expected.json')));
        assert.deepEqual(expected,reference[index].expected,'use the frozen shared C++/C observation');
        const options={seed:item.seed,sizeX:item.size[0],sizeY:item.size[1]};
        const result=iprop.generate(bytes,options);
        assert.deepEqual(observeBinding(result),expected,'shared public WASM part fixture: '+item.name);
        iprop.generate(bytes,{...options,seed:item.seed^0xffffffff});
        assert.deepEqual(observeBinding(result),expected,'shared WASM result retains all owned data: '+item.name);
    }
    return cases.length;
}
function run(iprop,tool,directory,probes={}) {
    const owned=!directory;if(owned)directory=fs.mkdtempSync(path.join(os.tmpdir(),'iprop-part-exports-'));
    const dir=path.join(directory,'part-exports');fs.mkdirSync(dir,{recursive:true});
    const base=path.join(dir,'root'),program=base+'.iprop';
    const native=probes.native||path.resolve(path.dirname(tool('ipc')),'../tests/part_exports_probe'+(process.platform==='win32'?'.exe':''));
    if(probes.native)assert(fs.existsSync(native),'requested native result probe exists');
    if(probes.wasm)assert(fs.existsSync(probes.wasm),'requested WASM result probe exists');
    const hasNative=fs.existsSync(native),hasWasm=!!probes.wasm;
    let programs=0,generations=0,pairs=0,records=0,pinned=0,roundtrips=0,formatted=0,traps=0,rejections=0,compilerErrors=0,legacyRejections=0,bindingRejections=0,matrix=0,publicBindingGenerations=0,repeatedBindingErrors=0,sharedBindingScenes=0;
    const configs=Array.from({length:8},(_,i)=>({opt:!!(i&4),debug:!!(i&2),compressed:!!(i&1)}));
    const invoke=(name,args)=>execFileSync(tool(name),args,{encoding:'utf8',stdio:'pipe',maxBuffer:16*1024*1024});
    function compile(text,config=configs[4]) {
        fs.writeFileSync(base+'.ips',text);
        invoke('ipc',[base+'.ips',...(config.opt?[]:['--no-opt']),config.debug?'--debug':'--strip',config.compressed?'--compress':'--no-compress','-o',program]);
        const bytes=fs.readFileSync(program);assert.equal(bytes.subarray(0,8).toString('hex'),'4950525000000000');
        assert.equal(bytes.readUInt32LE(16),0);assert.equal(!!(bytes.readUInt32LE(8)&2),config.compressed);++programs;
        invoke('ipval',[program]);return bytes;
    }
    function assemble(text) {
        fs.writeFileSync(base+'.ipa',text);invoke('ipa',[base+'.ipa','-o',program]);const bytes=fs.readFileSync(program);invoke('ipval',[program]);++programs;return bytes;
    }
    function observe(bytes,seed,size) {
        fs.writeFileSync(program,bytes);const args=[program,String(seed),...size.map(String)];let result;
        if(hasNative){result=JSON.parse(execFileSync(native,args,{encoding:'utf8',maxBuffer:16*1024*1024}));records++;}
        if(hasWasm){const wasm=JSON.parse(execFileSync(process.execPath,[probes.wasm,...args],{encoding:'utf8',maxBuffer:16*1024*1024}));
            if(result){assert.deepEqual(wasm,result,'complete native/WASM result incl. geometry, holes, rasters, mappings, IDs, owners, order, bits, RGBA and fuel');pairs++;}
            result=wasm;records++;}
        if(iprop){const value=iprop.generate(bytes,{seed,sizeX:size[0],sizeY:size[1]});const actual=observeBinding(value);
            if(result){assert.deepEqual(actual,semantic(result),'public binding preserves every typed field/resource/image');
                assert.deepEqual([value.fuel.budget,value.fuel.remaining],result.fuel,'public binding fuel');}
            const saved=JSON.stringify(actual);
            iprop.generate(bytes,{seed:seed^0xffffffff,sizeX:size[0],sizeY:size[1]});
            assert.equal(JSON.stringify(observeBinding(value)),saved,'old public result survives later generation and WASM memory growth');
            if(!result)result={...actual,fuel:[value.fuel.budget,value.fuel.remaining]};
            records++;publicBindingGenerations++;}
        assert(fs.readFileSync(program).equals(bytes),'generation never serializes/mutates the program');
        if(result)generations++;return result;
    }
    function oldPaths(bytes) {
        fs.writeFileSync(program,bytes);const image=base+'-forbidden.png';fs.rmSync(image,{force:true});
        const r=spawnSync(tool('ipgen'),[program,'-o',image],{encoding:'utf8'});
        assert.equal(r.status,1,r.stderr);assert.match(r.stderr,/unsupported.*legacy JSON/);assert.equal(r.stdout,'');assert(!fs.existsSync(image));legacyRejections++;
        if(iprop){const value=iprop.generate(bytes,{seed:7});assert(value.outputs.some(s=>s.kind===3));assert(Array.isArray(value.resources));}
    }
    function reject(bytes,reason,validation=false) {
        fs.writeFileSync(program,bytes);
        for(const [enabled,command,prefix] of [[hasNative,native,[]],[hasWasm,process.execPath,[probes.wasm]]])if(enabled) {
            const r=spawnSync(command,[...prefix,program,'7','0','0'],{encoding:'utf8',maxBuffer:16*1024*1024});
            assert.equal(r.status,validation?3:4,r.stderr);assert.equal(r.stdout,'');if(reason)assert.match(r.stderr,reason);
        }
        if(iprop){assert.throws(()=>iprop.generate(bytes,{seed:7}),error=>error.status===(validation?2:3));bindingRejections++;}
        if(validation){const r=spawnSync(tool('ipval'),[program],{encoding:'utf8'});assert.equal(r.status,1,r.stderr);rejections++;}
        else if(hasNative||hasWasm||iprop)traps++;
    }
    try {
        for(const name of ['middle','leaf','unused','tail','raster-source'])fs.copyFileSync(path.join(fixtures,'part-exports-'+name+'.ips'),path.join(dir,'part-exports-'+name+'.ips'));
        for(const pin of pins.cases) {
            const text=fs.readFileSync(path.join(repo,pin.source),'utf8');let baseline;
            for(const config of configs) {
                const bytes=compile(text,config);oldPaths(bytes);
                const result=observe(bytes,pin.seed,pin.size);
                if(result){assert.equal(result.outputs.length,pin.outputs);assert.equal(result.outputs.reduce((n,s)=>n+s.parts.length,0),pin.parts);
                    assert.equal(result.resources.length,pin.resources);assert.equal(digest(result),pin.semantic_sha256,pin.name+' complete frozen semantic digest');
                    if(config.opt)assert.equal(result.fuel[0]-result.fuel[1],pin.default_fuel_used,pin.name+' implementation fuel pin');pinned++;}
                if(config.opt&&!config.debug&&!config.compressed)baseline={bytes,result};
                if(!config.compressed) {
                    const restored=assemble(invoke('ipdis',[program]));const output=observe(restored,pin.seed,pin.size);
                    if(result)assert.deepEqual(output,result,'IPA/IPDIS retains complete typed result and fuel');roundtrips++;
                }
            }
            assert(baseline);fs.writeFileSync(program,baseline.bytes);
            invoke('ipc',[base+'.ips','--format-only','--format',base+'-formatted.ips']);
            const output=observe(compile(fs.readFileSync(base+'-formatted.ips','utf8')),pin.seed,pin.size);
            if(baseline.result)assert.deepEqual(output,baseline.result,'formatter retains complete typed result');formatted++;
            if(pin.name==='render-order'&&output) {
                assert.deepEqual(output.rgba.slice((1*4+1)*4,(1*4+1)*4+4),[18,52,86,192]);
                assert.deepEqual(output.rgba.slice((1*4+2)*4,(1*4+2)*4+4),[255,238,204,64]);
                assert.deepEqual(output.outputs[0].parts.map(p=>[p.entry,p.id]),[[0,0],[1,0],[2,0]]);
            }
            if(pin.name==='mapped-raster'&&output) {
                const parts=output.outputs[0].parts;
                assert.deepEqual(parts[0].source_mapping,[7,-5,4]);assert.deepEqual(parts[1].texture_mapping,[7,-5,4]);
                assert.equal(parts[0].shape[1],parts[1].appearance[1]);assert.equal(parts[0].shape[1],parts[2].shape[1]);
                assert.equal(output.resources[0].format,1);assert.equal(output.resources[1].format,2);
            }
            if(pin.name==='shared-regions'&&output) {
                assert.deepEqual(output.outputs[0].parts.map(p=>p.shape),[[1,1,0],[1,1,1],[1,1,2]]);
                assert.equal(output.resources.length,1);
            }
        }
        const originalRoot=fs.readFileSync(path.join(fixtures,'part-exports-nested.ips'),'utf8');
        const originalMiddle=fs.readFileSync(path.join(fixtures,'part-exports-middle.ips'),'utf8');
        for(let outer=0;outer<8;++outer)for(let inner=0;inner<8;++inner) {
            fs.writeFileSync(path.join(dir,'part-exports-middle.ips'),originalMiddle.replace('rot=90, mirror_x=1, mirror_y=0',transform(inner)));
            const bytes=compile(originalRoot.replace('rot=90, mirror_x=0, mirror_y=0',transform(outer)),configs[(outer+inner)%8]);
            for(const size of [[24,20],[48,60],[18,25],[30,30]])for(const seed of [(outer*7+inner*3)&7,(outer*7+inner*3+3)&7]) {
                const result=observe(bytes,seed,size);
                if(result){assert.deepEqual(result.outputs.map(o=>o.owner),[0,2,3,3,4]);assert.equal(result.resources.length,2);
                    assert.deepEqual(result.outputs.map(o=>o.parts.length),[0,0,4,1,0]);}
                matrix++;
            }
        }
        oldPaths(compile('import tail from "part-exports-tail.ips" asset t{size 1x1 draw{}}'));
        const baseBody='p=part.create(regions(voronoi({0,0},{2,2},[{1,1}])),0)';
        for(const text of [
            `asset t{size 2x2 output list part pieces draw{${baseBody}for i in 0..257{pieces=p}}}`,
            `asset t{size 2x2 output list part pieces draw{for i in 0..257{${baseBody}}}}`,
            `asset t{size 2x2 output list part pieces draw{${baseBody}pieces=p part.draw(p)}}`
        ])reject(compile(text),/3000:/);
        fs.writeFileSync(path.join(dir,'far.ips'),`asset t{size 2x2 output list part pieces draw{${baseBody}pieces=part.place(p,{2147483520.0,0},0,0,0)}}`);
        reject(compile('import far from "far.ips" asset t{size 2x2 draw{far.draw(7,at={256,0})}}'),/3010:/);
        fs.writeFileSync(path.join(dir,'child.ips'),`asset t{size 2x2 output list part pieces draw{${baseBody}pieces=p pieces=p}}`);
        reject(compile(`import child from "child.ips" asset t{size 2x2 output list part pieces draw{${baseBody}for i in 0..255{pieces=p}child.draw(7,at={0,0})}}`),/3000:/);
        const unassigned=assemble('meta: size=1x1\noutput: list part pieces\ntypes: part2d\nfn draw ( ) -> ( ):\nlocals: part2d\nlocal.get 0\nout.list.part pieces\nendfn\n');
        reject(unassigned,/3000:/);
        const fuel=compile(fs.readFileSync(path.join(fixtures,'part-exports-order.ips'),'utf8'));
        const fuelCopy=Buffer.from(fuel),cost=pins.cases.find(c=>c.source.endsWith('part-exports-order.ips')).default_fuel_used;
        fuelCopy.writeBigUInt64LE(BigInt(cost-1),sections(fuel).get(1)+16);reject(fuelCopy,/3001:/);

        const plain=compile(fs.readFileSync(path.join(repo,'backends/capi/test/part-output-values.ips'),'utf8'),configs[0]);
        const meta=sections(plain).get(1),first=meta+36,code=instructions(plain),appends=code.filter(at=>plain[at]===0x69);
        assert.equal(appends.length,6);
        const mutate=fn=>{const copy=Buffer.from(plain);fn(copy);reject(copy,null,true);};
        mutate(b=>{b[first]=4;});mutate(b=>{b[first+1]=0;});mutate(b=>{b[first+1]=2;});
        mutate(b=>{b[first+2]=1;});mutate(b=>b.writeUInt32LE(0xffffffff,first+4));
        mutate(b=>b.writeUInt32LE(b.readUInt32LE(first+4),first+12));
        for(const at of appends){mutate(b=>{b[at+1]=1;});mutate(b=>b.writeUInt16LE(0xffff,at+2));mutate(b=>b.writeUInt16LE(3,at+2));}
        const load=appends.find(at=>plain[at-4]===0x13);assert(load);
        mutate(b=>{b[load-4]=0x10;b.writeUInt16LE(0,load-2);});
        const dead=assemble('meta: size=1x1\noutput: list part pieces\nfn draw ( ) -> ( ):\nreturn\nout.list.part pieces\nendfn\n');
        observe(dead,0,[0,0]);const deadAt=instructions(dead).find(at=>dead[at]===0x69);assert(deadAt);
        {const bad=Buffer.from(dead);bad[deadAt+1]=1;reject(bad,/aux byte/,true);}
        {const bad=Buffer.from(dead);bad.writeUInt16LE(0xffff,deadAt+2);reject(bad,/slot index/,true);}
        const exportsAt=sections(dead).get(10);assert.equal(dead.readUInt32LE(exportsAt),0);
        {const bad=Buffer.from(dead);bad.writeUInt32LE(0,exportsAt+4);reject(bad,/owner count/,true);}
        {const bad=Buffer.from(dead);bad.writeUInt32LE(0,exportsAt+8);reject(bad,/owner out of range/,true);}
        {const bad=Buffer.from(dead),count=bad.readUInt32LE(12);const index=Array.from({length:count},(_,i)=>i).find(i=>bad.readUInt32LE(20+i*12)===10);
            for(let i=index+1;i<count;++i)bad.copy(bad,20+(i-1)*12,20+i*12,32+i*12);bad.writeUInt32LE(count-1,12);reject(bad,/entry function owner/,true);}
        for(const text of [
            'asset t{size 1x1 output part pieces draw{}}',
            'asset t{size 1x1 output list part pieces draw{pieces=1}}',
            'asset t{size 1x1 output list part pieces draw{pieces={1,2}}}',
            'asset t{size 1x1 output list part pieces draw{x=pieces}}',
            'asset t{size 1x1 output list part pieces output point pieces draw{}}',
            'asset t{size 1x1 output list part pieces draw{layer image{pixel(0,0,#fff)}pieces=image}}',
            'asset t{size 1x1 f(p:list<part2d>){} draw{}}',
            'import tail from "part-exports-tail.ips" asset t{size 1x1 draw{x=tail.outputs.empty}}'
        ]) {
            fs.writeFileSync(base+'-bad.ips',text);fs.rmSync(base+'-bad.iprop',{force:true});
            const r=spawnSync(tool('ipc'),[base+'-bad.ips','-o',base+'-bad.iprop'],{encoding:'utf8'});
            assert.equal(r.status,1,r.stderr);assert(!fs.existsSync(base+'-bad.iprop'));compilerErrors++;
        }
        if(iprop){
            sharedBindingScenes=sharedBindingCases(iprop,tool,path.join(dir,'shared-bindings'));
            const failed=compile('asset t{size 2x2 output list part pieces draw{p=part.create(regions(voronoi({0,0},{2,2},[{1,1}])),0)pieces=p part.draw(p)}}');
            for(let i=0;i<2048;++i){assert.throws(()=>iprop.generate(failed,{seed:i}),e=>e instanceof Error&&e.status===3&&e.stage===4&&e.reason===3000);repeatedBindingErrors++;}
            const good=compile(fs.readFileSync(path.join(fixtures,'part-exports-order.ips'),'utf8'));
            assert.equal(digest(observeBinding(iprop.generate(good,{seed:7}))),pins.cases.find(p=>p.name==='render-order').semantic_sha256,'valid result after repeated failures');
        }
        return {passed:true,programs,generations,fullRecords:records,nativeWasmPairs:pairs,pins:pins.cases.length,pinComparisons:pinned,
            matrixGenerations:matrix,roundtrips,formatted,traps,rejections,compilerErrors,legacyRejections,bindingRejections,
            internalNative:hasNative,internalWasm:hasWasm,checkedCapi:hasNative||!!iprop,publicBindingTypedParts:!!iprop,
            publicBindingGenerations,repeatedBindingErrors,sharedBindingScenes,typedObservationSkipped:!hasNative&&!hasWasm&&!iprop};
    } finally {if(owned)fs.rmSync(directory,{recursive:true,force:true});}
}
module.exports={run,canonical,digest};
if(require.main===module){const tools=path.resolve(process.argv[2]||path.join(repo,'build/tools'));
    console.log(JSON.stringify(run(null,n=>path.join(tools,n),undefined,{native:process.argv[3],wasm:process.argv[4]})));}
