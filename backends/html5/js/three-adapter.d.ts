// Types of three-adapter.js and of the results of api3d.js (preview).

/** An image published beside a texture's colour image: kind 1 is a normal map, 2-5 the
 * roughness, metallic, ao and emission maps. */
export interface ModelImage {
    index: number;
    kind: number;
    source: number;
    name: string;
    width: number;
    height: number;
    data: Uint8Array;
}

/** One generated texture: RGBA rows from the top. Equal keys mean equal pixels. */
export interface ModelTexture {
    index: number;
    key: string;
    name: string;
    width: number;
    height: number;
    assetWidth: number;
    assetHeight: number;
    repeats: boolean;
    data: Uint8Array;
    images: ModelImage[];
    normal: ModelImage | null;
    /** What generating it cost, and the kinds of its images as bits. */
    fuel: number;
    work: number;
    kinds: number;
    /** The page already held it: `data` is the cache's, or empty before the cache completed the result. */
    reused: boolean;
}

/** The triangles of one material. `texture` indexes the result's textures. */
export interface ModelSurface {
    index: number;
    texture: number;
    roughness: number;
    metallic: number;
    emission: number;
    /** 0 opaque, 1 cutout, 2 blend (glass). */
    alphaMode: number;
    alphaCutoff: number;
    twoSided: boolean;
    crisp: boolean;
    vertexCount: number;
    indexCount: number;
    positions: Float32Array;
    normals: Float32Array;
    tangents: Float32Array;
    uvs: Float32Array;
    indices: Uint32Array;
}

/** One output of a model (M3): a point is 3 floats, a box 15 (centre, size, x, y and z axis). */
export interface ModelOutput {
    index: number;
    name: string;
    kind: "point3d" | "box3d";
    list: boolean;
    count: number;
    values: Float32Array;
    points?: number[][];
    boxes?: { center: number[]; size: number[]; axes: number[][] }[];
}

/** What model.generate() of api3d.js returns. */
export interface ModelResult {
    bounds: { min: number[]; max: number[] };
    size: number[];
    unitsPerMeter: number;
    vertexCount: number;
    triangleCount: number;
    fuel: { budget: number; used: number };
    work: { budget: number; used: number };
    textures: ModelTexture[];
    surfaces: ModelSurface[];
    outputs: ModelOutput[];
    /** The level of detail it was generated at, 0 to 7. */
    lod: number;
}

/** The colour and normal textures of one cache entry; three.js Texture objects. */
export interface CachedTextures<Texture = unknown> {
    map: Texture;
    normalMap: Texture | null;
}

export interface AdapterOptions<Texture = unknown> {
    /** Shared between calls: surfaces whose texture keys are equal share one texture. */
    textures?: Map<string, CachedTextures<Texture>>;
    /** Cast and receive shadows (default true). */
    shadows?: boolean;
    /** Anisotropic filtering of smooth textures (default 4). */
    anisotropy?: number;
    /** Scale the group to metres (default true). */
    meters?: boolean;
}

/**
 * A result as a three.js Group of one Mesh per surface. `THREE` is the
 * caller's three.js namespace; the returned value is its `Group`.
 */
export function toObject3D<Three extends { Group: new () => unknown }>(
    result: ModelResult, THREE: Three, options?: AdapterOptions): InstanceType<Three["Group"]>;

/**
 * The model's outputs as a Group in the space of toObject3D: an empty Object3D
 * at each point, named after its output (`name[i]` in a list), and a line box
 * for each box. `color` is the line colour; `meters` scales as toObject3D does.
 */
export function outputsToObject3D<Three extends { Group: new () => unknown }>(
    result: ModelResult, THREE: Three, options?: { color?: number; meters?: boolean }): InstanceType<Three["Group"]>;

/**
 * Levels of detail of one prop as a `THREE.LOD`: generations of one model and
 * seed at rising `lod`, nearest first, with the distance in metres from which
 * each shows (the first is 0). The levels share the textures of the cache.
 */
export function toLOD<Three extends { LOD: new () => unknown }>(
    results: ModelResult[], THREE: Three, distances: number[], options?: AdapterOptions): InstanceType<Three["LOD"]>;

/** Releases the geometry and materials of an object made by toObject3D, toLOD or outputsToObject3D, and its textures when it owns them. */
export function disposeObject3D(object: unknown): void;

/** Disposes and clears a texture cache. */
export function disposeTextures(cache: Map<string, CachedTextures>): void;
