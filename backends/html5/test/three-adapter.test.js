"use strict";
// The three.js adapter on a synthetic result, without WebGL: what it builds
// from surfaces and textures, what it shares through a texture cache and
// what it releases. three.js is the website's dependency; where it is not
// installed these tests are skipped.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { pathToFileURL } = require("node:url");
const test = require("node:test");

const threePath = path.resolve(__dirname, "../../../website/node_modules/three/build/three.module.js");
const available = fs.existsSync(threePath);
// The adapter is a browser module in a directory of classic scripts; a data
// URL imports it as a module without asking Node to guess its kind.
const load = async () => ({
    THREE: await import(pathToFileURL(threePath).href),
    adapter: await import("data:text/javascript," +
        encodeURIComponent(fs.readFileSync(path.resolve(__dirname, "../js/three-adapter.js"), "utf8"))),
});

function image(width, height, fill) {
    return new Uint8Array(width * height * 4).fill(fill);
}

// Two textures and four surfaces: every material field takes two values.
function result() {
    const quad = offset => ({
        positions: new Float32Array([0, 0, offset, 1, 0, offset, 1, 1, offset, 0, 1, offset]),
        normals: new Float32Array([0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1]),
        tangents: new Float32Array([1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1]),
        uvs: new Float32Array([0, 1, 1, 1, 1, 0, 0, 0]),
        indices: new Uint32Array([0, 1, 2, 0, 2, 3]),
        vertexCount: 4, indexCount: 6,
    });
    const normal = { index: 0, kind: 1, source: 1, name: "relief", width: 4, height: 2, data: image(4, 2, 128) };
    return {
        bounds: { min: [0, 0, 0], max: [1, 1, 3] }, size: [2, 2, 4], unitsPerMeter: 50, vertexCount: 16, triangleCount: 8,
        textures: [
            { index: 0, key: "00000000000000aa", name: "wood", width: 4, height: 2, assetWidth: 4, assetHeight: 2, repeats: true,
              data: image(4, 2, 200), images: [normal], normal },
            { index: 1, key: "00000000000000bb", name: "label", width: 2, height: 2, assetWidth: 2, assetHeight: 2, repeats: false,
              data: image(2, 2, 90), images: [], normal: null },
        ],
        surfaces: [
            { index: 0, texture: 0, roughness: 0.8, metallic: 0, emission: 0, alphaMode: 0, alphaCutoff: 0.5, twoSided: false, crisp: false, ...quad(0) },
            { index: 1, texture: 0, roughness: 0.25, metallic: 0.75, emission: 0, alphaMode: 0, alphaCutoff: 0.5, twoSided: false, crisp: true, ...quad(1) },
            { index: 2, texture: 1, roughness: 0.8, metallic: 0, emission: 0, alphaMode: 1, alphaCutoff: 0.35, twoSided: true, crisp: false, ...quad(2) },
            { index: 3, texture: 1, roughness: 0.8, metallic: 0, emission: 1.5, alphaMode: 0, alphaCutoff: 0.5, twoSided: false, crisp: false, ...quad(3) },
        ],
    };
}

