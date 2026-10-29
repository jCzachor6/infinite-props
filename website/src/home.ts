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
// "Roll a legendary" searches seeds for one and opens it as a drop with a
// share link, #roll/<weapon>/<seed>: the look follows from the seed alone, so
// the same link shows the same pixels to everyone.

import "./site";
import "./butterfly";

interface Output {
    name: string;
    points: { x: number; y: number }[];
    rects: { x: number; y: number; w: number; h: number }[];
}

interface Result {
    width: number;
    height: number;
    data: Uint8Array;
    outputs: Output[];
}

type Uniforms = Record<string, number | string>;

interface IpropModule {
    generate(fileBytes: Uint8Array, seed: number, sizeX: number, sizeY: number, uniforms?: Uniforms): Result;
}

declare global {
    interface Window {
        createIpropModule(options: { locateFile(): string }): Promise<IpropModule>;
    }
}

const SEED_LIMIT = 0xffffffff;
const files = new Map<string, Uint8Array>();
const WEAPON_ASSETS = new Set(["sword", "axe", "spear", "staff"]);
let iprop: IpropModule | null = null;

function randomSeed() {
    return 1 + Math.floor(Math.random() * 99999);
}

// Renders one asset; the RGBA view into the module heap is copied at once.
function render(asset: string, seed: number, uniforms: Uniforms = {}) {
    // The browser composites weapon bloom from a separate emission pass.
    const inputs = WEAPON_ASSETS.has(asset) ? { bloom: 0, ...uniforms } : uniforms;
    const result = iprop!.generate(files.get(asset)!, seed >>> 0, 0, 0, inputs);
    const image = new ImageData(new Uint8ClampedArray(result.data), result.width, result.height);
    return { image, outputs: result.outputs };
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
    return { enchantment, quality, uniforms: { quality, v: enchantment } as Uniforms };
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
    uniforms: Uniforms, outputs: Output[], intensity = 1) {
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
        for (let exposure = count * alpha * intensity; exposure > 0; --exposure) {
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
const LEGENDARY: Uniforms = { v: 2, quality: 2, glowStrength: 4 };

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

// ---- the legendary drop: a weapon case -------------------------------------------
//
// "Roll a legendary" opens a case: a strip of real rolled weapons runs under
// a marker, slows down and stops on a legendary one, then the win shows it
// big. The next case is prepared in idle time, so a click starts at once.

const WEAPONS: Record<string, string> = { sword: "Sword", axe: "Axe", spear: "Spear", staff: "Staff" };
const drop = document.querySelector<HTMLDialogElement>("[data-drop]");
const dropCanvas = document.querySelector<HTMLCanvasElement>("[data-drop-canvas]");
const track = document.querySelector<HTMLElement>("[data-reel]");
const ROLL_LINK = /^#roll\/(sword|axe|spear|staff)\/(\d{1,10})$/;
const reducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)");

interface Item { asset: string; seed: number; rarity: Rarity; image: ImageData; glow: HTMLCanvasElement }
interface Case { items: Item[]; winner: Item }

const REEL_LENGTH = 40;
const WIN_INDEX = 34;
// The fillers come the way a case shows its loot: mostly common.
const FILLER_ODDS: [Rarity, number][] = [["common", 0.45], ["rare", 0.33], ["epic", 0.22]];
const QUALITY: Record<Rarity, number> = { common: 1, rare: 3, epic: 2, legendary: 2 };

let nextCase: Promise<Case> | null = null;
let spin: Animation | null = null;

function idle() {
    return new Promise<void>((resolve) => {
        if ("requestIdleCallback" in window) requestIdleCallback(() => resolve(), { timeout: 250 });
        else setTimeout(resolve, 16);
    });
}

// Searches seeds from a random start for a weapon of the wanted rarity. The
// look is a cheap hash; only matching candidates are rendered.
function rollItem(wanted: Rarity): Item {
    const names = Object.keys(WEAPONS);
    const asset = names[Math.floor(Math.random() * names.length)];
    for (let seed = randomSeed(), tries = 0; ; ++seed, ++tries) {
        const look = weaponLook(seed);
        if (look.quality !== QUALITY[wanted] || (wanted === "legendary" && look.enchantment !== 2)) continue;
        const { image, outputs } = render(asset, seed, look.uniforms);
        const rarity = rarityOf(look.quality, look.enchantment, outputs);
        if (rarity === wanted || tries > 50000) {
            // Prepare the halo with the case, keeping WASM and blur work off the spin.
            const glow = document.createElement("canvas");
            glow.className = "glow";
            glow.setAttribute("aria-hidden", "true");
            paintGlow(glow, asset, seed, look.uniforms, outputs);
            return { asset, seed, rarity, image, glow };
        }
    }
}

async function prepareCase(): Promise<Case> {
    const items: Item[] = [];
    for (let i = 0; i < REEL_LENGTH; ++i) {
        let roll = Math.random();
        let tier: Rarity = "epic";
        for (const [name, odds] of FILLER_ODDS) {
            if (roll < odds) { tier = name; break; }
            roll -= odds;
        }
        items.push(rollItem(i === WIN_INDEX ? "legendary" : tier));
        if (i % 4 === 3) await idle();
    }
    return { items, winner: items[WIN_INDEX] };
}

// Focus the dialog itself, not its first button, so nothing shows a focus ring.
function openModal() {
    if (!drop || drop.open) return;
    drop.showModal();
    drop.focus();
}

function makeSparks() {
    const box = drop?.querySelector("[data-sparks]");
    if (!box || box.childElementCount) return;
    for (let i = 0; i < 16; ++i) {
        const spark = document.createElement("span");
        spark.className = "spark";
        spark.style.left = `${6 + Math.random() * 88}%`;
        spark.style.top = `${6 + Math.random() * 88}%`;
        spark.style.animationDelay = `${(Math.random() * 2.4).toFixed(2)}s`;
        box.append(spark);
    }
}

// The win: the weapon big, with its rarity, its data and its share link.
function showWin(asset: string, seed: number) {
    if (!drop || !dropCanvas) return;
    const look = weaponLook(seed);
    const { image, outputs } = render(asset, seed, look.uniforms);
    paint(dropCanvas, image);
    const halo = drop.querySelector<HTMLCanvasElement>(".win canvas.glow:not(.glow--front)")!;
    paintGlow(halo, asset, seed, look.uniforms, outputs, 1.2);
    const overlay = drop.querySelector<HTMLCanvasElement>(".win canvas.glow--front")!;
    overlay.hidden = halo.hidden;
    overlay.width = halo.width;
    overlay.height = halo.height;
    if (!halo.hidden) {
        // Reuse the halo, clipped to the weapon; CSS applies one third of its intensity.
        const context = overlay.getContext("2d")!;
        context.putImageData(image, GLOW_PAD, GLOW_PAD);
        context.globalCompositeOperation = "source-in";
        context.drawImage(halo, 0, 0);
    }
    const rarity = rarityOf(look.quality, look.enchantment, outputs);
    showRarity(drop.querySelector<HTMLElement>("[data-drop-rarity]")!, rarity);
    drop.querySelector("[data-drop-title]")!.textContent = `${WEAPONS[asset]} #${seed}`;
    dropCanvas.setAttribute("aria-label", `A ${rarity} ${asset}, seed ${seed}`);
    const glow = glowOf(outputs);
    const tip = outputs.find((output) => output.name === "tip")?.points[0];
    const rows: [string, string][] = [
        ["file", `${asset}.iprop · ${formatSize(files.get(asset)!.byteLength)}`],
        ["inputs", `seed ${seed} · enchantment ${look.enchantment} · quality ${look.quality}`],
        ["glow", glow && glow.y > 0 ? `power ${glow.y} · sigma ${+glow.w.toFixed(2)} · bloom ${+glow.h.toFixed(2)}` : "none"],
        ["tip", tip ? `${Math.round(tip.x)}, ${Math.round(tip.y)}` : "none"],
    ];
    drop.querySelector("[data-drop-data]")!.replaceChildren(...rows.flatMap(([term, value]) => {
        const dt = document.createElement("dt");
        const dd = document.createElement("dd");
        dt.textContent = term;
        dd.textContent = value;
        return [dt, dd];
    }));
    drop.querySelector("[data-drop-status]")!.textContent = "";
    makeSparks();
    drop.classList.remove("is-spinning");
    history.replaceState(null, "", `#roll/${asset}/${seed}`);
    openModal();
}

async function openCase() {
    if (!drop || !track) return;
    const strip = track.parentElement!;
    spin?.cancel();
    strip.classList.remove("is-done");
    track.replaceChildren();
    if (reducedMotion.matches) {
        const current = await (nextCase ?? prepareCase());
        nextCase = prepareCase();
        showWin(current.winner.asset, current.winner.seed);
        return;
    }
    drop.classList.add("is-spinning");
    openModal();
    const current = await (nextCase ?? prepareCase());
    nextCase = prepareCase();
    if (!drop.open) return;
    track.replaceChildren(...current.items.map((item) => {
        const card = document.createElement("div");
        card.className = "reel__item";
        card.dataset.tier = item.rarity;
        const sprite = document.createElement("div");
        sprite.className = "reel__sprite";
        const canvas = document.createElement("canvas");
        paint(canvas, item.image);
        sprite.append(canvas, item.glow);
        card.append(sprite);
        return card;
    }));
    // Stop with the marker somewhere on the winner, never quite centred.
    const winner = track.children[WIN_INDEX] as HTMLElement;
    const stop = winner.offsetLeft + winner.offsetWidth * (0.5 + (Math.random() - 0.5) * 0.7);
    spin = track.animate(
        [{ transform: "translateX(0)" }, { transform: `translateX(${strip.clientWidth / 2 - stop}px)` }],
        { duration: 6500, easing: "cubic-bezier(0.12, 0.72, 0.1, 1)", fill: "forwards" });
    try {
        await spin.finished;
    } catch {
        return; // closed mid-spin
    }
    winner.classList.add("is-winner");
    strip.classList.add("is-done");
    await new Promise((resolve) => setTimeout(resolve, 1200));
    if (drop.open && drop.classList.contains("is-spinning")) showWin(current.winner.asset, current.winner.seed);
}

function openRollLink() {
    const match = ROLL_LINK.exec(location.hash);
    if (match && Number(match[2]) <= SEED_LIMIT) showWin(match[1], Number(match[2]));
}

if (drop) {
    drop.querySelector("[data-drop-close]")!.addEventListener("click", () => drop.close());
    drop.querySelector("[data-drop-again]")!.addEventListener("click", openCase);
    drop.querySelector("[data-drop-copy]")!.addEventListener("click", async () => {
        const status = drop.querySelector("[data-drop-status]")!;
        try {
            await navigator.clipboard.writeText(location.href);
            status.textContent = "Link copied. Anyone who opens it gets this exact weapon.";
        } catch {
            status.textContent = location.href;
        }
    });
    // A click on the backdrop closes the drop.
    drop.addEventListener("click", (event) => { if (event.target === drop) drop.close(); });
    drop.addEventListener("close", () => {
        spin?.cancel();
        if (ROLL_LINK.test(location.hash)) history.replaceState(null, "", location.pathname + location.search);
    });
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

function drawForest() {
    if (!forest || !files.has("tree")) return;
    const cells = Array.from(forest.querySelectorAll<HTMLElement>(".forest__tree"))
        .filter((cell) => cell.offsetParent !== null);
    if (forestRoll) forestRoll.textContent = `Roll ${cells.length} new trees`;
    for (const cell of cells) {
        const seed = (Math.random() * SEED_LIMIT) >>> 0;
        // The crown and its shadow fill the middle 144 of the 176 canvas.
        const canvas = cell.querySelector("canvas")!;
        canvas.width = canvas.height = 144;
        canvas.getContext("2d")!.putImageData(render("tree", seed).image, -16, -16);
        cell.querySelector("figcaption")!.textContent = `seed ${seed.toString(16).padStart(8, "0")}`;
    }
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
        drawForest();
        if (forestRoll) {
            forestRoll.disabled = false;
            forestRoll.addEventListener("click", drawForest);
        }
        wideForest.addEventListener("change", drawForest);
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
        iprop = await window.createIpropModule({ locateFile: () => "iprop.wasm" });
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

    const rollBox = document.querySelector<HTMLElement>("[data-roll-box]");
    if (rollBox) rollBox.hidden = false;
    document.querySelector("[data-roll]")?.addEventListener("click", openCase);
    window.addEventListener("hashchange", openRollLink);
    openRollLink();
    nextCase = prepareCase();

    setAuto(autoOn);
    autoroll?.addEventListener("click", () => setAuto(!autoOn));
    if (showcase) {
        new IntersectionObserver((entries) => { inView = entries[0].isIntersecting; }).observe(showcase);
    }
    tiles.forEach(schedule);
}

main();
