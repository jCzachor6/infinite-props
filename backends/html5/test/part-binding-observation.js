"use strict";
// Test-only normalization into the existing independent C++/C observations.
const assert = require('node:assert/strict');
const storage = new DataView(new ArrayBuffer(4));
const bits = value => {storage.setFloat32(0, value, true); return storage.getUint32(0, true);};
const point = p => [bits(p.x), bits(p.y)];
function observe(result) {
    const outputs = result.outputs.map((s, index) => {
        assert.equal(s.index, index);
        const keys = [['x','y'],['x','y','w','h'],['x','y','r'],[]][s.kind];
        assert.equal(s.count, s.kind === 3 ? s.parts.length : s.valueBits.length);
        const parts = s.parts.map(p => {
            assert(p.shapeResource >= 1 && p.shapeResource <= result.resources.length);
            assert(p.textureResource >= 0 && p.textureResource <= result.resources.length);
            return {slot:p.outputIndex, entry:p.elementIndex, id:p.id, source_id:p.sourceId,
                shape:[p.shapeKind,p.shapeResource,p.regionIndex], appearance:[p.appearanceKind,p.textureResource,...p.tint],
                source_mapping:[p.sourceD4,p.sourceOriginX,p.sourceOriginY], texture_mapping:[p.textureD4,p.textureOriginX,p.textureOriginY],
                transform:[p.d4,p.translationX,p.translationY], source_scale:point(p.sourceScale), target_scale:point(p.targetScale),
                anchor:point(p.sourceAnchor), center:point(p.center), bounds_origin:point(p.boundsOrigin), bounds_size:point(p.boundsSize)};
        });
        return {name:s.name,owner:s.owner,kind:s.kind,list:s.isList,values:s.valueBits.map(v=>keys.map(k=>v[k])),parts};
    });
    const resources = result.resources.map((r, index) => {
        assert.equal(r.id, index + 1); assert.equal(r.byteCount, r.data.length);
        if (r.kind === 2) return {kind:'raster',format:r.pixelFormat,size:[r.width,r.height],scale:point(r.scale),stride:r.rowStride,bytes:Array.from(r.data)};
        assert.equal(r.kind, 1);
        return {kind:'geometry',vertex_count:r.vertexCount,edge_count:r.edgeCount,
            rings:r.rings.map((v,i)=>{assert.equal(v.index,i);return {parent:v.parentRing,hole:v.isHole,vertices:v.vertices.map(point)};}),
            regions:r.regions.map((v,i)=>{assert.equal(v.index,i);return {outer:v.outerRing,holes:v.holes,source_id:v.sourceId,area:bits(v.area),
                center:point(v.center),bounds_origin:point(v.boundsOrigin),bounds_size:point(v.boundsSize)};})};
    });
    return {schema:1,size:[result.width,result.height],rgba:Array.from(result.data),outputs,resources};
}
module.exports = {observe};
