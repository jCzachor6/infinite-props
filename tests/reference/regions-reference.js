"use strict";
// Independent read-only checks: polygon moments, segment intersections,
// Euclidean signed-distance offsets away from the chord tolerance, and a
// deliberately naive per-subsample rasterizer (the runtime uses scanlines).
const assert=require('node:assert/strict');
const scratch=new DataView(new ArrayBuffer(4));
const bits=n=>{scratch.setFloat32(0,n,true);return scratch.getUint32(0,true);};
const value=n=>{scratch.setUint32(0,n,true);return scratch.getFloat32(0,true);};
const point=p=>p.map(value),key=p=>[p[1],p[0]],cross=(a,b,c)=>(b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);
const cmp=(a,b)=>{for(let i=0;i<a.length;i++)if(a[i]!==b[i])return a[i]<b[i]?-1:1;return 0;};
function rings(d){return d.rings.map(r=>r.vertices.map(i=>point(d.points[i])));}
function containsRing(points,p){let inside=false;for(let k=0;k<points.length;k++){
    const a=points[k],b=points[(k+1)%points.length];if((a[1]>p[1])!==(b[1]>p[1])){
        const lo=a[1]<b[1]?a:b,hi=a[1]<b[1]?b:a;
        const x=lo[0]+(p[1]-lo[1])*(hi[0]-lo[0])/(hi[1]-lo[1]);if(p[0]<x)inside=!inside;
    }}return inside;}
function contains(d,loops,r,p){return containsRing(loops[r.outer],p)&&r.holes.every(h=>!containsRing(loops[h],p));}
function moments(points){let sum=0,x=0,y=0;const origin=points[0];for(let k=0;k<points.length;k++){
    const a=points[k].map((v,i)=>v-origin[i]),b=points[(k+1)%points.length].map((v,i)=>v-origin[i]);
    const c=a[0]*b[1]-b[0]*a[1];sum+=c;x+=(a[0]+b[0])*c;y+=(a[1]+b[1])*c;
}const area=sum/2;return[area,x/6+origin[0]*area,y/6+origin[1]*area];}
function doubleBits(n){const b=Buffer.alloc(8);b.writeDoubleLE(n);return b.readBigUInt64LE().toString(16).padStart(16,'0');}
function on(a,b,p){return cross(a,b,p)===0&&p[0]>=Math.min(a[0],b[0])&&p[0]<=Math.max(a[0],b[0])&&p[1]>=Math.min(a[1],b[1])&&p[1]<=Math.max(a[1],b[1]);}
function intersects(a,b,c,d){
    if(Math.max(a[0],b[0])<Math.min(c[0],d[0])||Math.max(c[0],d[0])<Math.min(a[0],b[0])||Math.max(a[1],b[1])<Math.min(c[1],d[1])||Math.max(c[1],d[1])<Math.min(a[1],b[1]))return false;
    const ac=cross(a,b,c),ad=cross(a,b,d),ca=cross(c,d,a),cb=cross(c,d,b);
    return on(a,b,c)||on(a,b,d)||on(c,d,a)||on(c,d,b)||((ac>0)!==(ad>0)&&(ca>0)!==(cb>0));
}
function segmentDistance(a,b,p){const x=b[0]-a[0],y=b[1]-a[1];const t=Math.max(0,Math.min(1,((p[0]-a[0])*x+(p[1]-a[1])*y)/(x*x+y*y)));
    return Math.hypot(p[0]-a[0]-x*t,p[1]-a[1]-y*t);}
function distance(loops,region,p){let result=Infinity;for(const i of[region.outer,...region.holes]){
    const ring=loops[i];for(let k=0;k<ring.length;k++)result=Math.min(result,segmentDistance(ring[k],ring[(k+1)%ring.length],p));}return result;}
