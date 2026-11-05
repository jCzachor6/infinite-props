"use strict";
// The same binaries and work policy as capture.py; JS getter timing is only
// benchmark instrumentation. It uses the public synchronous cancellation seam.
const fs=require("node:fs"), path=require("node:path"), assert=require("node:assert/strict");
const crypto=require("node:crypto"), {performance}=require("node:perf_hooks");
function hash(bytes) {
    let value=14695981039346656037n;
    for(const byte of bytes) value=BigInt.asUintN(64,(value^BigInt(byte))*1099511628211n);
    return String(value);
}
(async()=>{
    const [build,input,output]=process.argv.slice(2);
    const report=JSON.parse(fs.readFileSync(input));
    const moduleFile=path.resolve(build,"iprop.js");
    const Module=await require(moduleFile)();
    const iprop=await require("../../backends/html5/js/api.js")(()=>Promise.resolve(Module)).create();
    for(const row of report.cases) {
        const bytes=fs.readFileSync(row.binary);
        assert.equal(crypto.createHash("sha256").update(bytes).digest("hex"),row.binary_sha256);
        const samples=[], probed=[];
        for(let i=0;i<report.iterations+3;++i) {
            for(const observe of [false,true]) {
                let previous=performance.now(), maximum=0, polls=0, result, failure;
                const signal={get aborted(){
                    const now=performance.now();
                    // The first read is JS option validation; the second is
                    // the first native checkpoint after load/validation.
                    if(polls>=2) maximum=Math.max(maximum,now-previous);
                    previous=now; ++polls; return false;
                }};
                const start=performance.now(); previous=start;
                try {result=iprop.generate(bytes,{seed:7,workLimit:row.work_limit,...(observe?{signal}:{})});}
                catch(error){failure=error;}
                const end=performance.now();
                const sample={wall_ns:(end-start)*1e6, reason:failure?failure.reason:0, status:failure?failure.status:0};
                if(result) Object.assign(sample,{work_used:result.work.used,fuel_used:result.fuel.used,rgba_fnv1a64:hash(result.data)});
                for(const key of ["reason","work_used","fuel_used","rgba_fnv1a64"])
                    assert.equal(sample[key],row.observed[key],row.name+" native/WASM "+key);
                if(observe) Object.assign(sample,{polls:Math.max(0,polls-1),max_poll_gap_ns:maximum*1e6,post_poll_ns:(end-previous)*1e6});
                if(i>=3) (observe?probed:samples).push(sample);
            }
        }
        row.wasm={plain:samples,probed};
        console.log(row.name,"reason",row.observed.reason);
    }
    report.wasm_environment={node:process.version,arch:process.arch,platform:process.platform,
        module_sha256:crypto.createHash("sha256").update(fs.readFileSync(moduleFile)).digest("hex"),
        wasm_sha256:crypto.createHash("sha256").update(fs.readFileSync(path.resolve(build,"iprop.wasm"))).digest("hex"),
        scope:"Public wrapper includes program load and JS result delivery. Getter probes include preflight validation; observed gaps include scheduler/GC/allocator effects and are not universal time bounds."};
    fs.writeFileSync(output,JSON.stringify(report,null,2)+"\n");
})().catch(error=>{console.error(error);process.exitCode=1;});
