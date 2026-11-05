"use strict";

const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path");
const {execFileSync,spawnSync}=require("node:child_process");
const {decodePng}=require("./png.js");
const {replaceFloatConstants}=require("./vector-conformance.js");
const reference=require("./noise-v09-reference.js");
const {instructions,outputBits,sections}=require("./harness.js");
const repo=path.resolve(__dirname,"../../..");
const opcodes={cell2:0xdf,cell_id:0xe0,warp_vec:0xe1,fbm:0xe2};
const mnemonics={cell2:"noise.cell2",cell_id:"noise.cell_id",warp_vec:"noise.warp_vec",fbm:"noise.fbm_config"};
const type=v=>v.op==="cell_id"?"i32":v.op==="warp_vec"?"vec2":"f32";
function literal(b) {
    const x=reference.value(b);
    if(Object.is(x,-0))return "-0.0";
    // IPS rejects subnormal literals; valid arithmetic can produce them.
    if((b&0x7fffffff)>0&&(b&0x7fffffff)<0x00800000)
        return `(${literal(0x00800000)}*${literal(reference.bits(x/reference.value(0x00800000)))})`;
    if(Number.isInteger(x))return x+".0";
    return x.toPrecision(9);
}
function expression(v,vector,runtime=false) {
    const args=vector?[`{${literal(v.x)},${literal(v.y)}}`]:[literal(v.x),literal(v.y)];
    if(v.op==="fbm")args.push(String(v.octaves),literal(v.lacunarity),literal(v.gain));
    if(v.op==="warp_vec")args.push(literal(v.amount));
    args.push(runtime?`(${v.seed}+runtime_seed)`:String(v.seed));
    return `noise.${v.op}(${args.join(",")})`;
}
function observation(v,name) {
    return v.op==="cell_id"?`{${name}&65535,${name}>>16}`:v.op==="warp_vec"?name:`{${name},0}`;
}
function expected(v) {
    if(v.op==="cell_id")return {x:reference.bits(v.expected[0]&65535),y:reference.bits(v.expected[0]>>>16)};
    return {x:v.expected[0],y:v.expected[1]??0};
}
function argumentsOf(v) {
    const args=[["f32",v.x],["f32",v.y]];
    if(v.op==="fbm")args.push(["i32",v.octaves],["f32",v.lacunarity],["f32",v.gain]);
    if(v.op==="warp_vec")args.push(["f32",v.amount]);
    args.push(["i32",v.seed]);return args;
}
function run(iprop,tool,directory) {
    const vectors=JSON.parse(fs.readFileSync(path.join(repo,"tests/fixtures/noise-v0.9.json"),"utf8")).vectors;
    let generations=0,pins=0,traps=0,compilerErrors=0,rejections=0;
    const base=path.join(directory,"noise-v09");
    for(const v of vectors)assert.deepEqual(reference.evaluate(v),v.expected,"independent noise pin "+JSON.stringify(v));
    const old=JSON.parse(fs.readFileSync(path.join(repo,"tests/fixtures/compat-v0.8.json"),"utf8"));
    for(const tie of old.worley_ties) {
        const q=reference.worley(tie.x,tie.y,tie.seed);
        assert.deepEqual(q.winner,tie.winner);assert.equal(reference.bits(q.first),tie.distance_bits);
        assert.equal(reference.bits(q.second),tie.distance_bits);
        assert.equal(q.id,reference.hash(tie.seed));
    }
    function compile(source,flags=[]) {
        fs.writeFileSync(base+".ips",source);
        execFileSync(tool("ipc"),[base+".ips",...flags,"-o",base+".iprop"],{stdio:"pipe"});
        return fs.readFileSync(base+".iprop");
    }
    function assemble(text) {
        fs.writeFileSync(base+".ipa",text);
        execFileSync(tool("ipa"),[base+".ipa","-o",base+".iprop"],{stdio:"pipe"});
        return fs.readFileSync(base+".iprop");
    }
    function generate(bytes,seed=7) {
        fs.writeFileSync(base+".iprop",bytes);
        const native=JSON.parse(execFileSync(tool("ipgen"),[base+".iprop","--float-bits","--seed",String(seed),"-o",base+".png"],{encoding:"utf8"}));
        const image=decodePng(fs.readFileSync(base+".png"));
        if(iprop) {
            const wasm=iprop.generate(new Uint8Array(bytes),{seed});
            assert.deepEqual(outputBits(wasm.outputs),native.outputs,"noise native/WASM output bits");
            assert.deepEqual(Buffer.from(wasm.data),Buffer.from(image.data),"noise native/WASM pixels");
        }
        ++generations;return native.outputs;
    }
    function reject(bytes,message,validation=false) {
        fs.writeFileSync(base+".iprop",bytes);
        const png=base+"-failed.png";fs.rmSync(png,{force:true});
        const result=spawnSync(tool(validation?"ipval":"ipgen"),[base+".iprop",...(validation?[]:["-o",png])],{encoding:"utf8"});
        assert.equal(result.signal,null);assert.equal(result.status,1,result.stderr);
        assert.match(result.stderr,message);assert.equal(fs.existsSync(png),false,"failed noise generation published PNG");
        if(iprop)assert.throws(()=>iprop.generate(new Uint8Array(bytes),{}),message);
        if(validation)++rejections;else ++traps;
    }
    const controlBytes=compile("asset t{size 1x1 output point state draw{state={1,unit()}}}");
    const controls=new Map([0,7,0xffffffff].map(seed=>[seed,generate(controlBytes,seed)[0].values]));
    for(const vector of [false,true])for(const constant of [false,true]) {
        const declarations=constant?vectors.map((v,i)=>`const ${type(v)} n${i}=${expression(v,vector)}`).join("\n"):"input i32 runtime_seed=0";
        const body=vectors.map((v,i)=>`${constant?"":`n${i}=${expression(v,vector,true)} `}values=${observation(v,`n${i}`)}`).join("\n");
        const source=`asset t{size 1x1 output list point values output point state ${declarations} draw{before=seed ${body} state={seed==before,unit()}}}`;
        for(const optimize of [false,true])for(const debug of [false,true])for(const compress of [false,true]) {
            const bytes=compile(source,[...(optimize?[]:["--no-opt"]),debug?"--debug":"--strip",...(compress?[]:["--no-compress"])]);
            const text=execFileSync(tool("ipdis"),[base+".iprop"],{encoding:"utf8"});
            for(const op of Object.values(mnemonics)) {
                const present=text.split("\n").some(line=>line.trim().startsWith(op+" ")||line.trim()===op);
                assert.equal(present,!constant,"runtime vs folded opcode "+op);
            }
            for(const seed of [0,7,0xffffffff]) {
                const outputs=generate(bytes,seed);
                assert.deepEqual(outputs[0].values,vectors.map(expected),"frozen noise result encodings");
                assert.deepEqual(outputs[1].values,controls.get(seed),"noise must not consume current RNG");
                pins+=vectors.length;
            }
            const roundtrip=assemble(text);
            assert.deepEqual(generate(roundtrip)[0].values,vectors.map(expected),"noise IPA/IPDIS roundtrip");
        }
    }
    // Exact encodings exercise signed zero, subnormals and raw NaN payloads
    // without relying on IPS literal spelling or JavaScript NaN conversion.
    function raw(v,fuel=1000000) {
        const floats=[],args=argumentsOf(v).map(([kind,value])=>kind==="i32"?`const.i32 ${value}\n`:
            `const.f32 ${100+floats.push(value)-1}\n`).join("");
        const bytes=assemble(`meta: size=1x1 fuel=${fuel}\nfn draw ( ) -> ( ):\n${args}${mnemonics[v.op]}\ndrop\nendfn\n`);
        replaceFloatConstants(bytes,floats);return bytes;
    }
    const B=reference.bits, coordinate=/noise coordinate out of range/;
    const good={cell2:{op:"cell2",x:B(0.5),y:B(0.5),seed:7},cell_id:{op:"cell_id",x:B(0.5),y:B(0.5),seed:7},
        warp_vec:{op:"warp_vec",x:B(0.5),y:B(0.5),amount:B(0.2),seed:7},
        fbm:{op:"fbm",x:B(0.5),y:B(0.5),octaves:8,lacunarity:B(2),gain:B(0.5),seed:7}};
    for(const op of Object.keys(good)) {
        const badCoordinates=[0x7f800000,0xff800000,0x7fc00001,0xffc12345,0x7fa00001,
            B(op.startsWith("cell")?16777215:2147483648),B(op.startsWith("cell")?-16777215:-2147483904)];
        for(const bad of badCoordinates)for(const component of ["x","y"])reject(raw({...good[op],[component]:bad}),coordinate);
    }
    for(const bad of [0x7f800000,0xff800000,0x7fc01234])
        reject(raw({...good.warp_vec,x:0x7fc01234,amount:bad}),/warp amount out of range/);
    for(const bad of [0,0x80000000,B(-1),B(8)+1,0x7f800000,0xff800000,0x7fc01234])
        reject(raw({...good.fbm,lacunarity:bad,gain:0}),/fbm lacunarity out of range/);
    for(const bad of [0x80000001,B(1)+1,0x7f800000,0xff800000,0x7fc01234])
        reject(raw({...good.fbm,gain:bad}),/fbm gain out of range/);
    for(const v of [{...good.fbm,x:B(1073741824),octaves:2},
                    {...good.fbm,y:B(1073741824),octaves:2},
                    {...good.warp_vec,x:0,y:B(268435440)},
                    {...good.warp_vec,amount:0x7f7fffff}])reject(raw(v),coordinate);
    for(const bad of [0x7fc01234,0x7f800000,0xff800000,B(2147483648)])
        reject(raw({...good.warp_vec,x:bad,amount:0}),coordinate);
    // Exact fuel threshold includes argument instructions and dropping result.
    const fuelCases=[[good.cell2,25],[good.cell_id,25],[good.warp_vec,8],
        [{...good.warp_vec,amount:0},1],[good.fbm,8],[{...good.fbm,gain:0},8],
        [{...good.fbm,octaves:-3},1],[{...good.fbm,octaves:9},8]];
    for(const [v,cost] of fuelCases) {
        const exact=argumentsOf(v).length+cost+1;
        generate(raw(v,exact));reject(raw(v,exact-1),/fuel exhausted/);
    }
    // Const diagnostics must agree with runtime, with optimization on and off.
    const invalidConstants=[
        ["f32","noise.cell2(16777215.0,0,7)",coordinate],
        ["i32","noise.cell_id({0,1.0/0.0},7)",coordinate],
        ["vec2","noise.warp_vec({0,0},1.0/0.0,7)",/warp amount out of range/],
        ["vec2","noise.warp_vec({0.0/0.0,0},0,7)",coordinate],
        ["f32","noise.fbm(0,0,8,0,0,7)",/fbm lacunarity out of range/],
        ["f32","noise.fbm({0,0},8,2,1.01,7)",/fbm gain out of range/],
        ["f32","noise.fbm({1073741824,0},2,2,0.5,7)",coordinate]];
    for(const [result,expression,message] of invalidConstants)for(const optimize of [false,true]) {
        fs.writeFileSync(base+".ips",`asset t{size 1x1 const ${result} bad=${expression} draw{}}`);
        fs.rmSync(base+".iprop",{force:true});
        const actual=spawnSync(tool("ipc"),[base+".ips","-o",base+".iprop",...(optimize?[]:["--no-opt"])],{encoding:"utf8"});
        assert.equal(actual.status,1);assert.match(actual.stderr,message);assert.equal(fs.existsSync(base+".iprop"),false);++compilerErrors;
    }
    // Every argument position is checked by native and WASM bytecode validators.
    for(const v of Object.values(good)) {
        const bytes=raw(v),code=sections(bytes).get(3),ops=instructions(bytes).map(at=>({at,opcode:bytes[at]}));
        assert(code);const args=ops.filter(i=>i.opcode===0x10||i.opcode===0x11);
        assert.equal(args.length,argumentsOf(v).length);
        for(const arg of args) {
            const bad=Buffer.from(bytes);bad[arg.at]=arg.opcode===0x10?0x11:0x10;bad.writeUInt16LE(0,arg.at+2);
            reject(bad,/type mismatch/,true);
        }
        const instruction=ops.find(i=>i.opcode===opcodes[v.op]);assert(instruction);
        const aux=Buffer.from(bytes);aux[instruction.at+1]=1;reject(aux,/aux byte must be zero/,true);
        const unknown=Buffer.from(bytes);unknown[instruction.at]=0x0b;reject(unknown,/reserved opcode/,true);
        const reserved=Buffer.from(bytes);reserved.writeUInt32LE(9,4);reject(reserved,/reserved header field must be zero/,true);
        // Current runtime accepts new instructions on a polymorphic dead
        // stack. Aux and reserved-header checks still apply to dead code.
        const unreachable=assemble(`meta: size=1x1\nfn draw ( ) -> ( ):\nreturn\n${mnemonics[v.op]}\nendfn\n`);
        generate(unreachable);
        const deadAt=instructions(unreachable).find(at=>unreachable[at]===opcodes[v.op]);
        assert.notEqual(deadAt,undefined);
        const deadAux=Buffer.from(unreachable);deadAux[deadAt+1]=1;reject(deadAux,/aux byte must be zero/,true);
        const deadUnknown=Buffer.from(unreachable);deadUnknown[deadAt]=0x0b;reject(deadUnknown,/reserved opcode/,true);
    }
    return {generations,pins,vectors:vectors.length,traps,compilerErrors,rejections,wasm:Boolean(iprop)};
}
module.exports={run};
if(require.main===module) {
    const os=require("node:os"),directory=fs.mkdtempSync(path.join(os.tmpdir(),"iprop-noise-v09-"));
    const tools=path.resolve(process.argv[2]||"build/tools"),tool=name=>path.join(tools,name+(process.platform==="win32"?".exe":""));
    try {console.log(JSON.stringify(run(null,tool,directory)));}
    finally {fs.rmSync(directory,{recursive:true,force:true});}
}