function validate(d,{canonical=false}={}){
    const loops=rings(d),metrics=loops.map(moments),owners=Array(d.rings.length).fill(0);
    for(let i=0;i<loops.length;i++){
        const p=loops[i],r=d.rings[i];assert(p.length>=3&&p.length<=2048);assert(p.every(v=>v.every(Number.isFinite)));
        assert.equal(metrics[i][0]<0,r.hole);assert.deepEqual(metrics[i].map(doubleBits),r.moments,'independent binary64 area and first moments');
        assert(r.parent===0xffffffff||r.parent<i);if(canonical)assert(p.every(v=>cmp(key(p[0]),key(v))<=0),'canonical minimum vertex');
        for(let a=0;a<p.length;a++)for(let b=a+1;b<p.length;b++){
            if(b===a+1||(a===0&&b===p.length-1))continue;
            assert(!intersects(p[a],p[(a+1)%p.length],p[b],p[(b+1)%p.length]),'simple ring without crossing or self-touch');
        }
    }
    for(let i=0;i<d.regions.length;i++){
        const r=d.regions[i],outer=loops[r.outer];owners[r.outer]++;let [area,mx,my]=metrics[r.outer];
        for(const h of r.holes){owners[h]++;assert(d.rings[h].hole);assert.equal(d.rings[h].parent,r.outer);assert(containsRing(outer,loops[h][0]),'hole inside outer');
            area+=metrics[h][0];mx+=metrics[h][1];my+=metrics[h][2];}
        assert(area>0);assert.equal(bits(area),r.area);assert.deepEqual([bits(mx/area),bits(my/area)],r.center);
        const lo=[0,1].map(axis=>Math.min(...outer.map(p=>p[axis]))),hi=[0,1].map(axis=>Math.max(...outer.map(p=>p[axis])));
        assert.deepEqual(lo.map(bits),r.origin);assert.deepEqual(hi.map((v,i)=>bits(v-lo[i])),r.size);
        assert.deepEqual(r.neighbors,[...new Set(r.neighbors)].sort((a,b)=>a-b));
        for(const n of r.neighbors){assert(n>=0&&n<d.regions.length&&n!==i);assert(d.regions[n].neighbors.includes(i),'symmetric provenance adjacency');}
        for(const a of[r.outer,...r.holes])for(const b of[r.outer,...r.holes])if(a<b){
            const p=loops[a],q=loops[b];for(let j=0;j<p.length;j++)for(let k=0;k<q.length;k++)assert(!intersects(p[j],p[(j+1)%p.length],q[k],q[(k+1)%q.length]),'separate component rings');}
    }
    assert(owners.every(n=>n===1),'one region owner per ring');return loops;
}
function raster(d,mask,index=-1,check=true){
    const loops=rings(d),rs=index<0?d.regions:[d.regions[index]],scale=mask.scale.map(value),pixels=[];
    for(let y=0;y<mask.size[1];y++)for(let x=0;x<mask.size[0];x++){
        let inside=0;for(let sy=0;sy<4;sy++)for(let sx=0;sx<4;sx++){
            const p=[(mask.origin[0]+x+(sx+.5)/4)/scale[0],(mask.origin[1]+y+(sy+.5)/4)/scale[1]];
            if(rs.some(r=>contains(d,loops,r,p)))inside++;
        }pixels.push(Math.floor((inside*255+8)/16));
    }if(check)assert.deepEqual(mask.pixels,pixels,'independent union of sample memberships including holes');return pixels;
}
function makeMask(d,scale=[1,1],index=-1){
    const loops=rings(d),selected=index<0?d.regions:[d.regions[index]],points=selected.flatMap(r=>loops[r.outer]);
    const origin=points.length?[0,1].map(a=>Math.floor(Math.min(...points.map(p=>p[a]))*scale[a])):[0,0];
    const size=points.length?[0,1].map(a=>Math.max(1,Math.ceil(Math.max(...points.map(p=>p[a]))*scale[a])-origin[a])):[1,1];
    const mask={origin,size,scale:scale.map(bits),pixels:[]};mask.pixels=raster(d,mask,index,false);return mask;
}
function check(c,record){
    const source=record.source,result=record.result,loops=validate(result,{canonical:!!c.inset}),original=validate(source),minimum=c.minArea||0;
    assert.equal(record.released,0);assert(result.regions.every(r=>source.regions.some(s=>s.source===r.source)));
    if(c.expected){const e=c.expected;if(e.components!==undefined)assert.equal(result.regions.length,e.components,c.name);
        if(e.holes!==undefined)assert.equal(result.regions.reduce((n,r)=>n+r.holes.length,0),e.holes,c.name);
        if(e.ids)assert.deepEqual(result.regions.map(r=>r.source),e.ids,c.name);if(e.areas)assert.deepEqual(result.regions.map(r=>value(r.area)),e.areas,c.name);}
    const areas=result.regions.map(r=>[r.outer,...r.holes].reduce((n,k)=>n+moments(loops[k])[0],0));assert(areas.every(a=>a>=minimum));
    // Source order; fragments use binary64 net area then bbox. Discovery is
    // only needed if every preceding value is exactly equal.
    const sources=source.regions.map(r=>r.source);let previous=null;
    result.regions.forEach((r,i)=>{const at=sources.indexOf(r.source),p=loops[r.outer],lo=[Math.min(...p.map(v=>v[0])),Math.min(...p.map(v=>v[1]))],hi=[Math.max(...p.map(v=>v[0])),Math.max(...p.map(v=>v[1]))];
        const sortKey=[at,-areas[i],lo[1],lo[0],hi[1],hi[0]];if(previous&&c.inset)assert(cmp(previous,sortKey)<=0,'source and fragment order');previous=sortKey;});
    const inset=c.inset||0;
    const edgeSets=result.regions.map(r=>{
        const found=[];for(const k of[r.outer,...r.holes]){const vertices=result.rings[k].vertices;
            for(let n=0;n<vertices.length;n++){const a=vertices[n],b=vertices[(n+1)%vertices.length];
                const edge=result.edges.find(e=>e[0]===Math.min(a,b)&&e[1]===Math.max(a,b));assert(edge,'canonical edge exists');found.push(edge);}}
        return found;
    });
    result.regions.forEach((r,i)=>{
        let expected;
        if(inset){
            for(const e of edgeSets[i]){assert(e[0]<e[1]&&e[1]<result.points.length);assert(e[2]<source.edges.length);assert.equal(e[3],r.source);assert.equal(e[5],3);}
            expected=result.regions.flatMap((other,j)=>j!==i&&other.source!==r.source&&edgeSets[i].some(a=>a[4]===other.source&&edgeSets[j].some(b=>b[4]===r.source&&a[2]===b[2]))?[j]:[]);
        }else{
            const old=source.regions.find(s=>s.source===r.source);const ids=old.neighbors.map(n=>source.regions[n].source);
            expected=result.regions.flatMap((other,j)=>ids.includes(other.source)?[j]:[]);
        }
        assert.deepEqual(r.neighbors,expected,'surviving original-edge fragment adjacency');
    });
    if(!minimum){let state=123456789;const random=()=>{state=(Math.imul(state,1664525)+1013904223)>>>0;return state/2**32;};
        for(const region of source.regions){
            const p=original[region.outer],padding=Math.abs(inset)+.5,lo=[0,1].map(a=>Math.min(...p.map(v=>v[a]))-padding),hi=[0,1].map(a=>Math.max(...p.map(v=>v[a]))+padding);
            const tolerance=1/64+Math.max(...p.flat().map(Math.abs),Math.abs(inset),1)*2**-21;
            for(let i=0;i<192;i++){
                const q=lo.map((v,a)=>v+(hi[a]-v)*random()),dist=distance(original,region,q);if(Math.abs(dist-Math.abs(inset))<=tolerance)continue;
                const inside=contains(source,original,region,q),expected=inset>=0?inside&&dist>=inset:inside||dist<=-inset;
                const actual=result.regions.some(r=>r.source===region.source&&contains(result,loops,r,q));assert.equal(actual,expected,'Euclidean offset away from bounded arc/rounding tolerance');
            }
            if(inset)for(const r of result.regions.filter(r=>r.source===region.source))for(const index of[r.outer,...r.holes]){
                const ps=loops[index];for(let k=0;k<ps.length;k++){
                    const q=ps[k].map((v,a)=>(v+ps[(k+1)%ps.length][a])/2);
                    assert(Math.abs(distance(original,region,q)-Math.abs(inset))<=tolerance,'offset boundary stays within pinned round tolerance');
                }
            }
        }
    }
    if(record.mask)raster(result,record.mask,c.maskIndex??-1);
    return{components:result.regions.length,rings:result.rings.length,vertices:result.rings.reduce((n,r)=>n+r.vertices.length,0)};
}
function argumentsOf(c,budget=1e9,occupied=0){let data=[],kind=1;
    if(c.diagram){kind=0;const d=c.diagram;data=[...d.origin.map(bits),...d.size.map(bits),d.sites.length,...d.sites.flat().map(bits)];}
    else if(c.pixels){kind=2;data=[c.pixels[0].length,c.pixels.length,...c.pixels.flat()];}
    else{data=[c.rings.length];for(const r of c.rings)data.push(r.parent>>>0,r.points.length,r.id>>>0,...r.points.flat().map(bits));}
    return[kind,data.join(','),bits(c.inset||0),bits(c.minArea||0),...(c.scale||[1,1]).map(bits),c.maskIndex??-1,budget,occupied].map(String);
}
module.exports={bits,value,rings,containsRing,moments,validate,raster,makeMask,check,argumentsOf};
