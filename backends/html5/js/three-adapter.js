// three-adapter.js — a generated model as three.js objects (preview).
//
//   import * as THREE from "three";
//   import { toObject3D, disposeObject3D } from "./three-adapter.js";
//
//   const textures = new Map();                  // one GPU texture per key, shared by the scene
//   const chair = toObject3D(result, THREE, { textures });
//   scene.add(chair);                            // a Group in metres, +Y up
//   ...
//   scene.remove(chair);
//   disposeObject3D(chair);                      // geometry and materials; `textures` keeps the images
//
// `result` is what model.generate() of api3d.js returns. The conventions of
// a model are those of three.js, so nothing is converted: right-handed, +Y
// up, counterclockwise front faces, tangents with a bitangent sign and
// normal maps with green up the image. Each surface becomes one Mesh with a
// MeshStandardMaterial; textures are DataTextures over the result's own
// arrays, colour in sRGB and normal maps as linear data, uploaded top row
// first so that UV (0, 0) is the upper left of the image.
//
// This file is the only one that knows three.js; it imports nothing, the
// caller hands its own THREE in.
//
// options:
//   textures    a Map shared between calls: surfaces of any model whose
//               texture keys are equal then share one texture
//   shadows     cast and receive shadows (default true)
//   anisotropy  anisotropic filtering of smooth textures (default 4)
//   meters      scale the group to metres (default true); false keeps model units

const NORMAL = 1, CUTOUT = 1;

function dataTexture(THREE, image, width, height, sampling) {
    const texture = new THREE.DataTexture(image, width, height, THREE.RGBAFormat, THREE.UnsignedByteType);
    texture.colorSpace = sampling.color ? THREE.SRGBColorSpace : THREE.NoColorSpace;
    texture.flipY = false;
    texture.wrapS = texture.wrapT = sampling.repeats ? THREE.RepeatWrapping : THREE.ClampToEdgeWrapping;
    // Crisp: every texel a hard square, as pixel art wants it.
    texture.magFilter = sampling.crisp ? THREE.NearestFilter : THREE.LinearFilter;
    texture.minFilter = sampling.crisp ? THREE.NearestFilter : THREE.LinearMipmapLinearFilter;
    texture.generateMipmaps = !sampling.crisp;
    if (!sampling.crisp) texture.anisotropy = sampling.anisotropy;
    texture.needsUpdate = true;
    return texture;
}

// The colour and normal textures of one result texture for one sampling.
// Filters live on a three.js texture, so a crisp and a smooth use of the
// same image are two entries.
function texturesOf(THREE, source, crisp, options, cache) {
    const key = source.key + (crisp ? ":crisp" : ":smooth") + (source.repeats ? ":repeat" : ":clamp");
    let entry = cache.get(key);
    if (!entry) {
        const sampling = { repeats: source.repeats, crisp, anisotropy: options.anisotropy === undefined ? 4 : options.anisotropy };
        const normal = source.normal || source.images.find(image => image.kind === NORMAL) || null;
        entry = {
            map: dataTexture(THREE, source.data, source.width, source.height, { ...sampling, color: true }),
            normalMap: normal ? dataTexture(THREE, normal.data, normal.width, normal.height, { ...sampling, color: false }) : null,
        };
        entry.map.name = source.name;
        if (entry.normalMap) entry.normalMap.name = source.name + " (normal)";
        cache.set(key, entry);
    }
    return entry;
}

export function toObject3D(result, THREE, options = {}) {
    const cache = options.textures || new Map();
    const shadows = options.shadows !== false;
    const group = new THREE.Group();
    for (const surface of result.surfaces) {
        const source = result.textures[surface.texture];
        const { map, normalMap } = texturesOf(THREE, source, surface.crisp, options, cache);

        const geometry = new THREE.BufferGeometry();
        geometry.setAttribute("position", new THREE.BufferAttribute(surface.positions, 3));
        geometry.setAttribute("normal", new THREE.BufferAttribute(surface.normals, 3));
        geometry.setAttribute("tangent", new THREE.BufferAttribute(surface.tangents, 4));
        geometry.setAttribute("uv", new THREE.BufferAttribute(surface.uvs, 2));
        geometry.setIndex(new THREE.BufferAttribute(surface.indices, 1));
        geometry.computeBoundingSphere();
        geometry.computeBoundingBox();

        const material = new THREE.MeshStandardMaterial({
            name: source.name,
            map,
            normalMap,
            roughness: surface.roughness,
            metalness: surface.metallic,
            side: surface.twoSided ? THREE.DoubleSide : THREE.FrontSide,
        });
        // Opaque surfaces ignore the image's alpha; cutout ones test it.
        if (surface.alphaMode === CUTOUT) material.alphaTest = surface.alphaCutoff;
        if (surface.emission > 0) {
            material.emissive = new THREE.Color(1, 1, 1);
            material.emissiveMap = map;
            material.emissiveIntensity = surface.emission;
        }

        const mesh = new THREE.Mesh(geometry, material);
        mesh.name = source.name;
        mesh.castShadow = mesh.receiveShadow = shadows;
        group.add(mesh);
    }
    if (options.meters !== false) group.scale.setScalar(1 / result.unitsPerMeter);
    // What a scene may want to know without keeping the result.
    group.userData.iprop = {
        bounds: { min: Array.from(result.bounds.min), max: Array.from(result.bounds.max) },
        size: Array.from(result.size),
        unitsPerMeter: result.unitsPerMeter,
        vertexCount: result.vertexCount,
        triangleCount: result.triangleCount,
    };
    // Textures made for this object alone are its to dispose.
    if (!options.textures) group.userData.ipropTextures = cache;
    return group;
}

// Releases the GPU resources of an object made by toObject3D. Textures in a
// shared cache stay; clear them with disposeTextures when the scene goes.
export function disposeObject3D(object) {
    object.traverse(node => {
        if (!node.isMesh) return;
        node.geometry.dispose();
        node.material.dispose();
    });
    if (object.userData.ipropTextures) disposeTextures(object.userData.ipropTextures);
}

export function disposeTextures(cache) {
    for (const entry of cache.values()) {
        entry.map.dispose();
        if (entry.normalMap) entry.normalMap.dispose();
    }
    cache.clear();
}
