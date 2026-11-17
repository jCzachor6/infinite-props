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

/** Releases the geometry and materials of an object made by toObject3D, and its textures when it owns them. */
export function disposeObject3D(object: unknown): void;

/** Disposes and clears a texture cache. */
export function disposeTextures(cache: Map<string, CachedTextures>): void;
