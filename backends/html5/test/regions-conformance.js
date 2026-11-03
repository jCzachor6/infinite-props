"use strict";
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),os=require('node:os'),crypto=require('node:crypto');
const {execFileSync,spawnSync}=require('node:child_process'),{decodePng}=require('./png.js'),{instructions}=require('./sampler-conformance.js');
const reference=require('../../../tests/reference/regions-reference.js');
const fixture=path.resolve(__dirname,'../../../tests/fixtures/regions-v0.9.json');
const digest=v=>crypto.createHash('sha256').update(JSON.stringify(v)).digest('hex');
const numeric=n=>Math.fround(n).toString(),vector=p=>'{'+p.map(numeric).join(',')+'}';
function publicInput(c){
    if(c.diagram){const d=c.diagram;return{size:[16,12],setup:`d=voronoi(${vector(d.origin)},${vector(d.size)},[${d.sites.map(vector).join(',')}])`,source:'d'};}
    if(c.pixels){const p=c.pixels;return{size:[p[0].length,p.length],setup:`layer image(alpha=surface.source_over){${p.flatMap((row,y)=>row.map((a,x)=>`pixel(${x},${y},#ffffff${a.toString(16).padStart(2,'0')})`)).join(' ')}} c=contour(mask.from(image),0.5)`,source:'c'};}
    if(c.name.startsWith('rectangle-'))return{size:[8,6],setup:'layer image{rect(0,0,8,6,#fff)} c=contour(mask.from(image),0.5)',source:'c'};
    return null;
}
function source(c,record,mode='union',short=false){
    const input=publicInput(c);if(!input)return null;
    const allowed=record.result.regions.flatMap((r,i)=>!r.holes.length&&record.result.rings[r.outer].vertices.length<=256?[i]:[]);
    const points=allowed.length?`if(${allowed.map(i=>'i=='+i).join('||')}){ps=regions.points(rs,i) trace={list.len(ps),-5}for k in 0..list.len(ps){trace=ps[k]}}`:'';
    const conversion=short?`regions(${input.source})`:`regions(min_area=${numeric(c.minArea||0)},source=${input.source},inset=${numeric(c.inset||0)})`;
    const mask=mode==='single'?'regions.mask(rs,0)':'regions.mask(rs)';
    const paint=mode==='clip'?'layer clipped(alpha=surface.source_over){clip(m){rect(0,0,'+input.size.join(',')+',#fff)}} surface.paint(clipped,blend=surface.source_over)':
        mode==='legacy'?'surface.paint(m,#fff,blend=surface.legacy)':
        mode==='algebra'?'surface.paint(mask.intersect(m,mask.union(m,m)),#fff,blend=surface.source_over)':'surface.paint(m,#fff,blend=surface.source_over)';
    const capture=mode==='creation-in-clip'?`layer empty{} clip(mask.from(empty)){m=${mask}}`:`m=${mask}`;
    return `asset t{size ${input.size.join('x')} output list point trace echo(rs:regions2d)->regions2d{return rs}
    draw{trace={-1,unit()} ${input.setup} rs=echo(${conversion}) copy=choose(7,rs,rs)
    count=regions.count(copy) trace={count,-3}
    for i in 0..count{trace={regions.id(rs,i),regions.area(rs,i)} trace=regions.center(rs,i)
        adjacent=regions.neighbors(rs,i) trace={list.len(adjacent),-4}for j in 0..list.len(adjacent){trace={i,adjacent[j]}} ${points}}
    ${capture} ${paint} trace={-2,unit()}}}`;
}
function expectedTrace(record,control){
    const b=reference.bits,result=record.result,trace=[control[0],[b(result.regions.length),b(-3)]];
    for(let i=0;i<result.regions.length;i++){
        const r=result.regions[i];trace.push([b(r.source),r.area],r.center,[b(r.neighbors.length),b(-4)]);
        for(const n of r.neighbors)trace.push([b(i),b(n)]);
        const ring=result.rings[r.outer];if(!r.holes.length&&ring.vertices.length<=256){trace.push([b(ring.vertices.length),b(-5)]);for(const v of ring.vertices)trace.push(result.points[v]);}
    }trace.push(control[1]);return trace.map(v=>Array.isArray(v)?{x:v[0],y:v[1]}:v);
}
function run(iprop,tool,directory,probes={}){
    const owned=!directory;if(owned)directory=fs.mkdtempSync(path.join(os.tmpdir(),'iprop-regions-'));
    const base=path.join(directory,'regions'),corpus=JSON.parse(fs.readFileSync(fixture));
    const nativeProbe=probes.native||path.resolve(path.dirname(tool('ipc')),'../tests/regions_probe'+(process.platform==='win32'?'.exe':'')),hasNative=fs.existsSync(nativeProbe);
    let generations=0,topologies=0,traps=0,rejections=0,compilerErrors=0;const resourceBoundaries=[];
    const nativeRun=(name,args)=>execFileSync(tool(name),args,{encoding:'utf8',stdio:'pipe'});
    function compile(text,flags=[]){fs.writeFileSync(base+'.ips',text);nativeRun('ipc',[base+'.ips',...flags,'-o',base+'.iprop']);return fs.readFileSync(base+'.iprop');}
    function assemble(text){fs.writeFileSync(base+'.ipa',text);nativeRun('ipa',[base+'.ipa','-o',base+'.iprop']);return fs.readFileSync(base+'.iprop');}
    function generate(bytes,options={},record,maskIndex=-1){
        options={seed:26,...options};fs.writeFileSync(base+'.iprop',bytes);
        const output=JSON.parse(nativeRun('ipgen',[base+'.iprop','--float-bits','--seed',String(options.seed),...(options.sizeX?['--size',options.sizeX+'x'+options.sizeY]:[]),'-o',base+'.png']));
        const image=decodePng(fs.readFileSync(base+'.png'));
        if(record){
            const mask=record.mask,expected=[];
            for(let y=0;y<image.height;y++)for(let x=0;x<image.width;x++){
                const mx=x-mask.origin[0],my=y-mask.origin[1],a=mx>=0&&my>=0&&mx<mask.size[0]&&my<mask.size[1]?mask.pixels[my*mask.size[0]+mx]:0;
                expected.push(record.legacy?a:a?255:0,record.legacy?a:a?255:0,record.legacy?a:a?255:0,record.legacy?(a===255?255:0):a);
            }assert.deepEqual([...image.data],expected,'public paint/clip reproduces independent mask including off-target bounds');
        }
        if(iprop){const wasm=iprop.generate(new Uint8Array(bytes),options);assert.deepEqual([...wasm.data],[...image.data],'regions native/WASM whole canvas');
            assert.deepEqual(wasm.outputs.map(s=>({name:s.name,kind:['point','rect','circle'][s.kind],list:s.isList,values:s.valueBits})),output.outputs,'regions metadata and RNG');}
        ++generations;return output.outputs;
    }
    function reject(bytes,message,validation=false){
        fs.writeFileSync(base+'.iprop',bytes);fs.rmSync(base+'-failed.png',{force:true});
        const result=spawnSync(tool(validation?'ipval':'ipgen'),[base+'.iprop',...(validation?[]:['--seed','26','-o',base+'-failed.png'])],{encoding:'utf8'});
        assert.equal(result.signal,null);assert.equal(result.status,1,result.stderr);assert.match(result.stderr,message);assert(!fs.existsSync(base+'-failed.png'),'failed generation publishes no PNG');
        if(iprop)assert.throws(()=>iprop.generate(new Uint8Array(bytes),{seed:26}),message);if(validation)++rejections;else ++traps;
    }
    const records=new Map();
    try{
        const control=generate(compile('asset t{size 1x1 output list point trace draw{trace={-1,unit()}trace={-2,unit()}}}'))[0].values;
        for(const c of corpus.cases){
            let data;
            if(hasNative){data=JSON.parse(execFileSync(nativeProbe,reference.argumentsOf(c)));reference.check(c,data);assert.equal(digest(data),c.geometry_sha256,c.name+' frozen complete record');++topologies;}
            else {data=c.public_record;if(data){reference.check(c,data);assert.equal(digest(data),c.geometry_sha256);}}
            if(probes.wasm){const wasm=JSON.parse(execFileSync(process.execPath,[probes.wasm,...reference.argumentsOf(c)]));reference.check(c,wasm);assert.equal(digest(wasm),c.geometry_sha256,c.name+' WASM complete record');if(data)assert.deepEqual(wasm,data);}
            if(!data||!publicInput(c))continue;records.set(c.name,data);
            for(const optimize of[false,true])for(const debug of[false,true])for(const compressed of[false,true]){
                const flags=[...(optimize?[]:['--no-opt']),debug?'--debug':'--strip',...(compressed?[]:['--no-compress'])],text=source(c,data);
                const bytes=compile(text,flags),output=generate(bytes,{},data);assert.deepEqual(output[0].values,expectedTrace(data,control),'all public metadata and points');
                if(!compressed)assert.deepEqual(generate(assemble(nativeRun('ipdis',[base+'.iprop'])),{},data),output,'IPA/IPDIS round trip');
            }
            nativeRun('ipc',[base+'.ips','--format',base+'-formatted.ips','--format-only']);const formatted=fs.readFileSync(base+'-formatted.ips','utf8');assert.deepEqual(generate(compile(formatted),{},data)[0].values,expectedTrace(data,control),'formatter round trip');
            for(const mode of['clip','algebra','creation-in-clip','legacy'])assert.deepEqual(generate(compile(source(c,data,mode)),{},{...data,legacy:mode==='legacy'})[0].values,expectedTrace(data,control));
            if(!c.inset&&!c.minArea)assert.deepEqual(generate(compile(source(c,data,'union',true)),{},data)[0].values,expectedTrace(data,control),'short defaults equal full');
            if(data.result.regions.length){const selected={...c,maskIndex:0},one=hasNative?JSON.parse(execFileSync(nativeProbe,reference.argumentsOf(selected))):{...data,mask:reference.makeMask(data.result,(c.scale||[1,1]),0)};reference.check(selected,one);
                assert.deepEqual(generate(compile(source(c,data,'single')),{},one)[0].values,expectedTrace(data,control),'single region including holes');}
        }
        for(const c of corpus.cases.filter(v=>v.diagram)){
            const scaled={...c,scale:[2,3]},original=records.get(c.name),data=hasNative?JSON.parse(execFileSync(nativeProbe,reference.argumentsOf(scaled))):{...original,mask:reference.makeMask(original.result,[2,3])};reference.check(scaled,data);
            generate(compile(source(c,data)),{sizeX:32,sizeY:36},data);
            if(probes.wasm)assert.deepEqual(JSON.parse(execFileSync(process.execPath,[probes.wasm,...reference.argumentsOf(scaled)])),data);
        }
        {const c=corpus.cases.find(v=>v.name==='diagram-off-target'),data=records.get(c.name);
            const moved={...data,mask:{...data.mask,origin:data.mask.origin.map(v=>v-20)}};
            generate(compile(source(c,data).replace('surface.paint(m,','surface.paint(mask.offset(m,-20,-20),')),{},moved);
        }
        if(hasNative)for(const name of['rectangle-inset','dumbbell-split','hole-inset']){
            const c=corpus.cases.find(v=>v.name===name),normal=JSON.parse(execFileSync(nativeProbe,reference.argumentsOf(c)));
            const probe=(budget,occupied,wasm=false)=>spawnSync(wasm?process.execPath:nativeProbe,[...(wasm?[probes.wasm]:[]),...reference.argumentsOf(c,budget,occupied)],{encoding:'utf8'});
            assert.equal(probe(normal.fuel,0).status,0);assert.equal(probe(normal.fuel-1,0).status,4);assert.match(probe(normal.fuel-1,0).stderr,/fuel exhausted/);
            let lo=0,hi=64*1024*1024-normal.sourceBytes;
            while(lo+1<hi){const mid=Math.floor((lo+hi)/2),r=probe(1e9,mid);if(r.status===0)lo=mid;else{assert.equal(r.status,4);assert.match(r.stderr,/64 MiB/);hi=mid;}}
            for(const wasm of probes.wasm?[false,true]:[false]){
                const fits=probe(1e9,lo,wasm),fails=probe(1e9,lo+1,wasm);assert.equal(fits.status,0,fits.stderr);assert.deepEqual(JSON.parse(fits.stdout),normal);
                assert.equal(fails.status,4);assert.match(fails.stderr,/64 MiB/);assert.equal(probe(normal.fuel-1,0,wasm).status,4);
            }resourceBoundaries.push({name,fuel:normal.fuel,minimumFreeBytes:64*1024*1024-lo});
        }
        for(const optimize of[false,true]){
            const flags=optimize?[]:['--no-opt'];
            for(const body of['regions.count(rs)','regions.id(rs,0)','regions.area(rs,0)','regions.center(rs,0)','regions.neighbors(rs,0)','regions.points(rs,0)','regions.mask(rs)','regions.mask(rs,0)'])
                reject(compile(`asset t{size 1x1 draw{if(seed==0){rs=regions(voronoi({0,0},{1,1},1,0))}${body}}}`,flags),/unassigned/);
            reject(compile('asset t{size 1x1 draw{if(seed==0){d=voronoi({0,0},{1,1},1,0)}regions(d)}}',flags),/unassigned/);
            reject(compile('asset t{size 1x1 draw{if(seed==0){layer image{} c=contour(mask.from(image),0.5)}regions(c,0,0)}}',flags),/unassigned/);
            for(const body of['regions.id(rs,-1)','regions.area(rs,1)','regions.center(rs,1)','regions.neighbors(rs,1)','regions.points(rs,1)','regions.mask(rs,1)'])
                reject(compile(`asset t{size 1x1 draw{rs=regions(voronoi({0,0},{1,1},1,0))${body}}}`,flags),/index/);
            for(const tail of['float(seed-seed)/float(seed-seed),0','0,-1','0,1.0/0.0'])
                reject(compile(`asset t{size 1x1 draw{d=voronoi({0,0},{1,1},1,0)regions(d,${tail})}}`,flags),/inset|min_area/);
        }
        reject(compile('asset t{size 3x3 draw{layer image{rect(0,0,3,1,#fff)rect(0,2,3,1,#fff)rect(0,1,1,1,#fff)rect(2,1,1,1,#fff)}rs=regions(contour(mask.from(image),0.5))regions.points(rs,0)}}'),/without holes/);
        reject(compile('asset t{size 8x8 draw{rs=regions(voronoi({0,0},{8,8},1,0),-100,0)regions.points(rs,0)}}'),/256 vertices/);
        reject(compile('asset t{size 1x1 draw{d=voronoi({2147483000,0},{1024,8},1,0)regions.mask(regions(d))}}'),/mapping/);
        for(const [id,mnemonic]of[[0xf0,'regions.diagram'],[0xf1,'regions.diagram.config'],[0xf2,'regions.contours'],[0xf3,'regions.contours.config'],[0xf4,'regions.count'],[0xf5,'regions.id'],[0xf6,'regions.area'],[0xf7,'regions.center'],[0xf8,'regions.neighbors'],[0xf9,'regions.points'],[0xfa,'regions.mask'],[0xfb,'regions.mask_all']]){
            const bytes=assemble(`meta: size=1x1\nfn draw ( ) -> ( ):\nreturn\n${mnemonic}\ndrop\nendfn\n`),at=instructions(bytes).find(p=>bytes[p]===id);assert(at!==undefined);
            for(const delta of[1,2,3]){const bad=Buffer.from(bytes);bad[at+delta]=1;reject(bad,/aux|operand|zero|reserved/,true);}
        }
        for(const body of['regions(1)','d=voronoi({0,0},{1,1},1,0)regions(d,1)','d=voronoi({0,0},{1,1},1,0)regions(d,inset=1)',
            'regions.count(voronoi({0,0},{1,1},1,0))','regions.mask(1)','regions.points(1,0)','rs=regions(voronoi({0,0},{1,1},1,0))regions.inset(rs,1)']){
            fs.writeFileSync(base+'.ips',`asset t{size 1x1 draw{${body}}}`);const r=spawnSync(tool('ipc'),[base+'.ips','--check'],{encoding:'utf8'});assert.equal(r.status,1,r.stderr);++compilerErrors;
        }
        return{passed:true,vectors:corpus.cases.length,publicVectors:records.size,topologies,generations,traps,rejections,compilerErrors,resourceBoundaries,nativeRepresentation:hasNative,wasmRepresentation:!!probes.wasm,wasm:!!iprop};
    }finally{if(owned)fs.rmSync(directory,{recursive:true,force:true});}
}
module.exports={run,source,publicInput,digest};
if(require.main===module){const tools=path.resolve(process.argv[2]);console.log(JSON.stringify(run(null,n=>path.join(tools,n),undefined,{native:process.argv[3],wasm:process.argv[4]})));}
