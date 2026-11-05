"use strict";
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),os=require('node:os'),crypto=require('node:crypto');
const {execFileSync,spawnSync}=require('node:child_process'),{decodePng}=require('./png.js');
const reference=require('../../../tests/reference/gap-bevel-reference.js');
const {instructions,outputBits}=require("./harness.js");
const fixture=path.resolve(__dirname,'../../../tests/fixtures/gap-bevel-v0.9.json');
const digest=v=>crypto.createHash('sha256').update(JSON.stringify(v)).digest('hex');
const numeric=n=>Math.fround(n).toString(),vector=p=>'{'+p.map(numeric).join(',')+'}';
const color=p=>'#'+p.map(n=>n.toString(16).padStart(2,'0')).join('');
function publicInput(c){
    if(c.internalOnly)return null;
    if(c.diagram){const d=c.diagram;return{size:[20,20],setup:`src=voronoi(${vector(d.origin)},${vector(d.size)},[${d.sites.map(vector).join(',')}])`};}
    if(c.pixels){const p=c.pixels;return{size:[p[0].length,p.length],setup:`layer source_image(alpha=surface.source_over){${p.flatMap((row,y)=>row.map((a,x)=>`pixel(${x},${y},#ffffff${a.toString(16).padStart(2,'0')})`)).join(' ')}} src=contour(mask.from(source_image),0.5)`};}
    if(c.name.startsWith('rectangle-'))return{size:[8,6],setup:'layer source_image{rect(0,0,8,6,#fff)}src=contour(mask.from(source_image),0.5)'};
    return null;
}
function source(c,record,mode='layer',short=false){
    const input=publicInput(c);if(!input)return null;
    const selected=record.result.regions.flatMap((r,i)=>!r.holes.length&&record.result.rings[r.outer].vertices.length<=256?[i]:[]);
    const points=selected.length?`if(${selected.map(i=>'i=='+i).join('||')}){ps=regions.points(rs,i)trace={list.len(ps),-5}for j in 0..list.len(ps){trace=ps[j]}}`:'';
    const gap=value=>short?`regions.gap(${value},${numeric(c.width)})`:`regions.gap(seed=${c.seed},variation=${numeric(c.variation)},width=${numeric(c.width)},source=${value})`;
    let operation='regions(src)';for(let i=0;i<c.repeat;i++)operation=gap(operation);
    const b=c.bevel||{},bevel=short?'regions.bevel(rs,2,{-1,-1})':`regions.bevel(strength=${numeric(b.strength??.5)},shadow=${color(b.shadow||[0,0,0,255])},highlight=${color(b.highlight||[255,255,255,255])},light=${vector(b.light||[-1,-1])},width=${numeric(b.width??2)},source=rs)`;
    const resource=c.bevel?`shade=echo_layer(${bevel})`:'m=regions.mask(rs)';
    const capture=mode==='creation-in-clip'?`layer empty{}clip(mask.from(empty)){${resource}}`:resource;
    const paint=c.bevel?'surface.paint(shade,blend=surface.'+(mode.startsWith('legacy')?'legacy':'source_over')+')':'surface.paint(m,#fff,blend=surface.source_over)';
    const use=mode==='unpainted'?'':mode==='clip'?`clip(regions.mask(rs)){${paint}}`:mode==='alpha-mask'&&c.bevel?'surface.paint(mask.from(shade),#fff,blend=surface.source_over)':paint;
    const background=mode.includes('background')?`layer background(alpha=surface.source_over){rect(0,0,${input.size.join(',')},#20406080)}surface.paint(background,blend=surface.source_over)`:'';
    return `asset t{size ${input.size.join('x')} output list point trace echo(rs:regions2d)->regions2d{return rs}echo_layer(l:layer)->layer{return l}
        draw{trace={-1,unit()}${input.setup}rs=echo(${operation})copy=choose(7,rs,rs)count=regions.count(copy)trace={count,-3}
        for i in 0..count{trace={regions.id(rs,i),regions.area(rs,i)}trace=regions.center(rs,i)
        ns=regions.neighbors(rs,i)trace={list.len(ns),-4}for j in 0..list.len(ns){trace={i,ns[j]}}${points}}
        ${capture}${background}${use}trace={-2,unit()}}}`;
}
function expectedTrace(record,control){const b=reference.bits,result=record.result,trace=[control[0],[b(result.regions.length),b(-3)]];
    for(let i=0;i<result.regions.length;i++){const r=result.regions[i];trace.push([b(r.source),r.area],r.center,[b(r.neighbors.length),b(-4)]);
        for(const n of r.neighbors)trace.push([b(i),b(n)]);const ring=result.rings[r.outer];if(!r.holes.length&&ring.vertices.length<=256){trace.push([b(ring.vertices.length),b(-5)]);for(const at of ring.vertices)trace.push(result.points[at]);}}
    trace.push(control[1]);return trace.map(p=>Array.isArray(p)?{x:p[0],y:p[1]}:p);}
function compose(dst,src,legacy){
    const a=src[3];if(legacy){if(a===255)return src;return[...src.slice(0,3).map((v,i)=>Math.floor((v*a+dst[i]*(255-a))/255)),dst[3]];}
    const dw=dst[3]*(255-a),den=a*255+dw;if(!den)return[0,0,0,0];
    return[...src.slice(0,3).map((v,i)=>Math.floor((v*a*255+dst[i]*dw+Math.floor(den/2))/den)),Math.floor((den+127)/255)];
}
function expectedPixels(record,width,height,mode){const frame=record.layer||record.mask,result=[];
    for(let y=0;y<height;y++)for(let x=0;x<width;x++){
        const fx=x-frame.origin[0],fy=y-frame.origin[1],inside=fx>=0&&fy>=0&&fx<frame.size[0]&&fy<frame.size[1],at=fy*frame.size[0]+fx;
        let src=inside?(record.layer?frame.pixels.slice(at*4,at*4+4):[255,255,255,frame.pixels[at]]):[0,0,0,0];
        if(mode==='alpha-mask')src=[255,255,255,src[3]];
        if(mode==='clip'){const mask=record.mask,mx=x-mask.origin[0],my=y-mask.origin[1],a=mx>=0&&my>=0&&mx<mask.size[0]&&my<mask.size[1]?mask.pixels[my*mask.size[0]+mx]:0;
            src=[...src.slice(0,3),Math.floor((src[3]*a+127)/255)];}
        result.push(...(mode==='unpainted'?[0,0,0,0]:compose(mode.includes('background')?[32,64,96,128]:[0,0,0,0],src,mode.startsWith('legacy'))));
    }return result;
}
function run(iprop,tool,directory,probes={}){
    const owned=!directory;if(owned)directory=fs.mkdtempSync(path.join(os.tmpdir(),'iprop-gap-bevel-'));
    const base=path.join(directory,'gap-bevel'),corpus=JSON.parse(fs.readFileSync(fixture));
    const nativeProbe=probes.native||path.resolve(path.dirname(tool('ipc')),'../tests/gap_bevel_probe'+(process.platform==='win32'?'.exe':''));assert(fs.existsSync(nativeProbe),'build the native gap_bevel_probe target: complete records come only from it');const hasNative=true;
    const native=(name,args)=>execFileSync(tool(name),args,{encoding:'utf8',stdio:'pipe'});
    let generations=0,topologies=0,traps=0,rejections=0,compilerErrors=0;const resourceBoundaries=[];
    function compile(text,flags=[]){fs.writeFileSync(base+'.ips',text);native('ipc',[base+'.ips',...flags,'-o',base+'.iprop']);return fs.readFileSync(base+'.iprop');}
    function assemble(text){fs.writeFileSync(base+'.ipa',text);native('ipa',[base+'.ipa','-o',base+'.iprop']);return fs.readFileSync(base+'.iprop');}
    function generate(bytes,options={},record,mode='layer',scene){
        options={seed:26,...options};fs.writeFileSync(base+'.iprop',bytes);
        const result=JSON.parse(native('ipgen',[base+'.iprop','--float-bits','--seed',String(options.seed),...(options.sizeX?['--size',options.sizeX+'x'+options.sizeY]:[]),'-o',base+'.png']));
        const image=decodePng(fs.readFileSync(base+'.png'));
        if(scene){assert.equal(crypto.createHash('sha256').update(image.data).digest('hex'),scene.rgba_sha256,'frozen authored wall/tile scene');
            assert.equal(result.fuel.spent,scene.fuel);assert.deepEqual([image.width,image.height],scene.size);}
        if(record)assert.deepEqual([...image.data],expectedPixels(record,image.width,image.height,mode),'independent layer/mask paint and existing composition');
        if(iprop){const wasm=iprop.generate(new Uint8Array(bytes),options);assert.deepEqual([...wasm.data],[...image.data],'gap/bevel whole native/WASM canvas');
            assert.deepEqual(outputBits(wasm.outputs),result.outputs,'gap/bevel metadata and RNG');}
        generations++;return result.outputs;
    }
    function reject(bytes,message,validation=false){fs.writeFileSync(base+'.iprop',bytes);fs.rmSync(base+'-failed.png',{force:true});
        const result=spawnSync(tool(validation?'ipval':'ipgen'),[base+'.iprop',...(validation?[]:['--seed','26','-o',base+'-failed.png'])],{encoding:'utf8'});
        assert.equal(result.signal,null);assert.equal(result.status,1,result.stderr);assert.match(result.stderr,message);assert(!fs.existsSync(base+'-failed.png'));
        if(iprop)assert.throws(()=>iprop.generate(new Uint8Array(bytes),{seed:26}),message);if(validation)rejections++;else traps++;
    }
    try{
        const control=generate(compile('asset t{size 1x1 output list point trace draw{trace={-1,unit()}trace={-2,unit()}}}'))[0].values;
        for(const scene of corpus.scenes)generate(compile(fs.readFileSync(path.resolve(__dirname,'../../..',scene.source),'utf8')),{seed:scene.seed},null,'layer',scene);
        let publicVectors=0;
        for(const c of corpus.cases){
            const record=JSON.parse(execFileSync(nativeProbe,reference.argumentsOf(c)));
            reference.check(c,record);assert.equal(digest(record),c.record_sha256,c.name+' complete regression record');topologies++;
            if(probes.wasm){const wasm=JSON.parse(execFileSync(process.execPath,[probes.wasm,...reference.argumentsOf(c)]));assert.deepEqual(wasm,record,c.name+' complete WASM record');}
            if(c.profileCheck){const shared=record.source.edges.findIndex(e=>{const a=record.source.points[e[0]].map(reference.value),b=record.source.points[e[1]].map(reference.value);return a[0]===8&&b[0]===8;});
                const profile=reference.profile(record.source,shared,c.width,c.variation,c.seed);
                for(let j=1;j<8;j++)for(const sign of[-1,1])assert(record.result.points.some(p=>p[0]===reference.bits(8+sign*profile.radii[j])&&p[1]===reference.bits(j*2)),'independent BigInt profile shared by opposite sides');}
            const input=publicInput(c);if(!input)continue;publicVectors++;
            const scale=c.scale||[1,1],options=scale.some(v=>v!==1)?{sizeX:input.size[0]*scale[0],sizeY:input.size[1]*scale[1]}:{};
            for(const optimize of[false,true])for(const debug of[false,true])for(const compressed of[false,true]){
                const flags=[...(optimize?[]:['--no-opt']),debug?'--debug':'--strip',...(compressed?[]:['--no-compress'])];
                const bytes=compile(source(c,record),flags),out=generate(bytes,options,record);assert.deepEqual(out[0].values,expectedTrace(record,control));
                if(!compressed)assert.deepEqual(generate(assemble(native('ipdis',[base+'.iprop'])),options,record),out,'IPA/IPDIS');
            }
            native('ipc',[base+'.ips','--format',base+'-formatted.ips','--format-only']);const text=fs.readFileSync(base+'-formatted.ips','utf8');
            assert.deepEqual(generate(compile(text),options,record)[0].values,expectedTrace(record,control),'formatter');
            for(const mode of['legacy','background','legacy-background','clip','creation-in-clip','unpainted','alpha-mask'])
                assert.deepEqual(generate(compile(source(c,record,mode)),options,record,mode)[0].values,expectedTrace(record,control));
            if(c.variation===0&&JSON.stringify(c.bevel)==='{}')assert.deepEqual(generate(compile(source(c,record,'layer',true)),options,record)[0].values,expectedTrace(record,control),'short defaults');
        }
        const setup='rs=regions(voronoi({0,0},{8,6},1,7)) ',bad='float(v)/float(v)',infinity='1.0/float(v)';
        for(const optimize of[false,true]){const flags=optimize?[]:['--no-opt'];
            for(const width of['-1',bad,infinity])for(const full of[false,true])reject(compile(`asset t{size 8x6 input i32 v=0 draw{${setup}regions.gap(rs,${width}${full?',0,7':''})}}`,flags),/gap width/);
            for(const variation of['-1','1.01',bad,infinity])reject(compile(`asset t{size 8x6 input i32 v=0 draw{${setup}regions.gap(rs,0,${variation},7)}}`,flags),/gap variation/);
            for(const width of['0','-1',bad,infinity])reject(compile(`asset t{size 8x6 input i32 v=0 draw{${setup}regions.bevel(rs,${width},{1,1})}}`,flags),/bevel width/);
            for(const strength of['-1','1.01',bad,infinity])reject(compile(`asset t{size 8x6 input i32 v=0 draw{${setup}regions.bevel(rs,1,{1,1},#fff,#000,${strength})}}`,flags),/bevel strength/);
            for(const light of['{0,0}',`{${bad},1}`,`{1,${infinity}}`])reject(compile(`asset t{size 8x6 input i32 v=0 draw{${setup}regions.bevel(rs,1,${light})}}`,flags),/bevel light/);
            for(const use of['regions.gap(rs,0)','regions.gap(rs,0,0,7)','regions.bevel(rs,1,{1,1})','regions.bevel(rs,1,{1,1},#fff,#000,0)'])
                reject(compile(`asset t{size 1x1 draw{if(seed==0){rs=regions(voronoi({0,0},{1,1},1,0))}${use}}}`,flags),/unassigned/);
        }
        reject(compile('asset t{size 1x1 draw{rs=regions(voronoi({2147483000,0},{1024,8},1,0))regions.bevel(rs,1,{1,1})}}'),/mapping|representable/);
        for(const [id,mnemonic,args,result] of[[252,'regions.gap',['regions2d','f32'],'regions2d'],[253,'regions.gap.config',['regions2d','f32','f32','i32'],'regions2d'],[254,'regions.bevel',['regions2d','f32','vec2'],'layer'],[255,'regions.bevel.config',['regions2d','f32','vec2','color','color','f32'],'layer']]){
            const bytes=assemble(`meta: size=1x1\ntypes: regions2d layer\nfn draw ( ) -> ( ):\nreturn\n${mnemonic}\ndrop\nendfn\n`),at=instructions(bytes).find(at=>bytes[at]===id);assert(at!==undefined);
            for(const delta of[1,2,3]){const bad=Buffer.from(bytes);bad[at+delta]=1;reject(bad,/aux|operand|zero/,true);}
            for(let index=0;index<args.length;index++)for(const wrong of['i32','f32','vec2','color','regions2d','layer'].filter(t=>t!==args[index])){
                const valid=assemble(`meta: size=1x1\ntypes: regions2d layer\nfn draw ( ) -> ( ):\nreturn\nendfn\nfn helper ( ${[...args,wrong].join(' ')} ) -> ( ${result} ):\n${args.map((_,i)=>'local.get '+i).join('\n')}\n${mnemonic}\nreturn\nendfn\n`);
                const bad=Buffer.from(valid),get=instructions(valid).filter(at=>valid[at]===0x13)[index];assert(get!==undefined);
                bad.writeUInt16LE(args.length,get+2);reject(bad,/type mismatch/,true);
            }
        }
        for(const body of['regions.gap(1,1)',setup+'regions.gap(rs,1,0.5)',setup+'regions.gap(rs,width=1,unknown=0,seed=7)',setup+'regions.bevel(rs,1)',setup+'regions.bevel(rs,1,{1,1},#fff)',setup+'regions.bevel(rs,1,1)',setup+'regions.bevel(rs,1,{1,1},1,#000,1)']){
            fs.writeFileSync(base+'.ips',`asset t{size 8x6 draw{${body}}}`);const r=spawnSync(tool('ipc'),[base+'.ips','--check'],{encoding:'utf8'});assert.equal(r.status,1,r.stderr);compilerErrors++;
        }
        for(const name of['rectangle-variable','hole-variable','diagram-scaled']){
            const c=corpus.cases.find(c=>c.name===name),normal=JSON.parse(execFileSync(nativeProbe,reference.argumentsOf(c))),probe=(budget,occupied,wasm=false)=>spawnSync(wasm?process.execPath:nativeProbe,[...(wasm?[probes.wasm]:[]),...reference.argumentsOf(c,budget,occupied)],{encoding:'utf8'});
            let lo=0,hi=64*1024*1024-normal.sourceBytes;
            while(lo+1<hi){const mid=Math.floor((lo+hi)/2),r=probe(1e9,mid);if(r.status===0)lo=mid;else{assert.equal(r.status,4);assert.match(r.stderr,/64 MiB/);hi=mid;}}
            for(const wasm of probes.wasm?[false,true]:[false]){
                const ok=probe(normal.fuel,lo,wasm);assert.equal(ok.status,0,ok.stderr);assert.deepEqual(JSON.parse(ok.stdout),normal);
                const fuel=probe(normal.fuel-1,0,wasm),memory=probe(1e9,lo+1,wasm);assert.equal(fuel.status,4);assert.match(fuel.stderr,/fuel exhausted/);assert.equal(memory.status,4);assert.match(memory.stderr,/64 MiB/);
            }resourceBoundaries.push({name,fuel:normal.fuel,minimumFreeBytes:64*1024*1024-lo});
        }
        return{passed:true,vectors:corpus.cases.length,scenes:corpus.scenes.length,publicVectors,topologies,generations,traps,rejections,compilerErrors,resourceBoundaries,nativeRepresentation:hasNative,wasmRepresentation:!!probes.wasm,wasm:!!iprop};
    }finally{if(owned)fs.rmSync(directory,{recursive:true,force:true});}
}
module.exports={run,source,publicInput,digest};
if(require.main===module){const tools=path.resolve(process.argv[2]);console.log(JSON.stringify(run(null,n=>path.join(tools,n),undefined,{native:process.argv[3],wasm:process.argv[4]})));}
