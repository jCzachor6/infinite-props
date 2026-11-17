// The home page showcase: real .iprop files executed in the browser by the
// WebAssembly interpreter. Every tile keeps its own seed. While auto-roll is
// on, a tile steps its seed up by one at a random moment; the ‹ › buttons and
// the arrow keys step it by hand. The portrait outputs card shows the
// structured outputs of the latest portrait, and the game slots draw one
// enchanted ornate weapon each, as a silhouette until hovered.
//
// Weapons glow the way a game host would make them: the pack renders its
// emission map (input `renderPass` 1) and reports the bloom in its `glow` output
// {palette, power, sigma, gain}; the page blurs the map by sigma and adds it
// behind the sprite.
//
// Each weapon also shows its loot rarity: common (plain), rare (soldier), epic
// (ornate) or legendary (ornate, enchanted and at full power, glow power 4).

import "./site";
import "./butterfly";
import "./lightning";
import { render as renderWith, startRuntime, type Output, type Runtime, type Uniforms } from "./runtime";

const SEED_LIMIT = 0xffffffff;
const files = new Map<string, Uint8Array>();
const WEAPON_ASSETS = new Set(["sword", "axe", "spear", "staff"]);
let iprop: Runtime | null = null;

function randomSeed() {
    return 1 + Math.floor(Math.random() * 99999);
}

// Renders one asset. The browser composites weapon bloom from a separate emission pass.
function render(asset: string, seed: number, uniforms: Uniforms = {}) {
    return renderWith(iprop!, files.get(asset)!, seed, WEAPON_ASSETS.has(asset) ? { bloom: 0, ...uniforms } : uniforms);
}

// A weapon's look for a seed, the same every time the seed comes back. The
// packs keep magic rare (most weapons are plain, half are never enchanted);
// the showcase leans on it: ornate 1 in 2, soldier 3 in 10, plain 1 in 5,
// each enchanted (enchantment 2) 4 times in 5.
function weaponLook(seed: number) {
    let hash = Math.imul(seed ^ 0x2f6b9a13, 0x9e3779b1);
    hash ^= hash >>> 15;
    hash = Math.imul(hash, 0x85ebca77) >>> 0;
    const pick = hash % 10;
    const quality = pick < 5 ? 2 : pick < 8 ? 3 : 1;
    const enchantment = (hash >>> 8) % 5 === 0 ? 1 : 2;
    return { enchantment, quality, uniforms: { quality, enchant: enchantment } as Uniforms };
}

type Rarity = "common" | "rare" | "epic" | "legendary";

// The `glow` output is {palette, power, sigma, gain}; power 0 means no glow.
function glowOf(outputs: Output[]) {
    return outputs.find((output) => output.name === "glow")?.rects[0];
}

function rarityOf(quality: number, enchantment: number, outputs: Output[]): Rarity {
    if (quality === 1) return "common";
    if (quality === 3) return "rare";
    return enchantment === 2 && (glowOf(outputs)?.y ?? 0) >= 4 ? "legendary" : "epic";
}

function showRarity(badge: HTMLElement, rarity: Rarity) {
    badge.hidden = false;
    badge.dataset.tier = rarity;
    badge.textContent = rarity;
}

// Room around the 96x96 sprite for the halo, in native pixels; the CSS for
// canvas.glow places the larger canvas to match (-50%, 200%).
const GLOW_PAD = 48;
const emission = document.createElement("canvas");

// Paints the bloom of a weapon render into its glow canvas, or hides it when
// the weapon has no power. As a game host would: the emission map blurred by
// the output's sigma and added in passes that follow the gain (additive passes
// raise alpha as well as colour, so thin glowing lines still make a visible
// halo), plus a wider, softer pass.
function paintGlow(canvas: HTMLCanvasElement, asset: string, seed: number,
    uniforms: Uniforms, outputs: Output[]) {
    const glow = glowOf(outputs);
    canvas.hidden = !glow || glow.y <= 0;
    if (!glow || canvas.hidden) return;
    paint(emission, render(asset, seed, { ...uniforms, renderPass: 1 }).image);
    canvas.width = emission.width + 2 * GLOW_PAD;
    canvas.height = emission.height + 2 * GLOW_PAD;
    const context = canvas.getContext("2d")!;
    context.globalCompositeOperation = "lighter";
    // A bright core and a wider halo spread out from under the opaque sprite.
    // Scale the core with the reported gain while keeping the colour visible.
    const passes = Math.min(6, Math.max(2, Math.round(glow.h * 0.6)));
    for (const [sigma, count, alpha] of [[glow.w, passes, 0.84], [glow.w * 3, 3, 0.6]]) {
        context.filter = `blur(${sigma}px)`;
        // Split the exposure into passes so alpha never exceeds its 0..1 range.
        for (let exposure = count * alpha; exposure > 0; --exposure) {
            context.globalAlpha = Math.min(1, exposure);
            context.drawImage(emission, GLOW_PAD, GLOW_PAD);
        }
    }
    context.filter = "none";
    context.globalAlpha = 1;
}

