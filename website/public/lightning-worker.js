/* The home page's lightning strike renders in this worker: for every frame the
   three passes of the effect (renderPass 0, the sprite; 1, the light mask; 2,
   the light baked from it), sent as bitmaps as soon as they are done. The page
   sends a plan, the strikes it wants in order; the rest are dropped between
   two frames. */
"use strict";

const here = new URL(".", self.location.href);
let interpreter = null;
let bytes = null;
// {id, seed, uniforms, next: the next frame to render, total: frames or null}
let plan = [];
let pumping = false;

async function renderFrame(job) {
    const index = job.next++;
    const pass = renderPass => interpreter.generate(bytes, job.seed >>> 0, 0, 0, { ...job.uniforms, frame: index, renderPass });
    // Each result's RGBA is a view into the WASM heap: copy it before the next call.
    const image = result => new ImageData(new Uint8ClampedArray(result.data), result.width, result.height);
    const sprite = pass(0);
    const anim = sprite.outputs.find(output => output.name === "anim")?.rects[0];
    if (!anim || !(anim.x > 0) || !(anim.y > 0)) throw new Error("The lightning has no anim output.");
    job.total ??= anim.x;
    const images = [image(sprite), image(pass(1))];
    const baked = pass(2);
    const glow = baked.outputs.find(output => output.name === "glow")?.circles[0];
    images.push(image(baked));
    const layers = await Promise.all(images.map(layer => createImageBitmap(layer)));
    const [spriteLayer, mask, light] = layers;
    self.postMessage({ type: "frame", id: job.id, index, total: job.total, fps: anim.y, power: glow ? glow.x : 0,
        sprite: spriteLayer, mask, light }, layers);
}

async function pump() {
    if (pumping || !interpreter) return;
    pumping = true;
    // Frames 0 (the hero) to total + 1, the empty frame a strike ends on.
    for (let job; (job = plan.find(job => job.total === null || job.next <= job.total + 1));) {
        try {
            await renderFrame(job);
        } catch (error) {
            plan = plan.filter(other => other !== job);
            self.postMessage({ type: "error", message: error instanceof Error ? error.message : String(error) });
        }
        // Let a new plan in between two frames.
        await new Promise(resolve => setTimeout(resolve, 0));
    }
    pumping = false;
}

self.onmessage = async event => {
    const message = event.data;
    if (message.type === "plan") {
        // Strikes under way keep their frames, unless asked for afresh.
        const known = new Map(plan.map(job => [job.id, job]));
        plan = message.jobs.map(job => (!job.fresh && known.get(job.id)) || { ...job, next: 0, total: null });
        pump();
    } else if (message.type === "init" && !interpreter) {
        try {
            importScripts(new URL("iprop.js", here).href);
            const response = await fetch(new URL(message.file, here));
            if (!response.ok) throw new Error(`${message.file}: HTTP ${response.status}`);
            bytes = new Uint8Array(await response.arrayBuffer());
            interpreter = await self.createIpropModule({ locateFile: () => new URL("iprop.wasm", here).href });
            self.postMessage({ type: "ready", bytes: bytes.byteLength });
            pump();
        } catch (error) {
            self.postMessage({ type: "error", message: error instanceof Error ? error.message : String(error) });
        }
    }
};