test("a result becomes one mesh per surface, in metres", { skip: !available && "three.js is not installed" }, async () => {
    const { THREE, adapter } = await load();
    const source = result();
    const group = adapter.toObject3D(source, THREE);
    assert.equal(group.children.length, 4);
    assert.equal(group.scale.x, 1 / 50);
    assert.deepEqual(group.userData.iprop, { bounds: { min: [0, 0, 0], max: [1, 1, 3] }, size: [2, 2, 4], unitsPerMeter: 50,
        vertexCount: 16, triangleCount: 8 });
    assert.equal(adapter.toObject3D(source, THREE, { meters: false }).scale.x, 1);

    const [plain, crisp, leaf, lamp] = group.children;
    // The arrays of the result are used as they are, not copied.
    const geometry = plain.geometry;
    assert.equal(geometry.getAttribute("position").array, source.surfaces[0].positions);
    assert.equal(geometry.getAttribute("position").itemSize, 3);
    assert.equal(geometry.getAttribute("normal").itemSize, 3);
    assert.equal(geometry.getAttribute("tangent").itemSize, 4);
    assert.equal(geometry.getAttribute("uv").itemSize, 2);
    assert.equal(geometry.getIndex().array, source.surfaces[0].indices);
    assert.equal(geometry.getIndex().count, 6);
    assert(geometry.boundingSphere && geometry.boundingBox);
    assert(plain.castShadow && plain.receiveShadow);
    assert.equal(adapter.toObject3D(source, THREE, { shadows: false }).children[0].castShadow, false);

    // Material numbers.
    assert(plain.material.isMeshStandardMaterial);
    assert.equal(plain.material.roughness, 0.8);
    assert.equal(plain.material.metalness, 0);
    assert.equal(plain.material.side, THREE.FrontSide);
    assert.equal(plain.material.alphaTest, 0);
    assert.equal(plain.material.emissiveIntensity, 1);
    assert.equal(plain.material.emissiveMap, null);
    assert.equal(crisp.material.roughness, 0.25);
    assert.equal(crisp.material.metalness, 0.75);
    assert.equal(leaf.material.alphaTest, 0.35);
    assert.equal(leaf.material.side, THREE.DoubleSide);
    assert.equal(lamp.material.emissiveIntensity, 1.5);
    assert.equal(lamp.material.emissiveMap, lamp.material.map);
    assert.equal(lamp.material.emissive.getHex(), 0xffffff);

    // Textures: colour in sRGB, the normal map as data, the top row first.
    const map = plain.material.map, normalMap = plain.material.normalMap;
    assert(map.isDataTexture && normalMap.isDataTexture);
    assert.equal(map.image.data, source.textures[0].data);
    assert.equal(map.image.width, 4);
    assert.equal(map.image.height, 2);
    assert.equal(map.colorSpace, THREE.SRGBColorSpace);
    assert.equal(normalMap.colorSpace, THREE.NoColorSpace);
    assert.equal(normalMap.image.data, source.textures[0].normal.data);
    assert.equal(map.flipY, false);
    assert.equal(map.wrapS, THREE.RepeatWrapping);
    assert.equal(map.wrapT, THREE.RepeatWrapping);
    assert.equal(map.minFilter, THREE.LinearMipmapLinearFilter);
    assert.equal(map.magFilter, THREE.LinearFilter);
    assert.equal(map.generateMipmaps, true);
    assert.equal(map.anisotropy, 4);
    assert.equal(leaf.material.map.wrapS, THREE.ClampToEdgeWrapping);
    assert.equal(leaf.material.normalMap, null);
    // A crisp use of the same image is a texture of its own, unfiltered.
    assert.notEqual(crisp.material.map, map);
    assert.equal(crisp.material.map.image.data, source.textures[0].data);
    assert.equal(crisp.material.map.minFilter, THREE.NearestFilter);
    assert.equal(crisp.material.map.magFilter, THREE.NearestFilter);
    assert.equal(crisp.material.map.generateMipmaps, false);
    // Surfaces of one texture and sampling share it.
    assert.equal(lamp.material.map, leaf.material.map);
});

