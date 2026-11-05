"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os");
const {execFileSync} = require("node:child_process");
const {Worker} = require("node:worker_threads");
const {instructions,sections}=require("./harness.js");
const repo = path.resolve(__dirname, "../../..");
const cases = JSON.parse(fs.readFileSync(path.join(repo,"backends/test/execution-limits-v0.9.json"))).cases;
function hash(bytes) {
    let result = 14695981039346656037n;
    for (const byte of bytes) result = BigInt.asUintN(64,(result ^ BigInt(byte))*1099511628211n);
    return result.toString(16);
}
function compile(tool, directory, name, source) {
    fs.mkdirSync(directory,{recursive:true});
    const file = path.join(directory,name.replace(/\W+/g,"-"));
    fs.writeFileSync(file+".ips",source);
    execFileSync(tool("ipc"),[file+".ips","--no-opt","--no-compress","-o",file+".iprop"],{stdio:"pipe"});
    const bytes = fs.readFileSync(file+".iprop");
    bytes.writeBigUInt64LE(1000000000n,sections(bytes).get(1)+16);
    fs.writeFileSync(file+".iprop",bytes);
    return {file:file+".iprop",bytes};
}
function workError(action, reason) {
    assert.throws(action,e => e.reason === reason && e.stage === 4,"structured execution limit reason "+reason);
}
function run(iprop, tool, directory, options={}) {
    const owned = !directory;
    directory = directory || fs.mkdtempSync(path.join(os.tmpdir(),"iprop-work-"));
    const probe = options.native || path.resolve(path.dirname(tool("ipc")),"../tests/execution_limits_probe"+(process.platform==="win32"?".exe":""));
    if(options.native) assert(fs.existsSync(probe),"requested public C work probe exists");
    const hasNative = fs.existsSync(probe);
    let nativePairs=0, boundaries=0, recoveries=0, repeatedCancellations=0;
    try {
        for(const item of cases) {
            const {file,bytes}=compile(tool,directory,item.name,item.source);
            const native=limit=>JSON.parse(execFileSync(probe,[file,String(limit),"0"],{encoding:"utf8"}));
            const reference=hasNative?native(0):null;
            if(reference) assert.equal(reference.status,0,item.name);
            if(iprop) {
                const plain=iprop.generate(bytes,{seed:26});
                assert(plain.work.used>0 && plain.work.budget===100000000);
                if(reference) {
                    assert.equal(plain.work.used,reference.workUsed,item.name+" deterministic C/native/WASM work");
                    assert.equal(plain.fuel.used,reference.fuelUsed,item.name+" original fuel");
                    assert.equal(hash(plain.data),reference.rgbaHash,item.name+" pixels");
                    ++nativePairs;
                }
                const exact=iprop.generate(bytes,{seed:26,workLimit:plain.work.used,timeoutMs:60000});
                assert.equal(exact.work.remaining,0);
                assert.deepEqual(exact.data,plain.data); assert.deepEqual(exact.outputs,plain.outputs); assert.deepEqual(exact.fuel,plain.fuel);
                workError(()=>iprop.generate(bytes,{seed:26,workLimit:plain.work.used-1}),3015);++boundaries;
                workError(()=>iprop.generate(bytes,{seed:26,signal:{aborted:true}}),3016);
                workError(()=>iprop.generate(bytes,{seed:26,timeoutMs:0.000001}),3017);
                const recovered=iprop.generate(bytes,{seed:26,timeoutMs:60000});
                assert.deepEqual(recovered.data,plain.data); assert.deepEqual(recovered.outputs,plain.outputs);
                assert.deepEqual(recovered.work,plain.work); ++recoveries;
            }
            if(reference) {
                assert.equal(native(reference.workUsed).status,0);
                const short=native(reference.workUsed-1);
                assert.equal(short.reason,3015); assert.equal(short.stage,4); ++boundaries;
            }
        }
        if(iprop) {
            const {bytes}=compile(tool,directory,"repeated-cancel","asset T {size 1x1 draw {pixel(0,0,#abc)} }");
            for(let i=0;i<2048;++i) {
                const signal={get aborted(){if(this.read)throw Error("host getter failed");this.read=true;return false;}};
                workError(()=>iprop.generate(bytes,{signal}),3016);++repeatedCancellations;
            }
            assert.equal(iprop.generate(bytes,{}).data[3],255,"module remains usable after cancellation/getter errors");
            for(const workLimit of [-1,1.1,Number.MAX_SAFE_INTEGER+1,Infinity,"1",1n])
                assert.throws(()=>iprop.generate(bytes,{workLimit}),/workLimit/);
            for(const timeoutMs of [-1,Infinity,NaN,"1",Number.MAX_SAFE_INTEGER])
                assert.throws(()=>iprop.generate(bytes,{timeoutMs}),/timeoutMs/);
            assert.throws(()=>iprop.generate(bytes,{cancelFlag:new Int32Array(1)}),/shared memory/);
        }
        return {pipelines:cases.length,nativePairs,boundaries,recoveries,repeatedCancellations,internalNative:hasNative};
    } finally {if(owned)fs.rmSync(directory,{recursive:true,force:true});}
}
async function workerCancellation(iprop,tool,directory) {
    const owned=!directory;
    directory=directory||fs.mkdtempSync(path.join(os.tmpdir(),"iprop-work-worker-"));
    let worker;
    try {
        const {bytes}=compile(tool,directory,"worker-scan",`asset Slow {size 1x1 output point state draw {
          m=noise.mask({0,0},{1024,1024},noise.mode_cell_edge,{0.1,0.1},0.2,seed); state={unit(),unit()} }}`);
        const flag=new Int32Array(new SharedArrayBuffer(4));
        worker=new Worker(`const {parentPort,workerData}=require('node:worker_threads');
          const flag=new Int32Array(workerData); parentPort.on('message',()=>setTimeout(()=>Atomics.store(flag,0,1),10));
          parentPort.postMessage('ready');`,{eval:true,workerData:flag.buffer});
        await new Promise((resolve,reject)=>{worker.once("message",resolve);worker.once("error",reject);});
        worker.postMessage("cancel");
        const start=performance.now();
        let failure;
        try {iprop.generate(bytes,{seed:26,cancelFlag:flag});} catch(error){failure=error;}
        const elapsedMs=performance.now()-start;
        assert(failure,"shared cancellation interrupted a synchronous WASM generation");
        assert.equal(failure.reason,3016); assert.equal(failure.stage,4);
        assert(failure.locationFlags&4,"cancellation reached an executing instruction");
        const opcode=bytes[instructions(bytes)[failure.pc]];
        assert.equal(opcode,0xe3,"worker interrupted inside noise.mask, before its return");
        workError(()=>iprop.generate(bytes,{seed:26,timeoutMs:1}),3017);
        return {passed:true,opcode:"noise.mask",elapsedMs,sharedMemory:true};
    } finally {
        if(worker) await worker.terminate();
        if(owned)fs.rmSync(directory,{recursive:true,force:true});
    }
}
module.exports={run,workerCancellation};
if(require.main===module){const tools=path.resolve(process.argv[2]),tool=name=>path.join(tools,name+(process.platform==="win32"?".exe":""));
    const result=run(null,tool,undefined,{native:process.argv[3]});assert.equal(result.boundaries,cases.length,"every pipeline has an exact C work boundary");
    console.log(JSON.stringify(result));}
