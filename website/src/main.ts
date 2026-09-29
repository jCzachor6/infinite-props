// The playground: an embedded WASM interpreter, an asset picker, the
// seed/variant/archetype controls and a shareable URL hash
// (#<asset>/<seed>/<variant>/<archetype>).

declare global {
    interface Window {
        createIpropModule(options: { locateFile(): string }): Promise<{
            generate(fileBytes: Uint8Array, seed: number, variant: number,
                archetype: number, sizeX: number, sizeY: number):
                { width: number; height: number; data: Uint8Array };
        }>;
    }
}

// The example assets and their declared ranges.
const ASSETS = [
    { id: "portrait", file: "portrait.iprop", variants: 8, archetypes: 1, label: "portrait — the QuantSpace character" },
    { id: "plant", file: "plant.iprop", variants: 1, archetypes: 1, label: "plant — the interior potted plant" },
    { id: "noise", file: "noise.iprop", variants: 1, archetypes: 1, label: "noise — a seed-driven field" },
];

const state = { asset: "portrait", seed: 0, variant: 0, archetype: 0 };
let iprop: Awaited<ReturnType<typeof window.createIpropModule>> | null = null;

const $ = (id: string) => document.getElementById(id) as HTMLElement;
const canvas = $("canvas") as HTMLCanvasElement;

function status(text: string, isError = false) {
    const node = $("status");
    node.textContent = text;
    node.className = isError ? "error" : "";
}

function currentAsset() {
    return ASSETS.find((a) => a.id === state.asset)!;
}

// ---- the URL hash -----------------------------------------------------------

function readHash() {
    const parts = location.hash.replace(/^#/, "").split("/").filter(Boolean);
    if (parts.length >= 1 && ASSETS.some((a) => a.id === parts[0])) {
        state.asset = parts[0];
    }
    if (parts.length >= 2) state.seed = Math.max(0, parseInt(parts[1], 10) || 0);
    if (parts.length >= 3) state.variant = Math.max(0, parseInt(parts[2], 10) || 0);
    if (parts.length >= 4) state.archetype = Math.max(0, parseInt(parts[3], 10) || 0);
}

function writeHash() {
    history.replaceState(null, "", "#" + [state.asset, state.seed, state.variant, state.archetype].join("/"));
}

// ---- rendering ------------------------------------------------------------------

async function render() {
    if (!iprop) return;
    const asset = currentAsset();
    status("generating…");
    const bytes = asset.bytes;
    const result = iprop.generate(bytes, state.seed >>> 0, state.variant, state.archetype, 0, 0);
    canvas.width = result.width;
    canvas.height = result.height;
    const context = canvas.getContext("2d")!;
    context.putImageData(
        new ImageData(
            new Uint8ClampedArray(result.data.buffer, result.data.byteOffset, result.data.byteLength),
            result.width, result.height),
        0, 0);
    status("seed " + (state.seed >>> 0) + " · variant " + state.variant +
        " · archetype " + state.archetype + " — deterministic");
    writeHash();
}

function syncControls() {
    const asset = currentAsset();
    (document.getElementById("seed-value") as HTMLInputElement).value = String(state.seed >>> 0);
    (document.getElementById("seed-slider") as HTMLInputElement).value = String(Math.min(state.seed >>> 0, 4095));
    for (const kind of ["variant", "archetype"] as const) {
        const max = kind === "variant" ? asset.variants - 1 : asset.archetypes - 1;
        const slider = document.getElementById(kind + "-slider") as HTMLInputElement;
        const value = document.getElementById(kind + "-value") as HTMLInputElement;
        slider.max = String(max);
        value.max = String(max);
        slider.value = String(Math.min(state[kind], max));
        value.value = String(Math.min(state[kind], max));
    }
}

// ---- wiring ------------------------------------------------------------------------

function bindSlider(kind: "seed" | "variant" | "archetype") {
    const slider = document.getElementById(kind + "-slider") as HTMLInputElement;
    const value = document.getElementById(kind + "-value") as HTMLInputElement;
    slider.addEventListener("input", () => {
        value.value = slider.value;
        state[kind] = parseInt(slider.value, 10);
        render();
    });
    value.addEventListener("change", () => {
        state[kind] = Math.max(0, parseInt(value.value, 10) || 0);
        syncControls();
        render();
    });
}

async function main() {
    readHash();
    for (const asset of ASSETS) {
        (asset as any).bytes = new Uint8Array(await (await fetch(asset.file)).arrayBuffer());
    }
    try {
        iprop = await window.createIpropModule({ locateFile: () => "iprop.wasm" });
    } catch (error) {
        status("cannot load the interpreter: " + error, true);
        return;
    }

    const tabs = $("assets");
    for (const asset of ASSETS) {
        const tab = document.createElement("button");
        tab.className = "asset-tab" + (asset.id === state.asset ? " active" : "");
        tab.textContent = asset.label;
        tab.addEventListener("click", () => {
            state.asset = asset.id;
            state.variant = 0;
            state.archetype = 0;
            for (const node of tabs.children) node.classList.remove("active");
            tab.classList.add("active");
            syncControls();
            render();
        });
        tabs.appendChild(tab);
    }

    $("share").addEventListener("click", async () => {
        writeHash();
        try {
            await navigator.clipboard.writeText(location.href);
            status("link copied — it reproduces this exact view");
        } catch {
            status(location.href);
        }
    });

    bindSlider("seed");
    bindSlider("variant");
    bindSlider("archetype");

    syncControls();
    await render();
}

main();
