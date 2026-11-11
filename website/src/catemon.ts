// The Catemon page. A name becomes a seed (32-bit FNV-1a of the season's
// prefix and the name in lower case), and the seed is all the frozen program
// showcase/catemon-1.iprop needs to draw that name's catemon, here in the
// browser. Its `traits` output says what came out (x the kind, w a house
// cat's coat, h the pose); the kinds' names, odds and blurbs come from
// showcase/catemon.json. A catemon is addressed by its query string:
// ?name=Jakub, or ?seed=123 for a plain seed.

import "./site";

type Tier = "common" | "uncommon" | "rare" | "legendary";
interface Kind { id: number; name: string; rarity: Tier; weight: number; blurb: string }
interface Manifest { season: number; seed_prefix: string; file: string; kinds: Kind[]; poses: string[]; coats: string[]; fruits: string[] }
interface Runtime {
    generate(file: Uint8Array, seed: number, width: number, height: number, uniforms?: Record<string, number>): {
        width: number; height: number; data: Uint8Array; outputs: { name: string; rects: { x: number; w: number; h: number }[] }[];
    };
}
interface Cat { query: string; owner: string; file: string; image: ImageData; kind: Kind; label: string; odds: number; look: string }

const ROOT = document.body.dataset.root ?? "../";
const FOUND = "catemon:1:found";
const TIERS: Record<Tier, string> = { common: "#9aa5b1", uncommon: "#4da3ff", rare: "#b77bff", legendary: "#ff9f1a" };
const FRUIT = 26;   // the kind whose fruit the traits do not report
// Names the page does not print: their catemon is drawn as a mystery.
const RUDE = /(^|[^a-z])(shit|cunt|bitch|whore|slut|chuj|cwel|pizda|cipa)([^a-z]|$)|fuck|nigg|faggot|hitler|kurw|pierdol|jeban/;

const el = <T extends HTMLElement = HTMLElement>(name: string) => document.querySelector<T>(`[data-${name}]`)!;
const stage = el("stage");
const card = el("card");
const input = el<HTMLInputElement>("name");
const dex = el("dex");
const reducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)");
const pageTitle = document.title;

let runtime: Runtime;
let program: Uint8Array;
let manifest: Manifest;
let totalWeight = 0;
let current: Cat | null = null;
let found: Record<string, string> = {};   // kind id -> the query of the first catemon of that kind met here
let timers: number[] = [];

function say(message: string, isError = false) {
    el("status").textContent = message;
    el("status").classList.toggle("is-error", isError);
}

const article = (noun: string) => (/^[aeiou]/i.test(noun) && !/^(uni|eu|one)/i.test(noun) ? "an" : "a");

// A name as the page keeps it: composed, single spaces, at most 24 characters. Its case is kept for display.
const tidy = (raw: string) => Array.from(raw.normalize("NFKC").replace(/\s+/g, " ").trim()).slice(0, 24).join("").trim();

function seedOf(name: string) {
    let hash = 0x811c9dc5;
    for (const byte of new TextEncoder().encode(manifest.seed_prefix + name.toLowerCase())) hash = Math.imul(hash ^ byte, 0x01000193);
    return hash >>> 0;
}

// One render; the RGBA view into the module heap is copied at once.
function generate(seed: number, uniforms: Record<string, number> = {}) {
    const result = runtime.generate(program, seed, 0, 0, uniforms);
    const traits = result.outputs.find((output) => output.name === "traits")!.rects[0];
    return { image: new ImageData(new Uint8ClampedArray(result.data), result.width, result.height), traits };
}

// The catemon a query string names, or null when it names none.
function catOf(query: string): Cat | null {
    const params = new URLSearchParams(query);
    const name = tidy(params.get("name") ?? "");
    const number = params.get("seed") ?? "";
    if (!name && !/^\d{1,10}$/.test(number)) return null;
    const seed = name ? seedOf(name) : Number(number) >>> 0;
    const { image, traits } = generate(seed);
    const kind = manifest.kinds.find((entry) => entry.id === traits.x) ?? manifest.kinds[0];
    // A Fruit catemon's fruit is the one that draws the same picture when forced.
    const fruit = kind.id === FRUIT && manifest.fruits.find((_, n) =>
        generate(seed, { fruit: n + 1 }).image.data.every((value, i) => value === image.data[i]));
    const coat = manifest.coats[traits.w - 1];
    const pose = manifest.poses[traits.h - 1];
    const plain = name.toLowerCase().normalize("NFD").replace(/[̀-ͯ]/g, "");
    return {
        query: name ? `?name=${encodeURIComponent(name)}` : `?seed=${seed}`,
        owner: !name ? `Catemon #${seed}` : RUDE.test(plain) ? "A Mystery Catemon" : `${name}'s Catemon`,
        file: `catemon-${plain.replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "") || seed}.png`,
        image, kind,
        label: fruit || kind.name,
        odds: Math.round(totalWeight / kind.weight),
        look: coat ? `Its coat is ${coat}, and it is ${pose}.` : `This one is ${pose}.`,
    };
}