test("a shared cache keeps one texture per key; disposal releases what an object owns", { skip: !available && "three.js is not installed" }, async () => {
    const { THREE, adapter } = await load();
    const textures = new Map();
    const first = adapter.toObject3D(result(), THREE, { textures, anisotropy: 8 });
    const second = adapter.toObject3D(result(), THREE, { textures });
    // Another result with the same keys: no new textures.
    assert.equal(textures.size, 3);
    assert.equal(second.children[0].material.map, first.children[0].material.map);
    assert.equal(second.children[2].material.map, first.children[2].material.map);
    assert.equal(first.children[0].material.map.anisotropy, 8);
    assert.notEqual(second.children[0].geometry, first.children[0].geometry);

    let disposed = { geometry: 0, material: 0, texture: 0 };
    const watch = object => object.traverse(node => {
        if (!node.isMesh) return;
        node.geometry.addEventListener("dispose", () => ++disposed.geometry);
        node.material.addEventListener("dispose", () => ++disposed.material);
    });
    for (const entry of textures.values())
        for (const texture of [entry.map, entry.normalMap].filter(Boolean)) texture.addEventListener("dispose", () => ++disposed.texture);
    watch(first);
    adapter.disposeObject3D(first);
    assert.deepEqual(disposed, { geometry: 4, material: 4, texture: 0 }, "a shared cache outlives the object");
    adapter.disposeTextures(textures);
    assert.equal(disposed.texture, 5); // three colour maps and two normal maps
    assert.equal(textures.size, 0);

    // Without a cache the object owns its textures.
    disposed = { geometry: 0, material: 0, texture: 0 };
    const own = adapter.toObject3D(result(), THREE);
    for (const entry of own.userData.ipropTextures.values())
        for (const texture of [entry.map, entry.normalMap].filter(Boolean)) texture.addEventListener("dispose", () => ++disposed.texture);
    watch(own);
    adapter.disposeObject3D(own);
    assert.deepEqual(disposed, { geometry: 4, material: 4, texture: 5 });
});

test("material maps bind as three.js maps and a blended surface is transparent", { skip: !available && "three.js is not installed" }, async () => {
    const { THREE, adapter } = await load();
    const source = result();
    const map = (kind, name, fill) => ({ index: kind - 1, kind, source: 1, name, width: 4, height: 2, data: image(4, 2, fill) });
    const maps = [map(1, "relief", 128), map(2, "gloss", 64), map(3, "metal", 255), map(4, "shade", 200), map(5, "glow", 90)];
    source.textures[0] = { ...source.textures[0], images: maps, normal: maps[0] };
    source.surfaces[0] = { ...source.surfaces[0], roughness: 1, metallic: 1, emission: 1 };
    source.surfaces[2] = { ...source.surfaces[2], alphaMode: 2 };
    const textures = new Map();
    const [plain, crisp, glass] = adapter.toObject3D(source, THREE, { textures }).children;
    const material = plain.material;
    // Linear data for the scalar maps; the emission map is a colour.
    for (const [field, kind] of [["normalMap", 1], ["roughnessMap", 2], ["metalnessMap", 3], ["aoMap", 4]]) {
        assert(material[field].isDataTexture, field);
        assert.equal(material[field].image.data, maps[kind - 1].data);
        assert.equal(material[field].colorSpace, THREE.NoColorSpace);
        assert.equal(material[field].wrapS, THREE.RepeatWrapping);
    }
    assert.equal(material.emissiveMap.image.data, maps[4].data);
    assert.equal(material.emissiveMap.colorSpace, THREE.SRGBColorSpace);
    assert.equal(material.emissiveIntensity, 1);
    assert.equal(material.roughness, 1);
    assert.equal(material.metalness, 1);
    // The crisp use has unfiltered copies of every map.
    assert.notEqual(crisp.material.roughnessMap, material.roughnessMap);
    assert.equal(crisp.material.roughnessMap.minFilter, THREE.NearestFilter);
    // Glass: transparent, no depth writes, its alpha kept; an opaque surface is neither.
    assert.equal(glass.material.transparent, true);
    assert.equal(glass.material.depthWrite, false);
    assert.equal(glass.material.alphaTest, 0);
    assert.equal(material.transparent, false);
    assert.equal(material.depthWrite, true);
    // Every map of the cache is released.
    let released = 0;
    for (const entry of textures.values())
        for (const texture of Object.values(entry).filter(Boolean)) texture.addEventListener("dispose", () => ++released);
    adapter.disposeTextures(textures);
    assert.equal(released, 2 * 6 + 1); // two samplings of a texture with five maps, one plain colour map
});