function formatSize(bytes: number) {
    return bytes < 1024 ? `${bytes} B` : `${(bytes / 1024).toFixed(1)} KB`;
}

function paint(canvas: HTMLCanvasElement, image: ImageData) {
    canvas.width = image.width;
    canvas.height = image.height;
    canvas.getContext("2d")!.putImageData(image, 0, 0);
}

// ---- the outputs card ----------------------------------------------------------

const outputsCard = document.querySelector<HTMLElement>("[data-outputs]");

function hex(rect: { x: number; y: number; w: number }) {
    return "#" + [rect.x, rect.y, rect.w].map((channel) => Math.round(channel).toString(16).padStart(2, "0")).join("");
}

function showOutputs(tile: Tile, outputs: Output[]) {
    if (!outputsCard) return;
    for (const other of tiles) other.element.classList.toggle("is-source", other === tile);
    outputsCard.querySelector("[data-outputs-source]")!.textContent = `${tile.asset}.iprop\nseed ${tile.seed}`;
    for (const output of outputs) {
        const row = outputsCard.querySelector<HTMLElement>(`[data-output="${output.name}"]`);
        if (!row) continue;
        const swatch = row.querySelector<HTMLElement>(".swatch")!;
        const value = row.querySelector<HTMLElement>("span:last-child")!;
        const rect = output.rects[0];
        const point = output.points[0];
        if (rect) {
            // A colour travels as a rect {r, g, b, a}; alpha 0 means "none".
            const present = rect.h > 0;
            swatch.classList.toggle("swatch--none", !present);
            swatch.style.setProperty("--swatch", present ? hex(rect) : "transparent");
            value.textContent = present ? hex(rect) : "none";
        } else if (point) {
            value.textContent = `${Math.round(point.x)}, ${Math.round(point.y)}`;
        }
    }
}

// ---- the tiles -------------------------------------------------------------------

class Tile {
    readonly asset: string;
    readonly canvas: HTMLCanvasElement;
    readonly glow: HTMLCanvasElement | null;
    readonly rarity: HTMLElement | null;
    readonly seedView: HTMLOutputElement;
    readonly portrait: boolean;
    seed = randomSeed();
    held = false;

    constructor(readonly element: HTMLElement) {
        this.asset = element.dataset.asset!;
        this.canvas = element.querySelector("canvas")!;
        this.glow = element.querySelector("canvas.glow");
        this.rarity = element.querySelector("[data-rarity]");
        this.seedView = element.querySelector("output")!;
        this.portrait = this.asset.startsWith("portraits");

        for (const button of Array.from(element.querySelectorAll<HTMLButtonElement>("[data-step]"))) {
            button.disabled = false;
            button.addEventListener("click", () => this.step(Number(button.dataset.step)));
        }
        element.addEventListener("keydown", (event) => {
            if (event.key !== "ArrowLeft" && event.key !== "ArrowRight") return;
            event.preventDefault();
            this.step(event.key === "ArrowLeft" ? -1 : 1);
        });
        // A tile under the pointer or the keyboard holds still.
        element.addEventListener("pointerenter", () => { this.held = true; });
        element.addEventListener("pointerleave", () => { this.held = false; });
        element.addEventListener("focusin", () => { this.held = true; });
        element.addEventListener("focusout", () => { this.held = false; });
    }

    step(delta: number) {
        this.seed = Math.min(SEED_LIMIT, Math.max(0, this.seed + delta));
        this.draw();
    }