// Draws a picture, or with `shade` its shape alone in that colour.
function paint(canvas: HTMLCanvasElement, image: ImageData, shade = "") {
    const context = canvas.getContext("2d")!;
    context.putImageData(image, 0, 0);
    if (!shade) return;
    context.globalCompositeOperation = "source-in";
    context.fillStyle = shade;
    context.fillRect(0, 0, canvas.width, canvas.height);
}

// Shows the catemon a query names (none hides the result). `push` is a visitor's
// own choice: it enters the history and is brought into view.
function show(query: string, push: boolean) {
    const cat = catOf(query);
    timers.forEach((timer) => window.clearTimeout(timer));
    current = cat;
    el("result").hidden = !cat;
    document.title = cat ? `${cat.owner} · ${cat.label} · Catemon` : pageTitle;
    if (!cat) return;
    input.value = new URLSearchParams(cat.query).get("name") ?? "";
    if (push && cat.query !== location.search) history.pushState(null, "", cat.query);
    say("");
    paint(el<HTMLCanvasElement>("cat"), cat.image);
    paint(el<HTMLCanvasElement>("shade"), cat.image, "#0b0e12");
    el("cat").setAttribute("aria-label", `${cat.owner}: ${article(cat.kind.rarity)} ${cat.kind.rarity} ${cat.label}`);
    stage.dataset.tier = el("rarity").dataset.tier = el("rarity").textContent = cat.kind.rarity;
    el("owner").textContent = cat.owner;
    el("kind").textContent = cat.label;
    el("odds").textContent = `About 1 name in ${cat.odds} meets ${article(cat.kind.name)} ${cat.kind.name}. ${cat.look}`;
    el("blurb").textContent = cat.kind.blurb;
    // A shaking silhouette, a flash in the rarity's colour, then the catemon; at once with reduced motion.
    const settle = () => {
        stage.dataset.phase = "shown";
        card.dataset.ready = "true";
        remember(cat);
    };
    if (reducedMotion.matches) settle();
    else {
        card.dataset.ready = "false";
        stage.dataset.phase = "shake";
        timers = [window.setTimeout(() => { stage.dataset.phase = "flash"; }, 950), window.setTimeout(settle, 1250)];
    }
    if (push) stage.scrollIntoView({ block: "center", behavior: reducedMotion.matches ? "auto" : "smooth" });
}

// ---- sharing: a 1080x1350 card, the link, a line of text ---------------------------------------

function drawCard(cat: Cat) {
    const canvas = Object.assign(document.createElement("canvas"), { width: 1080, height: 1350 });
    const context = canvas.getContext("2d")!;
    const font = getComputedStyle(document.body).fontFamily;
    const tier = TIERS[cat.kind.rarity];
    const light = (colour: string, radius: number) => {
        const gradient = context.createRadialGradient(540, 550, 0, 540, 550, radius);
        gradient.addColorStop(0, colour);
        gradient.addColorStop(1, "#13161a00");
        context.fillStyle = gradient;
        context.fillRect(0, 0, 1080, 1350);
    };
    context.fillStyle = "#13161a";
    context.fillRect(0, 0, 1080, 1350);
    light("#2a3642", 860);
    light(tier + "55", 470);
    context.strokeStyle = tier;
    context.lineWidth = 6;
    context.strokeRect(21, 21, 1038, 1308);
    // the catemon at six times its size, on whole pixels
    const sprite = Object.assign(document.createElement("canvas"), { width: 128, height: 128 });
    paint(sprite, cat.image);
    context.imageSmoothingEnabled = false;
    context.drawImage(sprite, 156, 150, 768, 768);
    // a centred line, in the largest size up to `size` that fits the card
    const line = (text: string, y: number, size: number, weight: number, colour: string) => {
        do context.font = `${weight} ${size}px ${font}`; while (context.measureText(text).width > 960 && (size -= 2) > 20);
        context.fillStyle = colour;
        context.fillText(text, 540, y);
    };
    context.textAlign = "center";
    line(`CATEMON · SEASON ${manifest.season}`, 104, 30, 600, "#f5d90a");
    line(cat.owner, 990, 58, 300, "#9fb2c4");
    line(cat.label, 1100, 112, 700, "#edf0f2");
    line(`${cat.kind.rarity.toUpperCase()} · 1 IN ${cat.odds}`, 1180, 34, 700, tier);
    line("What's yours?   infiniteprops.dev/catemon", 1284, 34, 500, "#f5d90a");
    return canvas;
}

const cardOf = (cat: Cat) => new Promise<File>((resolve) =>
    drawCard(cat).toBlob((blob) => resolve(new File([blob!], cat.file, { type: "image/png" }))));
