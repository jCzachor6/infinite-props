"use strict";
// Independent, deliberately naive pixel/point oracle. No runtime geometry,
// scanline lists, D4 implementation, compositor or native output is imported.
const matrices=[[1,0,0,1],[0,1,-1,0],[-1,0,0,-1],[0,-1,1,0],[-1,0,0,1],[1,0,0,-1],[0,1,1,0],[0,-1,-1,0]];
const f=Math.fround,buf=new ArrayBuffer(8),view=new DataView(buf);
function bits(n){view.setFloat32(0,n,true);return view.getUint32(0,true);}
function doubleBits(n){view.setFloat64(0,n,true);return view.getBigUint64(0,true).toString(16).padStart(16,'0');}
const linear=(k,p)=>{const m=matrices[k];return[m[0]*p[0]+m[1]*p[1],m[2]*p[0]+m[3]*p[1]];};
function kind(rot,mx,my){const m=[...matrices[rot/90]];if(mx){m[0]*=-1;m[1]*=-1;}if(my){m[2]*=-1;m[3]*=-1;}return matrices.findIndex(n=>n.every((v,i)=>v===m[i]));}
function compose(a,b){const x=linear(b,linear(a,[1,0])),y=linear(b,linear(a,[0,1]));return matrices.findIndex(m=>m.every((v,i)=>v===[x[0],y[0],x[1],y[1]][i]));}
const extent=(k,p)=>Math.abs(matrices[k][1])===1?[p[1],p[0]]:[...p];
function mappedPixel(k,p,size){const a=linear(k,p),end=linear(k,size.map(n=>n-1));return a.map((v,i)=>v-Math.min(0,end[i]));}
function mappedOrigin(k,origin,size,t){const a=linear(k,origin),end=linear(k,size);return a.map((v,i)=>v+Math.min(0,end[i])+t[i]);}
const multiply=(a,b)=>Math.floor((a*b+127)/255);
function composite(d,s,sourceOver){
    if(!sourceOver){if(s[3]===255)return [...s];return[...s.slice(0,3).map((v,i)=>Math.floor((v*s[3]+d[i]*(255-s[3]))/255)),d[3]];}
    const dw=d[3]*(255-s[3]),den=s[3]*255+dw;if(!den)return[0,0,0,0];
    return[...s.slice(0,3).map((v,i)=>Math.floor((v*s[3]*255+d[i]*dw+Math.floor(den/2))/den)),Math.floor((den+127)/255)];
}
function sourcePixel(x,y){const at=y*5+x,a=at===1?1:at===8?127:at===11?128:at===12?254:at===19?255:0;
    return[(17+x*43+y*11)%256,(29+x*13+y*37)%256,(53+x*31+y*17)%256,a];}
const texturePixel=(x,y)=>[(11+x*23)%256,(19+y*29)%256,(31+x*17+y*13)%256,(x*37+y*61)%256];
function textureAt(p){for(let y=0;y<6;y++)for(let x=0;x<7;x++){const q=mappedPixel(6,[x,y],[7,6]);if(q[0]-4===p[0]&&q[1]-2===p[1])return texturePixel(x,y);}return[0,0,0,0];}
function inside(ring,p){let odd=false;for(let i=0,j=ring.length-1;i<ring.length;j=i++){
    const a=ring[i],b=ring[j];if((a[1]>p[1])!==(b[1]>p[1])&&p[0]<(b[0]-a[0])*(p[1]-a[1])/(b[1]-a[1])+a[0])odd=!odd;}return odd;}
const geometry=[{rings:[[[0,0],[10,0],[10,8],[0,8]],[[2,2],[2,4],[4,4],[4,2]]],center:[f(97/19),f(77/19)],origin:[0,0],size:[10,8],sourceId:9},
    {rings:[[[-3.5,-1.25],[1.25,-.5],[-.75,2.75]]],center:[-1,f(1/3)],origin:[-3.5,-1.25],size:[4.75,4],sourceId:73}];
