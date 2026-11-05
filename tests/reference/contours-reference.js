"use strict";
// Independent coordinate-key graph and raster flood-fill oracles. No runtime
// executable, extracted geometry or runtime source supplies expected topology.
const assert=require("node:assert/strict");
const table=require("../fixtures/contours-v0.9.json");
const bits=x=>{const b=Buffer.alloc(4);b.writeFloatLE(x);return b.readUInt32LE();};
const number=x=>{const b=Buffer.alloc(4);b.writeUInt32LE(x);return b.readFloatLE();};
const turn=(a,b,c)=>(b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);
const same=(a,b)=>a[0]===b[0]&&a[1]===b[1];
const between=(a,b,p)=>p[0]>=Math.min(a[0],b[0])&&p[0]<=Math.max(a[0],b[0])&&p[1]>=Math.min(a[1],b[1])&&p[1]<=Math.max(a[1],b[1]);
function intersects(a,b,c,d){
    const ac=turn(a,b,c),ad=turn(a,b,d),ca=turn(c,d,a),cb=turn(c,d,b);
    return ac===0&&between(a,b,c)||ad===0&&between(a,b,d)||ca===0&&between(c,d,a)||cb===0&&between(c,d,b)||ac*ad<0&&ca*cb<0;
}
function contains(r,p){let yes=false;for(let i=0,j=r.length-1;i<r.length;j=i++){
    const a=r[j],b=r[i];if((a[1]>p[1])!==(b[1]>p[1])&&p[0]<a[0]+(p[1]-a[1])*(b[0]-a[0])/(b[1]-a[1]))yes=!yes;
}return yes;}
function area(r){const o=r[0];return r.reduce((sum,a,i)=>{const b=r[(i+1)%r.length];return sum+(a[0]-o[0])*(b[1]-o[1])-(b[0]-o[0])*(a[1]-o[1]);},0)/2;}
function moments(r){const o=r[0];let sum=0,x=0,y=0;for(let i=0;i<r.length;++i){const a=r[i].map((v,k)=>v-o[k]),b=r[(i+1)%r.length].map((v,k)=>v-o[k]),c=a[0]*b[1]-b[0]*a[1];sum+=c;x+=(a[0]+b[0])*c;y+=(a[1]+b[1])*c;}const area=sum/2;return [area,x/6+o[0]*area,y/6+o[1]*area];}
function doubleBits(x){const b=Buffer.alloc(8);b.writeDoubleLE(x);return b.readBigUInt64LE().toString(16).padStart(16,"0");}
function canonical(r,hole=false){
    r=r.slice();if((area(r)<0)!==hole)r.reverse();
    let first=0;for(let i=1;i<r.length;++i)if(r[i][1]<r[first][1]||r[i][1]===r[first][1]&&r[i][0]<r[first][0])first=i;
    return [...r.slice(first),...r.slice(0,first)];
}
function mapping(c,p){
    const w=c.pixels[0].length,h=c.pixels.length,[x,y]=p,k=c.d4||0;
    const q=[[x,y],[y,w-x],[w-x,h-y],[h-y,x],[w-x,y],[x,h-y],[y,x],[h-y,w-x]][k];
    let scale=c.scale||[1,1];if([1,3,6,7].includes(k))scale=[scale[1],scale[0]];
    return q.map((v,i)=>Math.fround((v+(c.origin||[0,0])[i])/Math.fround(scale[i]))||0);
}
function original(c){
    const w=c.pixels[0].length,h=c.pixels.length,level=Math.fround(c.threshold??0.5)*255;
    const alpha=(x,y)=>c.pixels[Math.max(0,Math.min(h-1,y-1))][Math.max(0,Math.min(w-1,x-1))];
    const inside=a=>level===0?a>0:a>=level;
    const point=(x,y)=>[Math.max(0,Math.min(w,x-0.5)),Math.max(0,Math.min(h,y-0.5))];
    const key=p=>p.join(":");const segments=new Map();
    function add(a,b){assert(!segments.has(key(a)),"oracle outgoing degree");segments.set(key(a),[a,b]);}
    function cross(a,b){if(a[1]>b[1]||a[0]>b[0])[a,b]=[b,a];const av=alpha(...a),bv=alpha(...b),t=Math.max(1/1024,Math.min(1023/1024,(level-av)/(bv-av))),p=point(...a),q=point(...b);return p.map((v,i)=>v+(q[i]-v)*t);}
    for(let y=0;y<=h;++y)for(let x=0;x<=w;++x){
        const nodes=[[x,y],[x+1,y],[x+1,y+1],[x,y+1]],states=nodes.map(p=>inside(alpha(...p)));
        const mask=states.reduce((s,v,i)=>s+(v?1<<i:0),0),pairs=(c.connectivity?table.eight:table.four)[mask];
        for(const [a,b] of pairs)add(cross(nodes[a],nodes[(a+1)%4]),cross(nodes[b],nodes[(b+1)%4]));
        for(let i=0;i<4;++i)if([y===0,x===w,y===h,x===0][i]){
            const j=(i+1)%4;if(!states[i]&&!states[j])continue;
            add(states[i]?point(...nodes[i]):cross(nodes[i],nodes[j]),states[j]?point(...nodes[j]):cross(nodes[i],nodes[j]));
        }
    }
    const loops=[];
    while(segments.size){const first=segments.values().next().value[0],r=[];let p=first;
        do{const edge=segments.get(key(p));assert(edge,"oracle closed graph");segments.delete(key(p));r.push(mapping(c,p));p=edge[1];}while(!same(p,first));
        let changed=true;while(changed&&r.length>3){changed=false;for(let i=0;i<r.length;++i){const a=r[(i+r.length-1)%r.length],b=r[i],d=r[(i+1)%r.length];if(turn(a,b,d)===0&&between(a,d,b)){r.splice(i,1);changed=true;break;}}}
        loops.push(r);
    }
    return loops;
}
function nesting(loops){return loops.map((r,i)=>{
    const enclosing=loops.map((p,j)=>({j,a:Math.abs(area(p))})).filter(p=>p.j!==i&&p.a>Math.abs(area(r))&&contains(loops[p.j],r[0])).sort((a,b)=>a.a-b.a);
    return {parent:enclosing[0]?.j??-1,hole:enclosing.length%2===1};
});}
function rasterTopology(c){
    const w=c.pixels[0].length,h=c.pixels.length,level=Math.fround(c.threshold??0.5)*255;
    const samples=c.pixels.map(r=>r.map(a=>level===0?a>0:a>=level));
    function groups(foreground,eight){let count=0;const seen=new Set();
        for(let y=0;y<h;++y)for(let x=0;x<w;++x){if(samples[y][x]!==foreground||seen.has(y*w+x))continue;
            const pending=[[x,y]];seen.add(y*w+x);let boundary=false;
            while(pending.length){const [a,b]=pending.pop();boundary||=a===0||b===0||a===w-1||b===h-1;
                for(let dy=-1;dy<=1;++dy)for(let dx=-1;dx<=1;++dx){if(dx===0&&dy===0||!eight&&dx!==0&&dy!==0)continue;
                    const nx=a+dx,ny=b+dy,k=ny*w+nx;if(nx<0||ny<0||nx>=w||ny>=h||samples[ny][nx]!==foreground||seen.has(k))continue;
                    seen.add(k);pending.push([nx,ny]);}}
            if(foreground||!boundary)++count;
        }return count;
    }
    return {components:groups(true,!!c.connectivity),holes:groups(false,!c.connectivity)};
}
function distance(p,a,b){const x=b[0]-a[0],y=b[1]-a[1],t=Math.max(0,Math.min(1,((p[0]-a[0])*x+(p[1]-a[1])*y)/(x*x+y*y)));return Math.hypot(p[0]-a[0]-t*x,p[1]-a[1]-t*y);}
function verify(c,data){
    assert.equal(data.released,0);const points=data.points.map(p=>p.map(number)),loops=data.rings.map(r=>r.vertices.map(i=>points[i]));
    const originals=original(c),parents=nesting(originals),actualParents=nesting(loops);
    const owned=new Set(),usedEdges=new Set(),usedPoints=new Set();
    const matches=loops.map((r,i)=>{const index=originals.findIndex(p=>r.every(v=>p.some(q=>same(v,q))));assert(index>=0,c.name+" vertices come from original ring");
        assert.equal(data.rings[i].hole,actualParents[i].hole);assert.equal(data.rings[i].parent,actualParents[i].parent<0?0xffffffff:actualParents[i].parent);
        assert.deepEqual(r,canonical(r,data.rings[i].hole),"canonical orientation and start");
        assert.deepEqual(data.rings[i].moments,moments(r).map(doubleBits),"all cached binary64 moments");
        for(let k=0;k<r.length;++k){const a=data.rings[i].vertices[k],b=data.rings[i].vertices[(k+1)%r.length],id=data.edges.findIndex(e=>e[0]===Math.min(a,b)&&e[1]===Math.max(a,b));
            assert(id>=0);assert(!usedEdges.has(id));usedEdges.add(id);usedPoints.add(a);assert.equal(data.edges[id][2],id);assert.equal(data.edges[id][4],-1);assert.equal(data.edges[id][5],2);}
        const original=canonical(originals[index],data.rings[i].hole);
        if(!c.simplify)assert.deepEqual(r,original,"independent interpolated ring");
        else for(let j=0;j<r.length;++j){const a=r[j],b=r[(j+1)%r.length],start=original.findIndex(p=>same(p,a));let k=(start+1)%original.length;
            while(!same(original[k],b)){assert(distance(original[k],a,b)<=Math.fround(c.simplify)+1e-12,"original arc error bound");k=(k+1)%original.length;assert(k!==start);}}
        return index;});
    assert.equal(new Set(matches).size,matches.length,"no duplicate rings");
    for(let i=0;i<loops.length;++i)for(let j=i;j<loops.length;++j)for(let a=0;a<loops[i].length;++a)for(let b=0;b<loops[j].length;++b){
        if(i===j&&(a===b||(a+1)%loops[i].length===b||(b+1)%loops[i].length===a))continue;
        assert(!intersects(loops[i][a],loops[i][(a+1)%loops[i].length],loops[j][b],loops[j][(b+1)%loops[j].length]),"simple disjoint rings");}
    for(let i=0;i<data.regions.length;++i){const r=data.regions[i];assert.equal(r.source,i);assert.deepEqual(r.neighbors,[]);
        for(const ring of [r.outer,...r.holes]){assert(!owned.has(ring));owned.add(ring);assert.equal(data.rings[ring].hole,ring!==r.outer);
            for(const v of data.rings[ring].vertices){const edge=data.edges.find(e=>e[0]===v||e[1]===v);assert.equal(edge[3],i,"contour source provenance");}}
        const expected=area(loops[r.outer])+r.holes.reduce((s,h)=>s+area(loops[h]),0);assert.equal(r.area,bits(expected),"net cached area");assert(expected>=Math.fround(c.minArea||0));
        const total=[r.outer,...r.holes].reduce((s,j)=>moments(loops[j]).map((v,k)=>v+s[k]),[0,0,0]);
        assert.deepEqual(r.center,[bits(total[1]/total[0]),bits(total[2]/total[0])],"cached centroid with holes");
        const outer=loops[r.outer],lo=[0,1].map(k=>Math.min(...outer.map(p=>p[k]))),hi=[0,1].map(k=>Math.max(...outer.map(p=>p[k])));
        assert.deepEqual(r.origin,lo.map(bits));assert.deepEqual(r.size,hi.map((v,k)=>bits(v-lo[k])));
        if(i)assert(number(data.regions[i-1].area)>=number(r.area),"component area order");}
    assert.equal(owned.size,loops.length);assert.equal(usedEdges.size,data.edges.length);assert.equal(usedPoints.size,data.points.length);
    if(!c.minArea){const topology=rasterTopology(c);assert.equal(data.regions.length,topology.components,"independent raster foreground flood fill");assert.equal(data.rings.filter(r=>r.hole).length,topology.holes,"independent dual-background flood fill");
        for(let i=0;i<loops.length;++i)for(let j=0;j<loops.length;++j)if(i!==j)assert.equal(contains(loops[i],loops[j][0]),contains(originals[matches[i]],originals[matches[j]][0]),"nesting preserved");}
    if(!c.simplify){const kept=originals.map((p,i)=>({i,area:Math.abs(area(p))-originals.reduce((s,q,j)=>s+(parents[j].parent===i?Math.abs(area(q)):0),0)})).filter(p=>!parents[p.i].hole&&p.area>=Math.fround(c.minArea||0));
        assert.equal(data.regions.length,kept.length,"independent whole-component min_area");}
    if(c.components!==undefined)assert.equal(data.regions.length,c.components,c.name+" analytical component count");
    if(c.holes!==undefined)assert.equal(data.rings.filter(r=>r.hole).length,c.holes,c.name+" analytical hole count");
    if(c.areas)assert.deepEqual(data.regions.map(r=>number(r.area)),c.areas,c.name+" analytical areas");
    return data;
}
module.exports={bits,number,original,verify,rasterTopology,area,contains};
