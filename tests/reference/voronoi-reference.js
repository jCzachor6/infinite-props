"use strict";
// Independent oracle: transform the complete rectangle into Euclidean metric
// space, clip segments by squared-distance planes, then transform back.
// The runtime instead clips original-space canonical supporting lines.
const assert=require("node:assert/strict");
const bits=n=>{const b=Buffer.alloc(4);b.writeFloatLE(n);return b.readUInt32LE();};
const number=n=>{const b=Buffer.alloc(4);b.writeUInt32LE(n);return b.readFloatLE();};
const defaults={count:1,seed:0,method:0,jitter:1,relax:0,anisotropy:1,direction:[1,0],rowBias:0,rowJitter:0,rowOffset:0.5};
function options(c){const o={...defaults,...c.options};for(const k of ["jitter","anisotropy","rowBias","rowJitter","rowOffset"])o[k]=Math.fround(o[k]);o.direction=o.direction.map(Math.fround);return o;}
function rectangle(c){const origin=c.origin.map(Math.fround),size=c.size.map((s,k)=>Math.fround(origin[k]+Math.fround(s))-origin[k]);return {origin,size};}
function hash(x){x^=x>>>16;x=Math.imul(x,0x7feb352d);x^=x>>>15;x=Math.imul(x,0x846ca68b);return (x^(x>>>16))>>>0;}
function unit(seed,i,l,t=0){return ((hash(seed^0x564f524f^hash(i+0x9e3779b9)^hash(l+0x85ebca6b)^hash(t+0xc2b2ae35))>>>8)+0.5)/16777216;}
function sites(c){
    if(c.sites)return c.sites.map(p=>p.map(Math.fround));
    const rect=rectangle(c),scale=Math.max(...rect.size),o=options(c),[w,h]=rect.size.map(n=>n/scale),columns=Math.max(1,Math.min(o.count,Math.ceil(Math.sqrt(o.count*w/h)))),rows=Math.ceil(o.count/columns);
    const length=Math.sqrt(o.direction[0]**2+o.direction[1]**2),u=o.direction.map(n=>n/length),v=[-u[1],u[0]];
    const minU=Math.min(0,u[0]*w)+Math.min(0,u[1]*h),maxU=Math.max(0,u[0]*w)+Math.max(0,u[1]*h);
    const minV=Math.min(0,v[0]*w)+Math.min(0,v[1]*h),maxV=Math.max(0,v[0]*w)+Math.max(0,v[1]*h);
    const colsR=Math.max(1,Math.min(o.count,Math.ceil(Math.sqrt(o.count*(maxU-minU)/(maxV-minV))))),rowsR=Math.ceil(o.count/colsR),result=[];
    for(let i=0;i<o.count;++i){
        let accepted=false;
        for(let t=0;t<32;++t){
            let p;
            if(o.method===0||t)p=[unit(o.seed,i,0,t)*w,unit(o.seed,i,1,t)*h];
            else{const r=Math.floor(i/columns),m=Math.min(columns,o.count-r*columns);p=[(i%columns+0.5+o.jitter*(unit(o.seed,i,0)-0.5))*w/m,(r+0.5+o.jitter*(unit(o.seed,i,1)-0.5))*h/rows];}
            if(o.rowBias!==0&&!t){
                const r=Math.floor(i/colsR),m=Math.min(colsR,o.count-r*colsR),z=minV+(maxV-minV)*(r+0.5+o.rowJitter*(unit(o.seed,r,2)-0.5))/rowsR;
                let lo=-Infinity,hi=Infinity;
                for(const k of [0,1])if(u[k]!==0){const a=-v[k]*z/u[k],b=([w,h][k]-v[k]*z)/u[k];lo=Math.max(lo,Math.min(a,b));hi=Math.min(hi,Math.max(a,b));}
                let phase=(i%colsR+0.5+(r%2?o.rowOffset:0))/m;phase-=Math.floor(phase);
                const s=lo+(hi-lo)*phase,row=u.map((n,k)=>n*s+v[k]*z);
                p=p.map((n,k)=>n+(row[k]-n)*o.rowBias);
            }
            p=p.map((n,k)=>Math.fround(rect.origin[k]+Math.max(0,Math.min([w,h][k],n))*scale));
            if(!result.some(q=>q[0]===p[0]&&q[1]===p[1])){result.push(p);accepted=true;break;}
        }
        assert(accepted,"reference generated collision");
    }
    return result;
}
function metric(c){const o=options(c),[x,y]=o.direction,l=Math.sqrt(x*x+y*y),u=[x/l,y/l],v=[-u[1],u[0]],a=o.anisotropy;
    if(a===1)return {forward:p=>p,back:p=>p};
    return {forward:p=>[(p[0]*u[0]+p[1]*u[1])/a,p[0]*v[0]+p[1]*v[1]],back:p=>[u[0]*p[0]*a+v[0]*p[1],u[1]*p[0]*a+v[1]*p[1]]};}