    draw() {
        const look = this.glow ? weaponLook(this.seed) : { enchantment: 0, quality: 0, uniforms: {} };
        const { image, outputs } = render(this.asset, this.seed, look.uniforms);
        paint(this.canvas, image);
        if (this.glow) paintGlow(this.glow, this.asset, this.seed, look.uniforms, outputs);
        if (this.rarity) {
            const rarity = rarityOf(look.quality, look.enchantment, outputs);
            showRarity(this.rarity, rarity);
            this.element.dataset.rarity = rarity;
        }
        this.seedView.value = String(this.seed);
        this.canvas.setAttribute("aria-label", `${this.element.dataset.label}, seed ${this.seed}`);
        for (const marker of Array.from(this.element.querySelectorAll<HTMLElement>("[data-marker]"))) {
            const point = outputs.find((output) => output.name === marker.dataset.marker)?.points[0];
            marker.hidden = !point;
            if (point) {
                marker.style.left = (point.x / image.width) * 100 + "%";
                marker.style.top = (point.y / image.height) * 100 + "%";
            }
        }
        if (this.portrait) showOutputs(this, outputs);
    }
}

const tiles = Array.from(document.querySelectorAll<HTMLElement>("[data-asset]")).map((element) => new Tile(element));

// ---- auto-roll -----------------------------------------------------------------------

const autoroll = document.querySelector<HTMLButtonElement>("[data-autoroll]");
const showcase = document.querySelector<HTMLElement>("#examples");
let autoOn = true;
let inView = false;

try { autoOn = localStorage.getItem("iprop.autoroll") !== "off"; } catch { /* storage may be blocked */ }

function setAuto(on: boolean) {
    autoOn = on;
    autoroll?.setAttribute("aria-pressed", String(on));
    const label = autoroll?.querySelector("[data-autoroll-label]");
    if (label) label.textContent = on ? "Auto-roll on" : "Auto-roll off";
    try { localStorage.setItem("iprop.autoroll", on ? "on" : "off"); } catch { /* ignore */ }
}

function schedule(tile: Tile) {
    window.setTimeout(() => {
        if (autoOn && inView && !document.hidden && !tile.held) tile.step(1);
        schedule(tile);
    }, 500 + Math.random() * 1000);
}

// ---- the game slots ----------------------------------------------------------------------

// Only the finest: ornate, enchanted and at full power.
const LEGENDARY: Uniforms = { enchant: 2, quality: 2, glowStrength: 4 };

function drawSilhouettes() {
    for (const canvas of Array.from(document.querySelectorAll<HTMLCanvasElement>("[data-silhouette]"))) {
        const asset = canvas.dataset.silhouette!;
        const seed = randomSeed();
        const { image, outputs } = render(asset, seed, LEGENDARY);
        paint(canvas, image);
        const glow = canvas.parentElement!.querySelector<HTMLCanvasElement>("canvas.glow");
        if (glow) paintGlow(glow, asset, seed, LEGENDARY, outputs);
    }
}

// Touch screens have no hover: a tap (or Enter) reveals a slot's weapon.
for (const slot of Array.from(document.querySelectorAll<HTMLElement>(".game-slot"))) {
    const reveal = () => slot.classList.toggle("is-revealed");
    slot.addEventListener("click", reveal);
    slot.addEventListener("keydown", (event) => {
        if (event.key === "Enter" || event.key === " ") { event.preventDefault(); reveal(); }
    });
}

// ---- the simplest example: one program, a forest --------------------------------------

// A tree (intro/tree.iprop, from tree.ips, its part clump.ips and the material
// foliage.ips), drawn for a fresh seed per cell.
const forest = document.querySelector<HTMLElement>("[data-forest]");
const forestRoll = document.querySelector<HTMLButtonElement>("[data-forest-roll]");

// Only the cells shown are drawn: 36 on a wide screen, 18 on a phone.
const wideForest = window.matchMedia("(min-width: 48rem)");
// Beside the code (two columns), the forest takes as many rows as fit next to
// the text and code above its foot, and spreads the leftover height between
// the rows, so both columns end together.
const sideBySide = window.matchMedia("(min-width: 64rem)");
const forestText = document.querySelector<HTMLElement>(".first-prop__text");
const forestFoot = document.querySelector<HTMLElement>(".forest__foot");
const markupTrees = forest ? forest.children.length : 0;

