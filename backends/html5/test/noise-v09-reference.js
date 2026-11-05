"use strict";

// Independent binary32 model of spec/noise-v0.9.md. It does not call native
// tools or runtime kernels. Worley sorts all candidates, rather than using
// the VM's incremental first/second scan.
const f = Math.fround, buffer = Buffer.alloc(4);
const bits = x => { buffer.writeFloatLE(x); return buffer.readUInt32LE(); };
const value = x => { buffer.writeUInt32LE(x); return buffer.readFloatLE(); };
const add = (a,b) => f(a+b), sub = (a,b) => f(a-b), mul = (a,b) => f(a*b);
function hash(x) {
    x ^= x >>> 16; x = Math.imul(x, 0x7feb352d);
    x ^= x >>> 15; x = Math.imul(x, 0x846ca68b);
    return (x ^ (x >>> 16)) >>> 0;
}
const cellHash = (seed,x,y) => hash(seed ^ Math.imul(x,73856093) ^ Math.imul(y,19349663));
function index(x, lower, upper) {
    if (!Number.isFinite(x) || Math.floor(x)<lower || Math.floor(x)>upper)
        throw Error("noise coordinate out of range");
    return Math.floor(x);
}
function jitter(cell, offset) {
    const coordinate = add(cell,offset);
    if (coordinate < cell+1) return coordinate;
    if (coordinate === 0) return value(0x80000001);
    return value((bits(coordinate) + (coordinate<0 ? 1 : -1)) >>> 0);
}
function worley(x,y,seed) {
    const ix=index(x,-16777214,16777214), iy=index(y,-16777214,16777214), candidates=[];
    for(let cy=iy-2;cy<=iy+2;++cy)for(let cx=ix-2;cx<=ix+2;++cx) {
        const id=cellHash(seed,cx,cy);
        const px=jitter(cx,(id>>>8)/16777216), py=jitter(cy,(hash(id^0x9e3779b9)>>>8)/16777216);
        const rx=sub(px,x), ry=sub(py,y);
        candidates.push({cx,cy,id,distance:f(Math.sqrt(add(mul(rx,rx),mul(ry,ry))))});
    }
    candidates.sort((a,b)=>a.distance-b.distance || a.cy-b.cy || a.cx-b.cx);
    return {first:candidates[0].distance,second:candidates[1].distance,id:candidates[0].id,
        winner:[candidates[0].cx,candidates[0].cy]};
}
function gradient(x,y,seed) {
    const ix=index(x,-2147483648,2147483646), iy=index(y,-2147483648,2147483646);
    const fx=sub(x,f(ix)), fy=sub(y,f(iy));
    const directions=[[1,1],[-1,1],[1,-1],[-1,-1],[1,0],[-1,0],[0,1],[0,-1]];
    const fade=t=>mul(mul(mul(t,t),t),add(mul(t,sub(mul(t,6),15)),10));
    const corner=(ox,oy)=>{
        const [gx,gy]=directions[cellHash(seed,ix+ox,iy+oy)&7];
        return add(mul(gx,sub(fx,ox)),mul(gy,sub(fy,oy)));
    };
    const u=fade(fx), v=fade(fy);
    const p=add(mul(corner(0,0),sub(1,u)),mul(corner(1,0),u));
    const q=add(mul(corner(0,1),sub(1,u)),mul(corner(1,1),u));
    return add(mul(p,sub(1,v)),mul(q,v));
}
function fbm(x,y,octaves,lacunarity,gain,seed) {
    if(!Number.isFinite(lacunarity)||lacunarity<=0||lacunarity>8)throw Error("fbm lacunarity out of range");
    if(!Number.isFinite(gain)||gain<0||gain>1)throw Error("fbm gain out of range");
    const n=gain===0?1:Math.max(1,Math.min(8,octaves));
    let sum=0,total=0,amp=1,freq=1;
    for(let i=0;i<n;++i) {
        sum=add(sum,mul(amp,gradient(mul(x,freq),mul(y,freq),(seed+Math.imul(i,0x9e3779b9))>>>0)));
        total=add(total,amp);amp=mul(amp,gain);freq=mul(freq,lacunarity);
    }
    return f(sum/total);
}
function warp(x,y,amount,seed) {
    if(!Number.isFinite(amount))throw Error("warp amount out of range");
    index(x,-2147483648,2147483646);index(y,-2147483648,2147483646);
    if(amount===0)return [x,y];
    const qx=fbm(x,y,4,2,0.5,seed), qy=fbm(add(x,f(3.7)),add(y,f(9.1)),4,2,0.5,seed);
    const wx=add(x,mul(amount,qx)),wy=add(y,mul(amount,qy));
    index(wx,-2147483648,2147483646);index(wy,-2147483648,2147483646);
    return [wx,wy];
}
function evaluate(v) {
    const x=value(v.x),y=value(v.y);
    if(v.op==="cell2")return [bits(worley(x,y,v.seed).second)];
    if(v.op==="cell_id")return [worley(x,y,v.seed).id];
    if(v.op==="warp_vec")return warp(x,y,value(v.amount),v.seed).map(bits);
    if(v.op==="fbm")return [bits(fbm(x,y,v.octaves,value(v.lacunarity),value(v.gain),v.seed))];
    throw Error("unknown reference operation");
}
module.exports={bits,value,hash,worley,gradient,fbm,warp,evaluate};
