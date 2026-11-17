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
// arrays, colour and emission in sRGB, normal, roughness, metallic and
// occlusion maps as linear data, uploaded top row first so that UV (0, 0) is
// the upper left of the image. A surface's numbers multiply its maps, as
// three.js does; a blended surface is transparent and writes no depth.
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

const NORMAL = 1, ROUGHNESS = 2, METALLIC = 3, AO = 4, EMISSION = 5, CUTOUT = 1, BLEND = 2;

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

// The colour, normal and material map textures of one result texture for
// one sampling. Filters live on a three.js texture, so a crisp and a smooth
// use of the same image are two entries.
function texturesOf(THREE, source, crisp, options, cache) {
    const key = source.key + (crisp ? ":crisp" : ":smooth") + (source.repeats ? ":repeat" : ":clamp");
    let entry = cache.get(key);
    if (!entry) {
        const sampling = { repeats: source.repeats, crisp, anisotropy: options.anisotropy === undefined ? 4 : options.anisotropy };
        const image = kind => source.images.find(candidate => candidate.kind === kind) || null;
        const extra = (kind, color, label) => {
            const found = image(kind);
            if (!found) return null;
            const texture = dataTexture(THREE, found.data, found.width, found.height, { ...sampling, color });
            texture.name = source.name + " (" + label + ")";
            return texture;
        };
        entry = {
            map: dataTexture(THREE, source.data, source.width, source.height, { ...sampling, color: true }),
            normalMap: extra(NORMAL, false, "normal"),
            // A scalar map holds its value in red, green and blue alike.
            roughnessMap: extra(ROUGHNESS, false, "roughness"),
            metalnessMap: extra(METALLIC, false, "metallic"),
            aoMap: extra(AO, false, "ao"),
            emissiveMap: extra(EMISSION, true, "emission"),
        };
        entry.map.name = source.name;
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
        const { map, normalMap, roughnessMap, metalnessMap, aoMap, emissiveMap } = texturesOf(THREE, source, surface.crisp, options, cache);

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
            roughnessMap,
            metalnessMap,
            aoMap,
            roughness: surface.roughness,
            metalness: surface.metallic,
            side: surface.twoSided ? THREE.DoubleSide : THREE.FrontSide,
        });
        // Opaque surfaces ignore the image's alpha; cutout ones test it and
        // blended ones mix over what is behind them.
        if (surface.alphaMode === CUTOUT) material.alphaTest = surface.alphaCutoff;
        if (surface.alphaMode === BLEND) {
            material.transparent = true;
            material.depthWrite = false;
        }
        if (surface.emission > 0) {
            // The emission map when the texture has one, else its colour.
            material.emissive = new THREE.Color(1, 1, 1);
            material.emissiveMap = emissiveMap || map;
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

// The model's outputs (M3) as a Group in the same space as toObject3D's:
// an empty Object3D at every point, named after its output, and a line box
// for every box3d, turned by its axes. A scene puts a light or a handle at a
// point and uses the boxes as colliders, or shows both while authoring.
export function outputsToObject3D(result, THREE, options = {}) {
    const group = new THREE.Group();
    const color = options.color === undefined ? 0xffcc33 : options.color;
    for (const output of result.outputs || []) {
        const values = output.values;
        for (let i = 0; i < output.count; ++i) {
            const name = output.list ? output.name + "[" + i + "]" : output.name;
            if (output.kind === "point3d") {
                const point = new THREE.Object3D();
                point.name = name;
                point.position.set(values[i * 3], values[i * 3 + 1], values[i * 3 + 2]);
                group.add(point);
                continue;
            }
            const at = i * 15;
            const box = new THREE.LineSegments(
                new THREE.EdgesGeometry(new THREE.BoxGeometry(values[at + 3], values[at + 4], values[at + 5])),
                new THREE.LineBasicMaterial({ color }));
            box.name = name;
            const axis = k => new THREE.Vector3(values[at + 6 + k * 3], values[at + 7 + k * 3], values[at + 8 + k * 3]);
            box.quaternion.setFromRotationMatrix(new THREE.Matrix4().makeBasis(axis(0), axis(1), axis(2)));
            box.position.set(values[at], values[at + 1], values[at + 2]);
            group.add(box);
        }
    }
    if (options.meters !== false) group.scale.setScalar(1 / result.unitsPerMeter);
    return group;
}

// Releases the GPU resources of an object made by toObject3D or
// outputsToObject3D. Textures in a shared cache stay; clear them with
// disposeTextures when the scene goes.
export function disposeObject3D(object) {
    object.traverse(node => {
        if (!node.isMesh && !node.isLineSegments) return;
        node.geometry.dispose();
        node.material.dispose();
    });
    if (object.userData.ipropTextures) disposeTextures(object.userData.ipropTextures);
}

export function disposeTextures(cache) {
    for (const entry of cache.values())
        for (const texture of [entry.map, entry.normalMap, entry.roughnessMap, entry.metalnessMap, entry.aoMap, entry.emissiveMap])
            if (texture) texture.dispose();
    cache.clear();
}