function shownTrees() {
    return Array.from(forest!.querySelectorAll<HTMLElement>(".forest__tree"))
        .filter((cell) => cell.offsetParent !== null);
}

// The crown and its shadow fill the middle 144 of the 176 canvas.
const TREE_VIEW = 144;
const TREE_CROP = 16;

// A tree as generated: its image and the normal map the same program
// publishes (tree.ips: `output normal relief`), and what its cell shows now.
const enum Shown { Generated, Lit, Unlit }
interface Planted {
    image: ImageData;
    normal: ImageData | null;
    shown: Shown;
    // Made when the lamp first reaches the tree: its opaque pixels in the
    // view, packed as y << 8 | x, and the image the lamp shades into.
    spots: Uint16Array | null;
    shaded: ImageData | null;
}
const planted = new WeakMap<HTMLElement, Planted>();

function plantTree(cell: HTMLElement) {
    const seed = (Math.random() * SEED_LIMIT) >>> 0;
    const canvas = cell.querySelector("canvas")!;
    canvas.width = canvas.height = TREE_VIEW;
    const { image, normal } = render("tree", seed);
    canvas.getContext("2d")!.putImageData(image, -TREE_CROP, -TREE_CROP);
    planted.set(cell, { image, normal, shown: Shown.Generated, spots: null, shaded: null });
    cell.querySelector("figcaption")!.textContent = `seed ${seed.toString(16).padStart(8, "0")}`;
    cell.dataset.planted = "";
}

function drawForest() {
    if (!forest || !files.has("tree")) return;
    const cells = shownTrees();
    if (forestRoll) forestRoll.textContent = `Roll ${cells.length} new trees`;
    for (const cell of cells) plantTree(cell);
    relight();
}

// ---- a lamp over the forest ---------------------------------------------------------------

// The pointer carries a lamp over the canopy, and the page shades every tree
// the way a game engine lights a sprite: a pixel's brightness is the ambient
// light plus the lamp's, by how squarely the direction stored in the normal
// map faces the lamp and how far away it hangs. Brightness moves in a few
// steps, so the pixel art stays crisp. Without a pointer over the forest
// every tree shows exactly its generated image.
let lamp: { x: number; y: number } | null = null;
let lighting = 0;
// How far the lamp's light carries and how high it hangs, in tree pixels.
const LAMP_REACH = TREE_VIEW * 4.8;
const LAMP_HEIGHT = TREE_VIEW * 0.8;
// The light without the lamp, the lamp at full strength, and the steps of brightness.
const AMBIENT = 0.4;
const LAMP_POWER = 0.9;
const LIGHT_STEPS = 10;

// The opaque pixels of a tree's view: the lamp shades only these.
function opaqueSpots(image: ImageData) {
    const data = image.data, spots: number[] = [];
    for (let y = 0; y < TREE_VIEW; ++y) {
        for (let x = 0; x < TREE_VIEW; ++x) {
            if (data[((y + TREE_CROP) * image.width + x + TREE_CROP) * 4 + 3] !== 0) spots.push(y << 8 | x);
        }
    }
    return Uint16Array.from(spots);
}

// One tree under a lamp at (lx, ly) in the tree's own pixels; a lamp out of
// reach leaves the ambient light alone.
function shadeTree(canvas: HTMLCanvasElement, tree: Planted, lx: number, ly: number, inReach: boolean) {
    const colour = tree.image.data, direction = tree.normal!.data, stride = tree.image.width;
    const spots = tree.spots ??= opaqueSpots(tree.image);
    const shaded = tree.shaded ??= new ImageData(TREE_VIEW, TREE_VIEW), target = shaded.data;
    const dark = Math.round(AMBIENT * LIGHT_STEPS) / LIGHT_STEPS;
    const reach = LAMP_REACH * LAMP_REACH, height = LAMP_HEIGHT * LAMP_HEIGHT;
    for (let i = 0; i < spots.length; ++i) {
        const x = spots[i] & 255, y = spots[i] >> 8;
        const from = ((y + TREE_CROP) * stride + x + TREE_CROP) * 4, to = (y * TREE_VIEW + x) * 4;
        let light = dark;
        if (inReach) {
            const dx = lx - (x + TREE_CROP), dy = ly - (y + TREE_CROP), away = dx * dx + dy * dy;
            const carried = 1 - away / reach;
            if (carried > 0) {
                // A byte b stores b / 127.5 - 1: x rightward, y upward, z toward
                // the viewer. The page's y grows downward.
                const facing = ((direction[from] - 127.5) * dx - (direction[from + 1] - 127.5) * dy
                    + (direction[from + 2] - 127.5) * LAMP_HEIGHT) / (127.5 * Math.sqrt(away + height));
                if (facing > 0) light = Math.round((AMBIENT + LAMP_POWER * facing * carried) * LIGHT_STEPS) / LIGHT_STEPS;
            }
        }
        target[to] = colour[from] * light;
        target[to + 1] = colour[from + 1] * light;
        target[to + 2] = colour[from + 2] * light;
        target[to + 3] = colour[from + 3];
    }
    canvas.getContext("2d")!.putImageData(shaded, 0, 0);
    tree.shown = inReach ? Shown.Lit : Shown.Unlit;
}