function cells(c,points){
    const rect=rectangle(c),m=metric(c),scale=Math.max(...rect.size),w=rect.size[0]/scale,h=rect.size[1]/scale;
    const transformed=points.map(p=>m.forward(p.map((n,k)=>(n-rect.origin[k])/scale)));
    return transformed.map((a,i)=>{
        let polygon=[[0,0],[w,0],[w,h],[0,h]].map(m.forward);
        transformed.forEach((b,j)=>{
            if(i===j)return;
            const n=[b[0]-a[0],b[1]-a[1]],middle=[(a[0]+b[0])/2,(a[1]+b[1])/2],value=p=>n[0]*(p[0]-middle[0])+n[1]*(p[1]-middle[1]),out=[];
            polygon.forEach((p,k)=>{const q=polygon[(k+1)%polygon.length],dp=value(p),dq=value(q);
                if(dp<=0)out.push(p);
                if((dp<=0)!==(dq<=0)){const t=dp/(dp-dq);out.push(p.map((x,axis)=>x+(q[axis]-x)*t));}
            });polygon=out;
        });
        return polygon.map(p=>m.back(p).map((n,k)=>n*scale+rect.origin[k]));
    });
}
function moments(p){
    const [ox,oy]=p[0];let cross=0,mx=0,my=0;
    p.forEach((q,i)=>{const r=p[(i+1)%p.length],ax=q[0]-ox,ay=q[1]-oy,bx=r[0]-ox,by=r[1]-oy,c=ax*by-bx*ay;cross+=c;mx+=(ax+bx)*c;my+=(ay+by)*c;});
    return {area:cross/2,center:[ox+mx/(3*cross),oy+my/(3*cross)]};
}
function relaxed(c){let result=sites(c);for(let i=0;i<(c.relax??c.options?.relax??0);++i)result=cells(c,result).map(p=>moments(p).center.map(Math.fround));return result;}
function verify(c,data){
    const rect=rectangle(c),points=data.points.map(p=>p.map(number)),actualSites=data.sites.map(p=>p.map(number)),referenceSites=relaxed(c),reference=cells(c,actualSites),scale=Math.max(...rect.size),area=rect.size[0]*rect.size[1];
    assert.equal(data.released,0);assert.equal(actualSites.length,referenceSites.length);assert.equal(data.rings.length,actualSites.length);
    if(!(c.relax??c.options?.relax??0))assert.deepEqual(data.sites,referenceSites.map(p=>p.map(bits)),c.name+" independent site bits");
    else actualSites.forEach((p,i)=>p.forEach((n,k)=>assert(Math.abs(n-referenceSites[i][k])<=scale*4e-6,c.name+" independent Lloyd")));
    const owners=new Map(),adj=actualSites.map(()=>new Set());let sum=0;
    data.rings.forEach((ring,i)=>{
        const polygon=ring.map(k=>points[k]),expected=moments(reference[i]),actual=moments(polygon);sum+=actual.area;
        assert(actual.area>0);assert(Math.abs(actual.area-expected.area)<=area*2e-6,c.name+" metric-space reference area "+i);
        assert.equal(data.regions[i][0],i);assert.equal(data.regions[i][1],bits(actual.area));
        assert.deepEqual(data.regions[i].slice(2,4),actual.center.map(bits));
        polygon.forEach((p,k)=>{
            p.forEach((n,axis)=>assert(n>=rect.origin[axis]&&n<=rect.origin[axis]+rect.size[axis],"rectangle clipping"));
            const a=ring[k],b=ring[(k+1)%ring.length],key=[Math.min(a,b),Math.max(a,b)].join(",");
            if(!owners.has(key))owners.set(key,[]);owners.get(key).push([i,a<b]);
            let nearest=Infinity;
            reference[i].forEach((q,j)=>{const r=reference[i][(j+1)%reference[i].length],dx=r[0]-q[0],dy=r[1]-q[1],den=dx*dx+dy*dy,t=den?Math.max(0,Math.min(1,((p[0]-q[0])*dx+(p[1]-q[1])*dy)/den)):0;nearest=Math.min(nearest,Math.hypot(p[0]-q[0]-t*dx,p[1]-q[1]-t*dy));});
            assert(nearest<=scale*4e-6,c.name+" independently transformed polygon boundary");
        });
        if(c.polygons)assert.deepEqual(polygon.map(p=>p.map(bits)).sort(),c.polygons[i].map(p=>p.map(bits)).sort(),"analytic polygon bits");
    });
    assert(Math.abs(sum-area)<=area*2e-6,"area conservation");assert.equal(data.edges.length,owners.size);
    data.edges.forEach((e,i)=>{
        assert.equal(e[2],i,"canonical provenance edge ID");const uses=owners.get(e.slice(0,2).join(","));assert(uses);assert(uses.length===1||uses.length===2);
        if(uses.length===2){assert.notEqual(uses[0][1],uses[1][1]);assert.equal(e[5],1);assert.deepEqual(e.slice(3,5),uses.map(x=>x[0]).sort((a,b)=>a-b));adj[uses[0][0]].add(uses[1][0]);adj[uses[1][0]].add(uses[0][0]);}
        else{assert.equal(e[5],0);const p=points[e[0]],q=points[e[1]];assert([0,1].some(k=>p[k]===q[k]&&(p[k]===rect.origin[k]||p[k]===rect.origin[k]+rect.size[k])),"no singly owned interior boundary");}
    });
    data.regions.forEach((r,i)=>assert.deepEqual(r[8],[...adj[i]].sort((a,b)=>a-b),"shared-edge neighbors"));
}
module.exports={bits,number,defaults,sites,cells,relaxed,verify};
