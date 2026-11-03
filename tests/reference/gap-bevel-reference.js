"use strict";
// Deliberately naive reference: one membership query and a complete boundary
// search per subpixel. It does not use the runtime's scanlines or active lists.
const assert=require('node:assert/strict'),geometry=require('./regions-reference.js');
const {bits,value,rings,containsRing}=geometry;
const color=n=>n||[255,255,255,255],f32=n=>Math.fround(n);
function bevel(d,frame,options={},check=true){
    const loops=rings(d),scale=frame.scale.map(value),width=f32(options.width??2),strength=f32(options.strength??0.5);
    const hi=color(options.highlight),lo=options.shadow||[0,0,0,255],light=(options.light||[-1,-1]).map(f32);
    const length=Math.sqrt(light[0]*light[0]+light[1]*light[1]),unit=light.map(v=>v/length);
    const offsets=[];let offset=0;for(const r of loops){offsets.push(offset);offset+=r.length;}
    const edges=d.regions.map(r=>[r.outer,...r.holes].flatMap(index=>loops[index].map((a,k)=>{
        const b=loops[index][(k+1)%loops[index].length],first=d.rings[index].vertices[k],second=d.rings[index].vertices[(k+1)%loops[index].length];
        const at=d.edges.findIndex(e=>e[0]===Math.min(first,second)&&e[1]===Math.max(first,second));assert(at>=0);
        const dx=b[0]-a[0],dy=b[1]-a[1],length2=dx*dx+dy*dy,l=Math.sqrt(length2);
        return{a,b,dx,dy,length2,normal:[dy/l,-dx/l],tie:[d.origin===2?d.edges[at][2]:at,offsets[index]+k]};
    })));
    const quantize=v=>Math.max(0,Math.min(255,Math.floor(v+0.5))),pixels=[];
    for(let y=0;y<frame.size[1];y++)for(let x=0;x<frame.size[0];x++){
        let alpha=0,red=0,green=0,blue=0;
        for(let sy=0;sy<4;sy++)for(let sx=0;sx<4;sx++){
            const p=[(frame.origin[0]+x+(sx+.5)/4)/scale[0],(frame.origin[1]+y+(sy+.5)/4)/scale[1]];
            const owner=d.regions.findIndex(r=>containsRing(loops[r.outer],p)&&r.holes.every(h=>!containsRing(loops[h],p)));if(owner<0)continue;
            let nearest=width*width,winner=null;
            for(const e of edges[owner]){
                const t=Math.max(0,Math.min(1,((p[0]-e.a[0])*e.dx+(p[1]-e.a[1])*e.dy)/e.length2));
                const dx=p[0]-e.a[0]-e.dx*t,dy=p[1]-e.a[1]-e.dy*t,dist=dx*dx+dy*dy;
                if(dist<nearest||(dist===nearest&&(!winner||e.tie[0]<winner.tie[0]||(e.tie[0]===winner.tie[0]&&e.tie[1]<winner.tie[1])))){nearest=dist;winner=e;}
            }
            if(!winner)continue;
            const dot=Math.max(-1,Math.min(1,winner.normal[0]*unit[0]+winner.normal[1]*unit[1]));
            const chosen=dot>=0?hi:lo,fade=Math.max(0,Math.min(1,1-Math.sqrt(nearest)/width));
            const a=chosen[3]*strength*Math.abs(dot)*fade;alpha+=a;red+=a*chosen[0];green+=a*chosen[1];blue+=a*chosen[2];
        }
        const a=quantize(alpha/16);pixels.push(...(a?[quantize(red/alpha),quantize(green/alpha),quantize(blue/alpha),a]:[0,0,0,0]));
    }
    if(check)assert.deepEqual(frame.pixels,pixels,'independent sixteen-sample bevel including normals, corners, holes and alpha');return pixels;
}
// BigInt arithmetic gives an independent exact u32 hash oracle.
const mask=0xffffffffn;
function mix(value){let x=BigInt(value)&mask;x^=x>>16n;x=(x*0x7feb352dn)&mask;x^=x>>15n;x=(x*0x846ca68bn)&mask;return Number((x^(x>>16n))&mask);}
function profile(root,edge,width,variation,seed){
    const e=root.edges[edge];let first=e[0],last=e[1];const p=root.points.map(v=>v.map(value));
    if(p[last][1]<p[first][1]||(p[last][1]===p[first][1]&&p[last][0]<p[first][0]))[first,last]=[last,first];
    const radii=Array.from({length:9},(_,j)=>{const h=j===0||j===8?
        mix(BigInt(seed>>>0)^0x9e3779b9n^BigInt(mix(BigInt(j===0?first:last)+0x68bc21ebn))):
        mix(BigInt(seed>>>0)^0x85ebca6bn^BigInt(mix(BigInt(edge)+0x27d4eb2dn))^(BigInt(j)*0x9e3779b9n));
        return f32(f32(width)*.5)*((1-f32(variation))+(2*f32(variation))*((h>>>8)/16777215));});
    return{first:p[first],last:p[last],radii};
}
function argumentsOf(c,budget=1e9,occupied=0){const [kind,input]=geometry.argumentsOf(c);
    const args=[kind,input,bits(c.width??0),bits(c.variation??0),(c.seed??7)>>>0,c.repeat??1,...(c.scale||[1,1]).map(bits),budget,occupied];
    if(c.bevel){const b=c.bevel,pack=p=>(p[0]+p[1]*256+p[2]*65536+p[3]*16777216)>>>0;
        args.push(bits(b.width??2),...(b.light||[-1,-1]).map(bits),pack(b.highlight||[255,255,255,255]),pack(b.shadow||[0,0,0,255]),bits(b.strength??.5));}
    return args.map(String);
}
function check(c,record){
    geometry.validate(record.source);geometry.validate(record.result,{canonical:!!c.width});geometry.raster(record.result,record.mask);
    assert.equal(record.released,0);if(record.layer)bevel(record.result,record.layer,c.bevel);
    if(!c.width)assert(record.shared,'zero-width gap shares source');
}
module.exports={...geometry,bevel,mix,profile,argumentsOf,check};