function lightForest() {
    lighting = 0;
    if (!forest) return;
    for (const cell of shownTrees()) {
        const tree = planted.get(cell);
        if (!tree || !tree.normal) continue;
        const canvas = cell.querySelector("canvas")!;
        if (!lamp) {
            if (tree.shown !== Shown.Generated) {
                canvas.getContext("2d")!.putImageData(tree.image, -TREE_CROP, -TREE_CROP);
                tree.shown = Shown.Generated;
            }
            continue;
        }
        const box = canvas.getBoundingClientRect();
        // Trees off the screen wait for the pointer's next move.
        if (box.bottom < 0 || box.top > window.innerHeight) continue;
        const scale = TREE_VIEW / box.width;
        const lx = (lamp.x - box.left) * scale + TREE_CROP, ly = (lamp.y - box.top) * scale + TREE_CROP;
        const centre = TREE_CROP + TREE_VIEW / 2;
        const inReach = Math.hypot(lx - centre, ly - centre) < LAMP_REACH + TREE_VIEW * 0.75;
        // A tree the lamp cannot reach is shaded once and then left alone.
        if (inReach || tree.shown !== Shown.Unlit) shadeTree(canvas, tree, lx, ly, inReach);
    }
}

function relight() {
    if (lamp && !lighting) lighting = requestAnimationFrame(lightForest);
}

function moveLamp(event: PointerEvent | null) {
    lamp = event ? { x: event.clientX, y: event.clientY } : null;
    if (!lighting) lighting = requestAnimationFrame(lightForest);
}

if (forest) {
    forest.addEventListener("pointermove", moveLamp);
    forest.addEventListener("pointerdown", moveLamp);
    forest.addEventListener("pointerleave", () => moveLamp(null));
    forest.addEventListener("pointercancel", () => moveLamp(null));
}

function fitForest() {
    if (!forest || !forestText || !forestFoot || !forest.firstElementChild) return;
    let count = markupTrees;
    let rowGap = "";
    if (sideBySide.matches) {
        const grid = getComputedStyle(forest);
        const columns = grid.gridTemplateColumns.split(" ").length;
        const gap = parseFloat(grid.columnGap) || 0;
        // Fractional sizes: rounding one cell would add up over every row.
        const cell = forest.firstElementChild.getBoundingClientRect().height;
        const height = forestText.getBoundingClientRect().height - forestFoot.getBoundingClientRect().height
            - parseFloat(getComputedStyle(forestFoot).marginTop);
        const rows = Math.max(1, Math.floor((height + gap) / (cell + gap)));
        count = rows * columns;
        if (rows > 1) rowGap = `${(height - rows * cell) / (rows - 1)}px`;
    }
    forest.style.rowGap = rowGap;
    while (forest.children.length > count) forest.lastElementChild!.remove();
    while (forest.children.length < count) {
        const cell = forest.firstElementChild!.cloneNode(true) as HTMLElement;
        delete cell.dataset.planted;
        cell.querySelector("figcaption")!.textContent = "seed …";
        forest.append(cell);
    }
    if (!files.has("tree")) return;
    // New cells get a tree; the trees already shown stay.
    for (const cell of shownTrees()) if (!("planted" in cell.dataset)) plantTree(cell);
    relight();
    if (forestRoll) forestRoll.textContent = `Roll ${shownTrees().length} new trees`;
}

