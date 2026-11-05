"use strict";

// Independent binary32 raster/threshold model. Scalar references use their
// independent formulas; Worley sorts all candidates, never calls the VM.
const scalar=require("./noise-v09-reference.js"),f=Math.fround;
const add=(a,b)=>f(a+b),sub=(a,b)=>f(a-b),mul=(a,b)=>f(a*b),div=(a,b)=>f(a/b);
function valueNoise(x,y,seed){
    const ix=Math.floor(x),iy=Math.floor(y),u0=sub(x,f(ix)),v0=sub(y,f(iy));
    const smooth=t=>mul(mul(t,t),sub(3,mul(2,t))),u=smooth(u0),v=smooth(v0);
    const corner=(cx,cy)=>(scalar.hash(seed^Math.imul(cx,73856093)^Math.imul(cy,19349663))>>>8)/16777216;
    const a=add(mul(corner(ix,iy),sub(1,u)),mul(corner(ix+1,iy),u));
    const b=add(mul(corner(ix,iy+1),sub(1,u)),mul(corner(ix+1,iy+1),u));
    return add(mul(a,sub(1,v)),mul(b,v));
}
function ridge(x,y,octaves,seed){
    let sum=0,amp=f(0.5),freq=1,total=0;
    for(let i=0;i<octaves;++i){
        const g=scalar.gradient(mul(x,freq),mul(y,freq),(seed+Math.imul(i,0x9e3779b9))>>>0),r0=sub(1,Math.abs(g));
        sum=add(sum,mul(mul(r0,r0),amp));total=add(total,amp);amp=mul(amp,0.5);freq=mul(freq,2);
    }
    return div(sum,total);
}
function sample(v,x,y){
    if(v.warp!==0)[x,y]=scalar.warp(x,y,f(v.warp),v.seed);
    switch(v.kind){
    case 0:return valueNoise(x,y,v.seed);
    case 1:return scalar.gradient(x,y,v.seed);
    case 2:return scalar.fbm(x,y,v.octaves,2,0.5,v.seed);
    case 3:return scalar.worley(x,y,v.seed).first;
    case 4:{const q=scalar.worley(x,y,v.seed);return sub(q.second,q.first);}
    case 5:return ridge(x,y,v.octaves,v.seed);
    default:throw Error("kind");
    }
}
function alpha(value,v){
    const threshold=f(v.threshold),softness=f(v.softness);
    let a;
    if(softness===0)a=(v.comparison===0?value>=threshold:value<=threshold)?1:0;
    else{
        const t=Math.max(0,Math.min(1,add(div(sub(value,threshold),softness),0.5)));
        a=mul(mul(t,t),sub(3,mul(2,t)));if(v.comparison)a=sub(1,a);
    }
    return Math.floor(add(mul(a,255),0.5));
}
function raster(v){
    const origin=v.origin.map(f),end=origin.map((x,i)=>add(x,f(v.size[i]))),scale=v.host.map((x,i)=>div(x,v.asset[i]));
    const left=Math.floor(mul(origin[0],scale[0])),top=Math.floor(mul(origin[1],scale[1]));
    const right=Math.max(left+1,Math.ceil(mul(end[0],scale[0]))),bottom=Math.max(top+1,Math.ceil(mul(end[1],scale[1])));
    const coverage=[];
    for(let py=top;py<bottom;++py)for(let px=left;px<right;++px){
        const x=div(add(f(px),0.5),scale[0]),y=div(add(f(py),0.5),scale[1]);
        coverage.push(x<origin[0]||y<origin[1]||x>=end[0]||y>=end[1]?0:
            alpha(sample(v,mul(x,f(v.frequency[0])),mul(y,f(v.frequency[1]))),v));
    }
    const visible=Array(v.host[0]*v.host[1]).fill(0),offset=v.offset||[0,0];
    for(let y=top;y<bottom;++y)for(let x=left;x<right;++x){
        const px=x+offset[0],py=y+offset[1];if(px>=0&&py>=0&&px<v.host[0]&&py<v.host[1])
            visible[py*v.host[0]+px]=coverage[(y-top)*(right-left)+x-left];
    }
    return {origin:[left,top],extent:[right-left,bottom-top],coverage,visible};
}
module.exports={raster,sample,alpha};