const linkOf = (cat: Cat) => new URL(cat.query, location.href).href;
const copyLink = (cat: Cat) => navigator.clipboard.writeText(linkOf(cat)).then(() => true, () => false);

async function download(cat: Cat) {
    const anchor = Object.assign(document.createElement("a"), { href: URL.createObjectURL(await cardOf(cat)), download: cat.file });
    anchor.click();
    window.setTimeout(() => URL.revokeObjectURL(anchor.href), 10000);
    say("Card downloaded.");
}

// The share sheet with the card where there is one; else the card as a file and the link on the clipboard.
async function share(cat: Cat) {
    const tier = cat.kind.rarity === "rare" || cat.kind.rarity === "legendary" ? `${cat.kind.rarity} ` : "";
    const data = { title: cat.owner, text: `My Catemon is ${article(tier + cat.label)} ${tier}${cat.label}. What's yours?`, url: linkOf(cat) };
    const file = await cardOf(cat);
    try {
        if (navigator.canShare?.({ files: [file] })) return await navigator.share({ ...data, files: [file] });
        if (navigator.share) return await navigator.share(data);
    } catch (error) {
        if ((error as Error).name === "AbortError") return;   // the sheet was closed
    }
    await download(cat);
    if (await copyLink(cat)) say("Card downloaded and link copied. Post them together.");
}

// ---- the collection: the first catemon of each kind met on this device --------------------------

function remember(cat: Cat) {
    if (!found[cat.kind.id]) {
        found[cat.kind.id] = cat.query;
        try { localStorage.setItem(FOUND, JSON.stringify(found)); } catch { /* the collection lasts this visit */ }
    }
    tile(cat.kind);
}

// A kind's tile: the catemon that met it, or the shape of a stand-in while nobody has.
function tile(kind: Kind) {
    const button = dex.querySelector<HTMLButtonElement>(`[data-kind="${kind.id}"]`)!;
    const cat = found[kind.id] ? catOf(found[kind.id]) : null;
    paint(button.querySelector("canvas")!, cat?.image ?? generate(7, { kind: kind.id, pose: 1 }).image, cat ? "" : "#0e1217");
    button.querySelector("b")!.textContent = cat?.label ?? "???";
    button.disabled = !cat;
    el("found").textContent = `Found ${Object.keys(found).length} of ${manifest.kinds.length}`;
}

// ---- start -----------------------------------------------------------------------------------

async function main() {
    try {
        manifest = await (await fetch(`${ROOT}showcase/catemon.json`)).json();
        totalWeight = manifest.kinds.reduce((sum, kind) => sum + kind.weight, 0);
        program = new Uint8Array(await (await fetch(`${ROOT}showcase/${manifest.file}`)).arrayBuffer());
        runtime = await (window as unknown as { createIpropModule(options: { locateFile(): string }): Promise<Runtime> })
            .createIpropModule({ locateFile: () => `${ROOT}iprop.wasm` });
    } catch (error) {
        say("The catemons could not wake up in this browser: " + (error as Error).message, true);
        return;
    }
    try { found = JSON.parse(localStorage.getItem(FOUND) ?? "{}") ?? {}; } catch { /* no storage: nothing is remembered */ }

    // the tiles, the common kinds first, painted one at a time so the page stays responsive
    const order = Object.keys(TIERS);
    const kinds = [...manifest.kinds].sort((a, b) => order.indexOf(a.rarity) - order.indexOf(b.rarity));
    dex.innerHTML = kinds.map((kind) => `<li><button class="cm-dex__tile" type="button" data-kind="${kind.id}" data-tier="${kind.rarity}" disabled>`
        + `<canvas width="128" height="128"></canvas><b>???</b><span class="rarity" data-tier="${kind.rarity}">${kind.rarity}</span></button></li>`).join("");
    kinds.forEach((kind, n) => window.setTimeout(() => tile(kind), 20 * n));
    dex.addEventListener("click", (event) => {
        const id = (event.target as Element).closest<HTMLElement>("[data-kind]")?.dataset.kind;
        if (id && found[id]) show(found[id], true);
    });

    el("form").addEventListener("submit", (event) => {
        event.preventDefault();
        if (tidy(input.value)) show(`?name=${encodeURIComponent(input.value)}`, true);
        else input.focus();
    });
    el("random").addEventListener("click", () => show(`?seed=${1 + Math.floor(Math.random() * 999999)}`, true));
    el("share").addEventListener("click", () => current && void share(current));
    el("download").addEventListener("click", () => current && void download(current));
    el("copy").addEventListener("click", () => current && void copyLink(current).then((copied) => say(copied ? "Link copied." : location.href)));
    window.addEventListener("popstate", () => show(location.search, false));
    el<HTMLButtonElement>("go").disabled = el<HTMLButtonElement>("random").disabled = false;
    say("");
    show(location.search, false);
}

void main();