async function startForest() {
    if (!forest) return;
    try {
        const response = await fetch("intro/tree.iprop");
        if (!response.ok) throw new Error(`intro/tree.iprop: HTTP ${response.status}`);
        files.set("tree", new Uint8Array(await response.arrayBuffer()));
        for (const size of Array.from(document.querySelectorAll("[data-forest-size]"))) {
            size.textContent = formatSize(files.get("tree")!.byteLength);
        }
        fitForest();
        drawForest();
        if (forestRoll) {
            forestRoll.disabled = false;
            forestRoll.addEventListener("click", drawForest);
        }
        wideForest.addEventListener("change", drawForest);
        // Tabs, fonts and the window change the code column's height; refit
        // on the next frame, outside the observer's own layout pass.
        let fitting = 0;
        const refit = () => { cancelAnimationFrame(fitting); fitting = requestAnimationFrame(fitForest); };
        const resized = new ResizeObserver(refit);
        if (forestText) resized.observe(forestText);
        resized.observe(forest);
    } catch {
        // The forest stays empty; the rest of the page does not depend on it.
    }
}

// The tree's source files as tabs: a click or the arrow keys pick one; the
// download link follows the open file.
for (const box of Array.from(document.querySelectorAll<HTMLElement>("[data-code-tabs]"))) {
    const tabs = Array.from(box.querySelectorAll<HTMLButtonElement>("[role=tab]"));
    const download = box.querySelector<HTMLAnchorElement>("[data-code-download]");
    const open = (tab: HTMLButtonElement) => {
        for (const other of tabs) {
            const on = other === tab;
            other.setAttribute("aria-selected", String(on));
            other.tabIndex = on ? 0 : -1;
            document.getElementById(other.getAttribute("aria-controls")!)!.hidden = !on;
        }
        if (download) download.href = tab.dataset.file!;
    };
    tabs.forEach((tab, k) => {
        tab.addEventListener("click", () => open(tab));
        tab.addEventListener("keydown", (event) => {
            const step = event.key === "ArrowRight" ? 1 : event.key === "ArrowLeft" ? -1 : 0;
            if (!step) return;
            event.preventDefault();
            const next = tabs[(k + step + tabs.length) % tabs.length];
            open(next);
            next.focus();
        });
    });
}

// ---- 3D: a room of chairs -------------------------------------------------------------------

// The models section brings three.js and the model module with it, so both
// load only when the section comes near the screen.
const models = document.querySelector<HTMLElement>("[data-models]");
if (models) {
    const near = new IntersectionObserver((entries) => {
        if (!entries.some((entry) => entry.isIntersecting)) return;
        near.disconnect();
        void import("./models").then(({ startModels }) => startModels(models));
    }, { rootMargin: "600px 0px" });
    near.observe(models);
}

// ---- start -----------------------------------------------------------------------------------

async function main() {
    const status = document.querySelector<HTMLElement>("[data-showcase-status]");
    try {
        const names = new Set([
            ...tiles.map((tile) => tile.asset),
            ...Array.from(document.querySelectorAll<HTMLElement>("[data-silhouette]")).map((node) => node.dataset.silhouette!),
        ]);
        await Promise.all(Array.from(names).map(async (name) => {
            const response = await fetch(`showcase/${name}.iprop`);
            if (!response.ok) throw new Error(`showcase/${name}.iprop: HTTP ${response.status}`);
            files.set(name, new Uint8Array(await response.arrayBuffer()));
        }));
        iprop = await startRuntime();
        for (const tile of tiles) tile.draw();
        drawSilhouettes();
        for (const tile of tiles) {
            const size = tile.element.querySelector("[data-size]");
            if (size) size.textContent = formatSize(files.get(tile.asset)!.byteLength);
        }
        const swordSize = document.querySelector("[data-stat-size]");
        if (swordSize) swordSize.textContent = formatSize(files.get("sword")!.byteLength);
        if (status) status.textContent = "";
        void startForest();
    } catch (error) {
        if (status) {
            status.textContent = "The interpreter could not start in this browser: " + (error as Error).message;
            status.classList.add("is-error");
        }
        return;
    }

    setAuto(autoOn);
    autoroll?.addEventListener("click", () => setAuto(!autoOn));
    if (showcase) {
        new IntersectionObserver((entries) => { inView = entries[0].isIntersecting; }).observe(showcase);
    }
    tiles.forEach(schedule);
}

main();
