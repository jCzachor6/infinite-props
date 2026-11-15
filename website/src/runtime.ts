// The WebAssembly interpreter as the pages use it. public/iprop.js, loaded by
// a plain script tag before the page's module, defines createIpropModule.

export interface Output {
    name: string;
    points: { x: number; y: number }[];
    rects: { x: number; y: number; w: number; h: number }[];
}
export type Uniforms = Record<string, number | string>;
// An image published beside the main one. Kind 1 is a normal map: R is x
// rightward, G is y upward, B is z toward the viewer, each byte b / 127.5 - 1.
export interface AuxImage {
    kind: number;
    name: string;
    width: number;
    height: number;
    data: Uint8Array;
}
export interface Runtime {
    generate(file: Uint8Array, seed: number, width: number, height: number, uniforms?: Uniforms):
        { width: number; height: number; data: Uint8Array; outputs: Output[]; images?: AuxImage[] };
}

// Starts the interpreter; `root` leads from the page to the site's root.
export const startRuntime = (root = "") => (window as unknown as {
    createIpropModule(options: { locateFile(): string }): Promise<Runtime>;
}).createIpropModule({ locateFile: () => root + "iprop.wasm" });

// One render as an image, its normal map when the prop publishes one, and its
// outputs; the RGBA view into the module heap is copied at once.
export function render(runtime: Runtime, file: Uint8Array, seed: number, uniforms: Uniforms = {}) {
    const result = runtime.generate(file, seed >>> 0, 0, 0, uniforms);
    const normal = result.images?.find((image) => image.kind === 1);
    return {
        image: new ImageData(new Uint8ClampedArray(result.data), result.width, result.height),
        normal: normal ? new ImageData(new Uint8ClampedArray(normal.data), normal.width, normal.height) : null,
        outputs: result.outputs,
    };
}
