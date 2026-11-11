// The WebAssembly interpreter as the pages use it. public/iprop.js, loaded by
// a plain script tag before the page's module, defines createIpropModule.

export interface Output {
    name: string;
    points: { x: number; y: number }[];
    rects: { x: number; y: number; w: number; h: number }[];
}
export type Uniforms = Record<string, number | string>;
export interface Runtime {
    generate(file: Uint8Array, seed: number, width: number, height: number, uniforms?: Uniforms):
        { width: number; height: number; data: Uint8Array; outputs: Output[] };
}

// Starts the interpreter; `root` leads from the page to the site's root.
export const startRuntime = (root = "") => (window as unknown as {
    createIpropModule(options: { locateFile(): string }): Promise<Runtime>;
}).createIpropModule({ locateFile: () => root + "iprop.wasm" });

// One render as an image and its outputs; the RGBA view into the module heap is copied at once.
export function render(runtime: Runtime, file: Uint8Array, seed: number, uniforms: Uniforms = {}) {
    const result = runtime.generate(file, seed >>> 0, 0, 0, uniforms);
    return { image: new ImageData(new Uint8ClampedArray(result.data), result.width, result.height), outputs: result.outputs };
}