function defaults(c){return{kind:3,rot:0,mx:0,my:0,at:[7.25,6.5],appearance:0,scale:[1,1],sourceD4:0,origin:[-3,2],clip:0,mode:1,background:0,maskMode:0,place:1,...c};}
function reference(input){
    const c=defaults(input),rawScale=c.scale.map(f),sourceScale=c.kind<2?rawScale:extent(c.sourceD4,rawScale);
    const placed=c.place!==0&&c.place!==4,B=placed?kind(c.rot,c.mx,c.my):0;
    const targetScale=c.place===3?sourceScale:c.place===4?extent(1,sourceScale):extent(B,sourceScale);
    const rasterScale=c.kind<2?extent(B,targetScale):sourceScale;
    let base,frame,workingFrame,originalCenter,originalOrigin,originalSize,sourceId=-1;
    if(c.kind<2){const g=geometry[c.kind],points=g.rings[0];
        const lo=[0,1].map(i=>Math.min(...points.map(p=>p[i]))),hi=[0,1].map(i=>Math.max(...points.map(p=>p[i])));
        const a=lo.map((v,i)=>v*sourceScale[i]),b=hi.map((v,i)=>v*sourceScale[i]);
        frame=[...g.center.map((v,i)=>v*sourceScale[i]),a[0],a[1],b[0],b[1]];
        const rlo=lo.map((v,i)=>v*rasterScale[i]),rhi=hi.map((v,i)=>v*rasterScale[i]);
        workingFrame=placed?[...g.center.map((v,i)=>v*rasterScale[i]),...rlo,...rhi]:frame;
        originalCenter=g.center;originalOrigin=g.origin;originalSize=g.size;sourceId=g.sourceId;
        const origin=rlo.map(Math.floor),end=rhi.map(Math.ceil),size=end.map((n,i)=>Math.max(1,n-origin[i])),pixels=[];
        for(let y=0;y<size[1];y++)for(let x=0;x<size[0];x++){let n=0;
            for(let sy=0;sy<4;sy++)for(let sx=0;sx<4;sx++){
                const p=[(origin[0]+x+(sx+.5)/4)/rasterScale[0],(origin[1]+y+(sy+.5)/4)/rasterScale[1]];
                if(inside(g.rings[0],p)&&!g.rings.slice(1).some(r=>inside(r,p)))n++;
            }pixels.push(Math.floor((n*255+8)/16));}
        base={size,origin,pixels,d4:0,scale:rasterScale};
    }else{
        const positive=[];for(let y=0;y<4;y++)for(let x=0;x<5;x++)if(sourcePixel(x,y)[3]){
            const q=mappedPixel(c.sourceD4,[x,y],[5,4]);positive.push(q.map((v,i)=>v+c.origin[i]));}
        const lo=[0,1].map(i=>Math.min(...positive.map(p=>p[i]))),hi=[0,1].map(i=>Math.max(...positive.map(p=>p[i]))+1);
        frame=[(lo[0]+hi[0])/2,(lo[1]+hi[1])/2,...lo,...hi];
        workingFrame=frame;
        originalOrigin=lo.map((v,i)=>f(v/sourceScale[i]));originalSize=hi.map((v,i)=>f((v-lo[i])/sourceScale[i]));
        originalCenter=frame.slice(0,2).map((v,i)=>f(v/sourceScale[i]));
        base={size:[5,4],origin:c.origin,pixels:Array.from({length:20},(_,i)=>sourcePixel(i%5,Math.floor(i/5))[3]),d4:c.sourceD4,scale:rawScale};
    }
    const anchor=linear(B,workingFrame.slice(0,2)),t=placed?c.at.map((v,i)=>Math.floor(f(v)*targetScale[i]-anchor[i]+.5)):[0,0];
    const a=linear(B,workingFrame.slice(2,4)),b=linear(B,workingFrame.slice(4,6));
    const appearance=c.appearance===0?'source':c.appearance===2||c.appearance===5?'texture':'fill';
    const common={id:0,sourceId,originalFrame:frame.map(doubleBits),sourceScale:sourceScale.map(bits)};
    const original={...common,center:originalCenter.map(bits),origin:originalOrigin.map(bits),size:originalSize.map(bits),d4:0,translation:[0,0],appearance:'source'};
    const result={...common,originalFrame:workingFrame.map(doubleBits),sourceScale:(c.kind<2&&placed?rasterScale:sourceScale).map(bits),
        center:(placed?anchor.map((v,i)=>f((v+t[i])/targetScale[i])):originalCenter).map(bits),
        origin:(placed?a.map((v,i)=>f((Math.min(v,b[i])+t[i])/targetScale[i])):originalOrigin).map(bits),
        size:(placed?a.map((v,i)=>f(Math.abs(v-b[i])/targetScale[i])):originalSize).map(bits),d4:B,translation:t,appearance};
    const originalPixel=(x,y)=>mappedPixel(base.d4,[x,y],base.size).map((v,i)=>v+base.origin[i]);
    const originalAlpha=p=>{for(let y=0;y<base.size[1];y++)for(let x=0;x<base.size[0];x++){
        const q=originalPixel(x,y);if(q[0]===p[0]&&q[1]===p[1])return base.pixels[y*base.size[0]+x];}return 0;};
    let mask=null;
    if(c.maskMode!==2){let source=base;
        if(c.kind>=3){const origin=frame.slice(2,4),size=frame.slice(4,6).map((v,i)=>v-origin[i]),pixels=[];
            for(let y=0;y<size[1];y++)for(let x=0;x<size[0];x++)pixels.push(originalAlpha([origin[0]+x,origin[1]+y]));
            source={origin,size,pixels,d4:0,scale:sourceScale};}
        mask={format:'A8',size:source.size,origin:mappedOrigin(B,source.origin,extent(source.d4,source.size),t),d4:compose(source.d4,B),scale:source.scale.map(bits),pixels:source.pixels};
    }
    const background=c.background?[201,4,172,c.background===1?128:0]:[0,0,0,0],canvas=Array.from({length:1024},()=>[...background]);
    if(c.maskMode!==1)for(let y=0;y<base.size[1];y++)for(let x=0;x<base.size[0];x++){
        const coverage=base.pixels[y*base.size[0]+x],p=originalPixel(x,y);
        const a=linear(B,p),b=linear(B,p.map(v=>v+1)),dest=a.map((v,i)=>Math.min(v,b[i])+t[i]);
        const [dx,dy]=dest;if(dx<0||dy<0||dx>=32||dy>=32)continue;
        let color;
        if(c.appearance===0){if(c.kind!==3)throw Error('oracle cannot draw absent appearance');color=sourcePixel(x,y);
            if(!color[3]&&!c.mode)continue;
        }else{if(!coverage)continue;
            color=c.appearance===2||c.appearance===5?textureAt(p):c.appearance===3?[7,11,13,0]:c.appearance===4?[41,97,153,64]:c.appearance===6?[13,29,47,255]:[17,89,203,128];
            color=[...color.slice(0,3),multiply(color[3],coverage)];
        }
        if(c.clip){const coverage=c.clip===2?0:c.clip===1?128:(dx*19+dy*31)%256;if(!coverage)continue;color=[...color.slice(0,3),multiply(color[3],coverage)];}
        const index=dy*32+dx;canvas[index]=composite(canvas[index],color,c.mode);
    }
    return {original,view:result,mask,canvas:canvas.flat(),creations:1,released:0};
}
module.exports={bits,doubleBits,defaults,reference,kind,compose,linear,extent,mappedPixel,multiply,composite,sourcePixel};
