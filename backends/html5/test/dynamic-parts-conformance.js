"use strict";
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),os=require('node:os'),crypto=require('node:crypto');
const {execFileSync,spawnSync}=require('node:child_process'),{decodePng}=require('./png.js'),{instructions}=require('./sampler-conformance.js');
const ref=require('../../../tests/reference/dynamic-parts-reference.js');
const repo=path.resolve(__dirname,'../../..'),fixture=path.join(repo,'tests/fixtures/dynamic-parts-v0.9.json');
const params=[[0,0,0],[90,0,0],[180,0,0],[270,0,0],[0,1,0],[0,0,1],[90,0,1],[90,1,0]];
const opcodes=[0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f,0x67,0x68];
const arities=[2,1,1,1,1,2,1,1,1,1,1,5,2,2,1,1];
const sha=data=>crypto.createHash('sha256').update(data).digest('hex');
function probeArgs(input,budget=1000000000,occupied=0){const c=ref.defaults(input);return[c.kind,c.rot,c.mx,c.my,...c.at.map(ref.bits),c.appearance,...c.scale.map(ref.bits),c.sourceD4,...c.origin,c.clip,c.mode,c.background,c.maskMode,budget,occupied,c.place].map(String);}
function compact(r){const {canvas,...rest}=r;return {...rest,canvas_sha256:sha(Buffer.from(canvas))};}
const color=c=>'#'+c.map(n=>n.toString(16).padStart(2,'0')).join('');
const point=p=>'{'+p.map(n=>Math.fround(n).toString()).join(',')+'}';
function writeModules(directory,c){
    const pixels=Array.from({length:20},(_,i)=>`pixel(${i%5},${Math.floor(i/5)},${color(ref.sourcePixel(i%5,Math.floor(i/5)))})`).join(' ');
    fs.writeFileSync(path.join(directory,'source.ips'),`asset src{size 5x4 output ${c.kind===2?'mask':'layer'} image draw{
        layer captured(alpha=surface.source_over){${pixels}}
        ${c.kind===2?'layer empty{}image=mask.union(mask.from(captured),mask.from(empty))':'image=captured'}}}`);
    const texture=Array.from({length:42},(_,i)=>{const x=i%7,y=Math.floor(i/7);return`pixel(${x},${y},${color([(11+x*23)%256,(19+y*29)%256,(31+x*17+y*13)%256,(x*37+y*61)%256])})`;}).join(' ');
    fs.writeFileSync(path.join(directory,'pattern.ips'),`asset pattern{size 7x6 output layer image draw{layer captured(alpha=surface.source_over){${texture}}image=captured}}`);
}
function publicSource(input){
    const c=ref.defaults(input),[rot,mx,my]=params[c.sourceD4];
    const source=c.kind===4?'mask.from(source_image)':'source_image';
    let appearance=c.appearance===0?'p':c.appearance===1?'part.fill(p,#1159cb80)':c.appearance===2?'part.texture(p,texture)':
        c.appearance===3?'part.fill(p,#070b0d00)':c.appearance===4?'part.fill(part.texture(p,texture),#29619940)':
        c.appearance===5?'part.texture(part.fill(p,#1159cb80),texture)':'part.fill(p,#0d1d2fff)';
    if(c.place===2)appearance=`part.place(${appearance},{7.25,9.5},0,0,0)`;
    const placed=c.place?`part.place(mirror_y=${c.my},mirror_x=${c.mx},rot=${c.rot},at=${point(c.at)},p=${appearance})`:appearance;
    const clip=c.clip===3?Array.from({length:1024},(_,i)=>`pixel(${i%32},${Math.floor(i/32)},${color([255,255,255,(i%32*19+Math.floor(i/32)*31)%256])})`).join(' '):
        c.clip===1?'rect(0,0,32,32,#ffffff80)':'';
    const background=c.background?'layer bg(alpha=surface.source_over){rect(0,0,32,32,#c904ac80)}surface.paint(bg,blend=surface.source_over)':'';
    const use=c.maskMode===1?'layer empty{}clip(mask.from(empty)){shape=part.mask(q)}surface.paint(shape,#fff,blend=surface.source_over)':
        (c.clip?'clip(mask.from(clipping)){part.draw(q)}':'part.draw(q)');
    const draw=background+use;
    return `import source from "source.ips" import pattern from "pattern.ips"
        asset t{size 32x32 output list point trace echo(p:part2d)->part2d{return p}
        draw{source.draw(7,at=${point(c.origin)},rot=${rot},mirror_x=${mx},mirror_y=${my}) source_image=source.outputs.image
            pattern.draw(11,at={-4,-2},rot=90,mirror_y=1)texture=pattern.outputs.image
            trace={-1,unit()} src=${source} p=echo(part.create(src)) q=echo(${placed}) q=choose(7,q,q)
            trace={part.id(q),part.source_id(q)}trace=part.center(q)trace=part.bounds_origin(q)trace=part.bounds_size(q)
            trace=part.center(p)trace=part.bounds_origin(p)trace=part.bounds_size(p)
            ${c.clip?'layer clipping(alpha=surface.source_over){'+clip+'}':''}
            ${c.mode&&c.maskMode!==1?'layer result(alpha=surface.source_over){'+draw+'}surface.paint(result,blend=surface.source_over)':draw}
            trace={part.id(part.create(src)),0}trace={-2,unit()}}}`;
}
function shapePixels(mask){const pixels=Array(32*32*4).fill(0);for(let y=0;y<mask.size[1];y++)for(let x=0;x<mask.size[0];x++){
    const a=mask.pixels[y*mask.size[0]+x];if(!a)continue;const p=ref.mappedPixel(mask.d4,[x,y],mask.size).map((v,i)=>v+mask.origin[i]);
    if(p[0]>=0&&p[1]>=0&&p[0]<32&&p[1]<32)pixels.splice((p[1]*32+p[0])*4,4,255,255,255,a);
}return pixels;}
function run(iprop,tool,directory,probes={}){
    const owned=!directory;if(owned)directory=fs.mkdtempSync(path.join(os.tmpdir(),'iprop-dynamic-parts-'));
    const base=path.join(directory,'dynamic-parts'),corpus=JSON.parse(fs.readFileSync(fixture));
    const nativeProbe=probes.native||path.resolve(path.dirname(tool('ipc')),'../tests/dynamic_parts_probe'+(process.platform==='win32'?'.exe':''));
    const hasNative=fs.existsSync(nativeProbe),hasWasm=!!probes.wasm;
    let generations=0,records=0,traps=0,rejections=0,compilerErrors=0,publicVectors=0;
    const invoke=(name,args)=>execFileSync(tool(name),args,{encoding:'utf8',stdio:'pipe'});
    function compile(text,flags=[]){fs.writeFileSync(base+'.ips',text);invoke('ipc',[base+'.ips',...flags,'-o',base+'.iprop']);return fs.readFileSync(base+'.iprop');}
    function assemble(text){fs.writeFileSync(base+'.ipa',text);invoke('ipa',[base+'.ipa','-o',base+'.iprop']);return fs.readFileSync(base+'.iprop');}
    function generate(bytes,expected,trace,scene){fs.writeFileSync(base+'.iprop',bytes);const seed=scene?scene.seed:26;
        const result=JSON.parse(invoke('ipgen',[base+'.iprop','--seed',String(seed),'--float-bits','-o',base+'.png']));
        const image=decodePng(fs.readFileSync(base+'.png'));
        if(scene){assert.equal(sha(image.data),scene.rgba_sha256);assert.equal(result.fuel.spent,scene.fuel);assert.deepEqual([image.width,image.height],scene.size);}
        if(expected)assert.deepEqual([...image.data],expected,'independent dynamic part RGBA');
        if(trace)assert.deepEqual(result.outputs[0].values,trace,'independent effective part metadata and unchanged RNG');
        if(iprop){const wasm=iprop.generate(bytes,{seed});assert.deepEqual([...wasm.data],[...image.data],'whole native/WASM dynamic part render');
            assert.deepEqual(wasm.outputs.map(s=>({name:s.name,kind:['point','rect','circle'][s.kind],list:s.isList,values:s.valueBits})),result.outputs);
            assert.equal(wasm.fuel.used,result.fuel.spent);assert.equal(wasm.fuel.remaining,result.fuel.budget-result.fuel.spent);assert.equal(wasm.fuel.budget,result.fuel.budget);}
        generations++;return result;
    }
    function reject(bytes,message,validation=false){fs.writeFileSync(base+'.iprop',bytes);fs.rmSync(base+'-failed.png',{force:true});
        const r=spawnSync(tool(validation?'ipval':'ipgen'),[base+'.iprop',...(validation?[]:['--seed','26','-o',base+'-failed.png'])],{encoding:'utf8'});
        assert.equal(r.signal,null);assert.equal(r.status,1,r.stderr);assert.match(r.stderr,message);assert(!fs.existsSync(base+'-failed.png'));
        if(iprop)assert.throws(()=>iprop.generate(bytes,{seed:26}),message);if(validation)rejections++;else traps++;
    }
    try{
        for(const c of corpus.cases){const expected=ref.reference(c);
            for(const key of ['original','view','mask','creations','released'])assert.deepEqual(c.result[key],expected[key],c.name+' frozen oracle '+key);
            assert.equal(c.result.canvas_sha256,sha(Buffer.from(expected.canvas)),c.name+' oracle full RGBA digest');
            if(hasNative){const r=JSON.parse(execFileSync(nativeProbe,probeArgs(c),{encoding:'utf8'}));
                assert.deepEqual(compact(r),c.result,c.name+' full core/fuel/bytes pin');for(const key of Object.keys(expected))assert.deepEqual(r[key],expected[key],c.name+' native '+key);records++;}
            if(hasWasm){const r=JSON.parse(execFileSync('node',[probes.wasm,...probeArgs(c)],{encoding:'utf8'}));
                assert.deepEqual(compact(r),c.result,c.name+' WASM full core/fuel/bytes pin');for(const key of Object.keys(expected))assert.deepEqual(r[key],expected[key],c.name+' WASM '+key);records++;}
        }
        const control=generate(compile('asset t{size 1x1 output list point trace draw{trace={-1,unit()}trace={-2,unit()}}}')).outputs[0].values;
        for(const scene of corpus.scenes||[])generate(compile(fs.readFileSync(path.join(repo,scene.source),'utf8')),null,null,scene);
        const selected=corpus.cases.filter(c=>c.kind>=2&&((c.name.startsWith('matrix-')&&(c.sourceD4+ref.kind(c.rot,c.mx,c.my))%3===0)||c.name.startsWith('appearance-')||c.name.startsWith('original-')));
        for(const item of selected){const c=ref.defaults({...item,scale:[1,1],background:item.maskMode===1?0:item.background===2?1:item.background});
            writeModules(directory,c);const record=ref.reference(c),expected=c.maskMode===1?shapePixels(record.mask):record.canvas;
            const trace=[control[0],{x:ref.bits(0),y:ref.bits(-1)},...[record.view.center,record.view.origin,record.view.size,record.original.center,record.original.origin,record.original.size].map(p=>({x:p[0],y:p[1]})),{x:ref.bits(1),y:ref.bits(0)},control[1]];
            const text=publicSource(c);publicVectors++;
            for(const optimize of[false,true])for(const debug of[false,true])for(const compressed of[false,true]){
                const flags=[...(optimize?[]:['--no-opt']),debug?'--debug':'--strip',...(compressed?[]:['--no-compress'])];
                const bytes=compile(text,flags);generate(bytes,expected,trace);
                if(!compressed){const asm=invoke('ipdis',[base+'.iprop']);generate(assemble(asm),expected,trace);}
            }
            compile(text);invoke('ipc',[base+'.ips','--format-only','--format',base+'-formatted.ips']);
            generate(compile(fs.readFileSync(base+'-formatted.ips','utf8')),expected,trace);
        }
        const dead=`asset t{size 8x8 unused(rs:regions2d,m:mask,l:layer,p:part2d,ps:parts2d,i:i32,v:vec2,c:color){
            part.create(rs,i)part.create(m)part.create(l)parts.from_regions(rs)parts.count(ps)parts.get(ps,i)
            part.id(p)part.source_id(p)part.center(p)part.bounds_origin(p)part.bounds_size(p)
            part.place(p,v,i,i,i)part.fill(p,c)part.texture(p,l)part.draw(p)part.mask(p)}draw{}}`;
        const valid=compile(dead,['--no-opt','--no-compress']),positions=instructions(valid);
        for(let n=0;n<opcodes.length;n++){const op=opcodes[n],index=positions.findIndex(at=>valid[at]===op);assert(index>=0,'all dynamic opcode overloads emitted');const at=positions[index];
            for(const offset of[1,2]){const bad=Buffer.from(valid);bad[at+offset]=1;reject(bad,/aux byte must be zero|operand must be zero/,true);}
            for(let k=0;k<arities[n];k++){const access=positions[index-arities[n]+k];assert.equal(valid[access],0x13,'typed helper argument is local.get');
                const param=valid.readUInt16LE(access+2),bad=Buffer.from(valid);bad.writeUInt16LE(param===5?3:5,access+2);reject(bad,/type mismatch/,true);}
        }
        const setup='layer image(alpha=surface.source_over){pixel(0,0,#ffffff80)}p=part.create(image)';
        const invalid=[
            ['layer empty{}part.create(empty)',/empty dynamic part/],['layer empty{}part.create(mask.from(empty))',/empty dynamic part/],
            ['part.create(regions(voronoi({0,0},{2,2},1,0)),-1)',/index out of range/],
            ['parts.get(parts.from_regions(regions(voronoi({0,0},{2,2},1,0))),1)',/index out of range/],
            ['part.draw(part.create(regions(voronoi({0,0},{2,2},1,0)),0))',/dynamic part has no appearance/],
            ['part.draw(part.create(mask.from(image)))',/dynamic part has no appearance/],
            ...['45,0,0','-90,0,0','360,0,0','0,2,0','0,0,-1'].map(p=>[`part.place(p,{0,0},${p})`,/requires rot/]),
            ['part.place(p,{0.0/0.0,0},0,0,0)',/position must be finite/],['part.place(p,{1.0/0.0,0},0,0,0)',/position must be finite/],
            ['part.place(p,{2147483648.0,0},0,0,0)',/translation is not representable/],
            ['part.create(regions(voronoi({0,0},{2,2},1,0)),1)',/index out of range/]];
        for(const [operation,message] of invalid)for(const optimize of[false,true])reject(compile('asset t{size 2x2 draw{'+setup+operation+'}}',optimize?[]:['--no-opt']),message);
        for(const [operation,source] of [['part.center(p)','if(seed==0){p=part.create(regions(voronoi({0,0},{2,2},1,0)),0)}'],
            ['parts.count(ps)','if(seed==0){ps=parts.from_regions(regions(voronoi({0,0},{2,2},1,0)))}'],
            ['part.create(rs,0)','if(seed==0){rs=regions(voronoi({0,0},{2,2},1,0))}']])reject(compile('asset t{size 2x2 draw{'+source+operation+'}}'),/unassigned/);
        for(const body of ['part.create(1)','parts.from_regions(1)','parts.get(1,0)','part.place(1,{0,0},0,0,0)',
            'part.texture(p,mask.from(image))','part.draw(1)','part.mask(1)','part.center(1)','part.place(p,{0,0},0.0,0,0)']){
            fs.writeFileSync(base+'.ips','asset t{size 2x2 draw{'+setup+body+'}}');const r=spawnSync(tool('ipc'),[base+'.ips','-o',base+'-failed.iprop'],{encoding:'utf8'});
            assert.equal(r.status,1,r.stderr);assert.match(r.stderr,/must be|no matching overload/);compilerErrors++;
        }
        return {passed:true,vectors:corpus.cases.length,scenes:(corpus.scenes||[]).length,records,internalNative:hasNative,internalWasm:hasWasm,wasm:!!iprop,publicVectors,generations,traps,rejections,compilerErrors};
    }finally{if(owned)fs.rmSync(directory,{recursive:true,force:true});}
}
module.exports={run,probeArgs,compact,publicSource};
if(require.main===module){const directory=path.resolve(process.argv[2]||path.join(repo,'build/tools'));console.log(JSON.stringify(run(null,n=>path.join(directory,n),undefined,{native:process.argv[3],wasm:process.argv[4]})));}