test("levels of detail become one THREE.LOD that shares its textures", { skip: !available && "three.js is not installed" }, async () => {
    const { THREE, adapter } = await load();
    const textures = new Map();
    // The coarser level arrives without pixels: the cache holds its textures.
    const near = result(), far = result();
    far.surfaces.length = 2;
    for (const texture of far.textures) { texture.reused = true; texture.data = new Uint8Array(0); texture.images = []; texture.normal = null; }
    const lod = adapter.toLOD([near, far], THREE, [0, 25], { textures });
    assert.ok(lod.isLOD);
    assert.deepEqual(lod.levels.map(level => level.distance), [0, 25]);
    assert.equal(lod.levels[0].object.children.length, 4);
    assert.equal(lod.levels[1].object.children.length, 2);
    assert.equal(lod.levels[1].object.children[0].material.map, lod.levels[0].object.children[0].material.map);
    assert.equal(lod.userData.ipropTextures, undefined, "a shared cache stays the caller's");
    adapter.disposeObject3D(lod);
    assert.ok(textures.size > 0);
    const own = adapter.toLOD([result()], THREE, [0]);
    assert.ok(own.userData.ipropTextures.size > 0);
    assert.throws(() => adapter.toLOD([near, far], THREE, [0]), /one distance for each result/);
    assert.throws(() => adapter.toLOD([near, far], THREE, [5, 25]), /start at 0 and rise/);
    assert.throws(() => adapter.toLOD([near, far], THREE, [0, 0]), /start at 0 and rise/);
});

test("the transfer list of a result names each buffer once", () => {
    const { transferables } = require("../js/api3d.js")(null);
    const source = result();
    // Two surfaces may share one buffer; it is still listed once.
    source.surfaces[1].normals = new Float32Array(source.surfaces[0].normals.buffer);
    const buffers = transferables(source);
    assert.equal(new Set(buffers).size, buffers.length);
    assert.equal(buffers.length, 4 * 5 - 1 + 2 + 1);
    assert(buffers.every(buffer => buffer instanceof ArrayBuffer));
});

test("outputs become named points and boxes turned by their axes", { skip: !available && "three.js is not installed" }, async () => {
    const { THREE, adapter } = await load();
    const source = result();
    // A yaw of 30 degrees: a rotation that is not its own transpose, so a
    // basis built from rows instead of columns would turn the other way.
    const c = Math.cos(Math.PI / 6), s = Math.sin(Math.PI / 6);
    source.outputs = [
        { index: 0, name: "bulbs", kind: "point3d", list: true, count: 2, values: new Float32Array([1, 2, 3, -1, 2, 3]) },
        { index: 1, name: "collider", kind: "box3d", list: false, count: 1,
          values: new Float32Array([4, 5, 6, 2, 3, 4, c, 0, -s, 0, 1, 0, s, 0, c]) },
    ];
    const group = adapter.outputsToObject3D(source, THREE);
    assert.equal(group.scale.x, 1 / 50);
    assert.equal(adapter.outputsToObject3D(source, THREE, { meters: false }).scale.x, 1);
    assert.deepEqual(group.children.map(child => child.name), ["bulbs[0]", "bulbs[1]", "collider"]);
    const [first, second, box] = group.children;
    assert.deepEqual(first.position.toArray(), [1, 2, 3]);
    assert.deepEqual(second.position.toArray(), [-1, 2, 3]);
    assert(box.isLineSegments);
    assert.deepEqual(box.position.toArray(), [4, 5, 6]);
    const close = (vector, expected) => vector.toArray().forEach((value, i) => assert(Math.abs(value - expected[i]) < 1e-6,
        vector.toArray() + " against " + expected));
    close(new THREE.Vector3(1, 0, 0).applyQuaternion(box.quaternion), [c, 0, -s]);
    close(new THREE.Vector3(0, 0, 1).applyQuaternion(box.quaternion), [s, 0, c]);
    box.geometry.computeBoundingBox();
    close(box.geometry.boundingBox.getSize(new THREE.Vector3()), [2, 3, 4]);
    let released = 0;
    group.traverse(node => { if (node.isLineSegments) node.geometry.addEventListener("dispose", () => ++released); });
    adapter.disposeObject3D(group);
    assert.equal(released, 1);
    assert.equal(adapter.outputsToObject3D({ ...source, outputs: [] }, THREE).children.length, 0);
});
