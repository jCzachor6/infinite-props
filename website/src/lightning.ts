// The animated lightning strike, effects/lightning-strike.iprop, played frame
// by frame. A worker (public/lightning-worker.js) renders the three passes of
// each frame and sends them as soon as they are done; the page draws the view
// picked. The first seed is random and each finished strike hands over to the
// next seed, rendered while the last one played. The seed, the colour and the
// branching restart the strike; the view redraws the frame on screen. Reduced
// motion holds the hero frame until Play.

interface Layers { sprite: ImageBitmap; mask: ImageBitmap; light: ImageBitmap }

interface Strike {
    seed: number;
    // 0 is the hero frame, 1..total the strike, total + 1 the empty end;
    // frames already played are let go.
    frames: (Layers | undefined)[];
    power: number[];
    total: number;
    fps: number;
}

type Reply =
    | { type: "ready"; bytes: number }
    | ({ type: "frame"; id: string; index: number; total: number; fps: number; power: number } & Layers)
    | { type: "error"; message: string };

const SEED_LIMIT = 0xffffffff;
const GAP = 600; // milliseconds of dark between two strikes
const FLOOR_Y = 180; // the effect's origin row, where the bolt hits the floor

function setup(root: HTMLElement) {
    const $ = <T extends Element = HTMLElement>(selector: string) => root.querySelector<T>(selector)!;
    const checked = (name: string) => $<HTMLInputElement>(`input[name=${name}]:checked`).value;
    const canvas = $<HTMLCanvasElement>("[data-strike-canvas]");
    const context = canvas.getContext("2d")!;
    const seedField = $<HTMLInputElement>("[data-strike-seed]");
    const custom = $<HTMLInputElement>("[data-strike-custom]");
    const pause = $<HTMLButtonElement>("[data-strike-pause]");

    let strikes = new Map<string, Strike>();
    let seed = 1 + Math.floor(Math.random() * 99999);
    let current = "";
    let shown = -1;
    let due = 0;
    let paused = matchMedia("(prefers-reduced-motion: reduce)").matches;
    let inView = false;
    let worker: Worker | null = null;
    const nextSeed = (value: number) => (value >= SEED_LIMIT ? 0 : value + 1);

    // The night stage: a dusk sky over a floor of stone flags.
    const stage = document.createElement("canvas");
    stage.width = canvas.width;
    stage.height = canvas.height;
    const paint = stage.getContext("2d")!;
    const sky = paint.createLinearGradient(0, 0, 0, stage.height);
    sky.addColorStop(0, "#0d0e18");
    sky.addColorStop(1, "#171722");
    paint.fillStyle = sky;
    paint.fillRect(0, 0, stage.width, FLOOR_Y);
    paint.fillStyle = "#2b2733";
    paint.fillRect(0, FLOOR_Y, stage.width, stage.height - FLOOR_Y);
    paint.fillStyle = "#18161e";
    for (let y = FLOOR_Y, row = 0; y < stage.height; y += 5, row++) {
        paint.fillRect(0, y + 4, stage.width, 1);
        for (let x = row % 2 ? 5 : 11; x < stage.width; x += 12) paint.fillRect(x, y, 1, 4);
    }
    paint.fillStyle = "#3a3642";
    paint.fillRect(0, FLOOR_Y, stage.width, 1);

    // From the first frame, on a new seed or new inputs. The worker gets the
    // strike on screen and the next seed; asked again for the strike on screen,
    // whose played frames are gone, it renders it afresh.
    function restart() {
        const tint = checked("strike-tint");
        // Transparent lets the seed roll the colour.
        const uniforms = { tint: (tint === "custom" ? custom.value : tint) || "#00000000", branching: Number(checked("strike-branching")) };
        const jobs = [seed, nextSeed(seed)].map((value) => {
            const id = `${value}|${uniforms.tint}|${uniforms.branching}`;
            return { id, seed: value, uniforms, fresh: id === current };
        });
        strikes = new Map(jobs.map((job) => [job.id,
            (!job.fresh && strikes.get(job.id)) || { seed: job.seed, frames: [], power: [], total: 0, fps: 0 }]));
        current = jobs[0].id;
        worker?.postMessage({ type: "plan", jobs });
        shown = -1;
        due = 0;
        if (document.activeElement !== seedField) seedField.value = String(seed);
    }

    function setSeed(value: number) {
        seed = Math.min(SEED_LIMIT, Math.max(0, value));
        restart();
    }

    function draw(strike: Strike, index: number) {
        const frame = strike.frames[index]!;
        const view = checked("strike-view");
        if (view === "mask" || view === "light") {
            // One pass alone, on black.
            context.fillStyle = "#000";
            context.fillRect(0, 0, canvas.width, canvas.height);
            context.drawImage(frame[view], 0, 0);
        } else {
            // The stage, plus the baked light (colour x alpha) when lit, then the sprite.
            context.drawImage(stage, 0, 0);
            if (view === "lit") {
                context.globalCompositeOperation = "lighter";
                context.drawImage(frame.light, 0, 0);
                context.globalCompositeOperation = "source-over";
            }
            context.drawImage(frame.sprite, 0, 0);
        }
        // A played frame is not shown again; the hero frame stays for a pause.
        if (shown >= 1 && shown !== index) strike.frames[shown] = undefined;
        shown = index;
        if (document.activeElement !== seedField) seedField.value = String(strike.seed);
        $<HTMLOutputElement>("[data-strike-frame]").value = index ? `${Math.min(index, strike.total)} / ${strike.total}` : "hero";
        $<HTMLOutputElement>("[data-strike-power]").value = strike.power[index].toFixed(1);
        canvas.setAttribute("aria-label", `A lightning strike, seed ${strike.seed}`);
    }

    function tick(now: number) {
        requestAnimationFrame(tick);
        const strike = strikes.get(current)!;
        if (paused || !inView || document.hidden) {
            // A stage that has not played yet shows the hero frame.
            if (shown < 0 && strike.frames[0]) draw(strike, 0);
            due = now;
            return;
        }
        if (now < due) return;
        const next = shown < 1 ? 1 : shown + 1;
        // The strike ended and the gap is over: the next seed.
        if (next > strike.total + 1) return setSeed(nextSeed(seed));
        // Not rendered yet: hold the frame on screen.
        if (!strike.frames[next]) return;
        draw(strike, next);
        const step = next > strike.total ? GAP : 1000 / strike.fps;
        due = due + step < now ? now + step : due + step;
    }

    function receive(reply: Reply) {
        if (reply.type === "ready") {
            for (const size of Array.from(root.querySelectorAll("[data-strike-size]"))) size.textContent = `${(reply.bytes / 1024).toFixed(1)} KB`;
        } else if (reply.type === "error") {
            const status = $("[data-strike-status]");
            status.textContent = "The lightning could not be rendered: " + reply.message;
            status.hidden = false;
        } else {
            // A frame of a strike that left the plan on its way here is dropped.
            const strike = strikes.get(reply.id);
            if (!strike) return;
            strike.total = reply.total;
            strike.fps = reply.fps;
            strike.frames[reply.index] = { sprite: reply.sprite, mask: reply.mask, light: reply.light };
            strike.power[reply.index] = reply.power;
        }
    }

    function start() {
        const url = new URL("lightning-worker.js", document.baseURI);
        // Refresh the worker together with the hashed bundle after a deployment.
        url.searchParams.set("v", import.meta.url.split("/").pop()!);
        worker = new Worker(url);
        worker.onmessage = (event: MessageEvent<Reply>) => receive(event.data);
        worker.onerror = () => receive({ type: "error", message: "the interpreter could not start" });
        worker.postMessage({ type: "init", file: "effects/lightning-strike.iprop" });
        restart();
        requestAnimationFrame(tick);
    }

    function setPaused(value: boolean) {
        paused = value;
        pause.textContent = value ? "Play" : "Pause";
    }

    root.addEventListener("change", (event) => {
        const target = event.target as HTMLInputElement;
        if (target === seedField) {
            // Any seed from 0 to 2^32 - 1; anything else puts the seed back.
            const text = seedField.value.trim();
            if (/^\d{1,10}$/.test(text) && Number(text) <= SEED_LIMIT) setSeed(Number(text));
            else seedField.value = String(seed);
        } else if (target.name === "strike-view") {
            const strike = strikes.get(current);
            if (strike?.frames[shown]) draw(strike, shown);
        } else {
            // The picker takes over as soon as a colour is picked.
            if (target === custom) $<HTMLInputElement>("input[name=strike-tint][value=custom]").checked = true;
            restart();
        }
    });
    for (const button of Array.from(root.querySelectorAll<HTMLButtonElement>("[data-strike-step]"))) {
        button.addEventListener("click", () => setSeed(seed + Number(button.dataset.strikeStep)));
    }
    pause.addEventListener("click", () => setPaused(!paused));
    setPaused(paused);
    seedField.value = String(seed);

    // The worker starts when the section first shows; the strike plays only while it is on screen.
    new IntersectionObserver((entries) => {
        inView = entries[entries.length - 1].isIntersecting;
        if (inView && !worker) start();
    }).observe(root);
}

const root = document.querySelector<HTMLElement>("[data-strike]");
if (root) setup(root);

export {};
