// The three .iprop programs run in a worker. Five canvas layers make up the
// butterfly on the page: each wing and its mirror, then the body's right
// half and its mirror. Only CSS transforms flap the wings during flight.

type PartName = "forewing" | "hindwing" | "body";
interface PartImage {
    width: number;
    height: number;
    data: Uint8ClampedArray;
    fuel: { used: number; budget: number; remaining: number };
}
interface ButterflyFrame {
    type: "generated";
    id: number;
    seed: number;
    rootY: number;
    milliseconds: number;
    style: number;
    scheme: number;
    parts: Record<PartName, PartImage>;
}
type WorkerReply = ButterflyFrame | { type: "ready"; parts: { id: PartName; bytes: number }[] }
    | { type: "error"; id?: number; message: string };
const PARTS: PartName[] = ["forewing", "hindwing", "body"];
const RUN_LENGTH = 50;
const number = new Intl.NumberFormat("en-US");
const motion = window.matchMedia("(prefers-reduced-motion: reduce)");
const kilobytes = (bytes: number) => `${(bytes / 1024).toFixed(1)} KB`;

class ButterflyBenchmark {
    readonly stage: HTMLElement;
    readonly flock: HTMLElement;
    readonly button: HTMLButtonElement;
    readonly autoButton: HTMLButtonElement;
    readonly actions: HTMLElement;
    readonly status: HTMLElement;
    readonly state: HTMLElement;
    worker: Worker | null = null;
    initialised: Promise<void> | null = null;
    readyResolve: (() => void) | null = null;
    readyReject: ((error: Error) => void) | null = null;
    pending = new Map<number, { resolve(frame: ButterflyFrame): void; reject(error: Error): void }>();
    nextId = 0;
    preview: HTMLElement | null = null;
    running = false;
    hasCompleted = false;
    autoRolling = false;
    failed = false;
    disposed = false;
    completed = 0;
    totalTime = 0;
    totals: Record<PartName, number> = { forewing: 0, hindwing: 0, body: 0 };
    flights = new Set<Promise<void>>();
    snapshots = new WeakMap<HTMLElement, HTMLCanvasElement>();
    seed = crypto.getRandomValues(new Uint32Array(1))[0];

    constructor(readonly element: HTMLElement) {
        this.stage = element.querySelector("[data-butterfly-stage]")!;
        this.flock = element.querySelector("[data-butterfly-flock]")!;
        this.button = element.querySelector("[data-butterfly-run]")!;
        this.autoButton = element.querySelector("[data-butterfly-auto]")!;
        this.actions = element.querySelector("[data-butterfly-actions]")!;
        this.status = element.querySelector("[data-butterfly-status]")!;
        this.state = element.querySelector("[data-butterfly-state]")!;
        this.button.addEventListener("click", () => { void this.run(); });
        this.autoButton.addEventListener("click", () => this.toggleAutoRoll());
        window.addEventListener("pagehide", () => {
            this.disposed = true;
            this.autoRolling = false;
            this.worker?.terminate();
            this.rejectPending(new Error("Page closed."));
        });
        // Avoid competing with the hero on first load; prepare just before
        // the visitor reaches the section, and keep the first butterfly ready.
        const observer = new IntersectionObserver(entries => {
            if (!entries.some(entry => entry.isIntersecting)) return;
            observer.disconnect();
            void this.loadPreview().catch(error => this.showError(error));
        }, { rootMargin: "400px" });
        observer.observe(element);
    }

    randomSeed() { this.seed = (this.seed + 1) >>> 0; return this.seed; }

    rejectPending(error: Error) {
        this.readyReject?.(error);
        this.readyReject = null;
        this.readyResolve = null;
        for (const pending of this.pending.values()) pending.reject(error);
        this.pending.clear();
    }

    initialise() {
        this.worker?.terminate();
        const workerUrl = new URL("butterfly-worker.js", document.baseURI);
        // Public scripts may be cached separately by the host. Refresh the
        // worker together with the hashed application bundle after deployment.
        workerUrl.searchParams.set("v", import.meta.url.split("/").pop()!);
        this.worker = new Worker(workerUrl);
        this.initialised = new Promise<void>((resolve, reject) => {
            this.readyResolve = resolve;
            this.readyReject = reject;
        });
        this.worker.addEventListener("message", (event: MessageEvent<WorkerReply>) => {
            const reply = event.data;
            if (reply.type === "ready") {
                let total = 0;
                for (const part of reply.parts) {
                    total += part.bytes;
                    const label = this.element.querySelector(`[data-butterfly-size="${part.id}"]`);
                    if (label) { label.textContent = kilobytes(part.bytes); label.setAttribute("title", `${number.format(part.bytes)} bytes`); }
                }
                this.element.querySelector("[data-butterfly-total-size]")!.textContent = kilobytes(total);
                this.readyResolve?.();
                this.readyResolve = this.readyReject = null;
            } else if (reply.type === "generated") {
                const request = this.pending.get(reply.id);
                this.pending.delete(reply.id);
                request?.resolve(reply);
            } else {
                const error = new Error(reply.message);
                if (reply.id !== undefined && this.pending.has(reply.id)) {
                    this.pending.get(reply.id)!.reject(error);
                    this.pending.delete(reply.id);
                } else this.rejectPending(error);
            }
        });
        this.worker.addEventListener("error", () => {
            this.rejectPending(new Error("The butterfly interpreter could not start."));
        });
        this.worker.postMessage({ type: "init" });
        return this.initialised;
    }

    async generate() {
        await this.initialised;
        const id = ++this.nextId;
        const promise = new Promise<ButterflyFrame>((resolve, reject) => this.pending.set(id, { resolve, reject }));
        this.worker!.postMessage({ type: "generate", id, seed: this.randomSeed() });
        return promise;
    }

    async loadPreview() {
        this.button.disabled = true;
        this.failed = false;
        this.state.textContent = "Loading";
        await this.initialise();
        const frame = await this.generate();
        this.preview?.remove();
        this.preview = this.makeButterfly(frame, true);
        this.element.querySelector<HTMLElement>("[data-butterfly-loading]")!.hidden = true;
        this.updateControls();
        this.state.textContent = "Ready";
        this.status.textContent = "Ready. Release fifty new butterflies.";
    }

    canvas(part: PartImage) {
        const canvas = document.createElement("canvas");
        canvas.width = part.width;
        canvas.height = part.height;
        canvas.getContext("2d")!.putImageData(new ImageData(new Uint8ClampedArray(part.data), part.width, part.height), 0, 0);
        return canvas;
    }

    makeButterfly(frame: ButterflyFrame, centered = false) {
        const butterfly = document.createElement("div");
        butterfly.className = "butterfly-bird";
        butterfly.style.left = centered ? "50%" : `${50 + (Math.random() * 2 - 1) * 25}%`;
        butterfly.setAttribute("aria-hidden", "true");
        butterfly.dataset.seed = String(frame.seed);
        const rig = document.createElement("div");
        rig.className = "butterfly-bird__rig";
        const snapshot = document.createElement("canvas");
        snapshot.width = snapshot.height = 256;
        const snapshotContext = snapshot.getContext("2d")!;
        for (const name of ["hindwing", "forewing"] as const) {
            const source = this.canvas(frame.parts[name]);
            for (const side of ["left", "right"]) {
                const wing = document.createElement("canvas");
                wing.width = source.width;
                wing.height = source.height;
                wing.className = `butterfly-bird__wing butterfly-bird__wing--${side} butterfly-bird__wing--${name}`;
                const context = wing.getContext("2d")!;
                if (side === "left") { context.translate(source.width, 0); context.scale(-1, 1); }
                context.drawImage(source, 0, 0);
                snapshotContext.drawImage(wing, side === "left" ? 0 : 128, 0);
                rig.append(this.colorGlow(wing), wing);
            }
        }
        const source = this.canvas(frame.parts.body);
        const body = document.createElement("canvas");
        body.width = body.height = 256;
        body.className = "butterfly-bird__body";
        const context = body.getContext("2d")!;
        const y = Math.round(frame.rootY) - 36;
        context.drawImage(source, 20, 0, source.width - 20, source.height, 128, y, source.width - 20, source.height);
        context.translate(256, 0);
        context.scale(-1, 1);
        context.drawImage(source, 20, 0, source.width - 20, source.height, 128, y, source.width - 20, source.height);
        snapshotContext.drawImage(body, 0, 0);
        rig.append(this.colorGlow(body), body);
        if (centered) {
            // A wider halo remains visible beyond the opaque resting wings.
            const halo = document.createElement("canvas");
            halo.width = halo.height = 256;
            halo.className = "butterfly-bird__idle-glow";
            halo.getContext("2d")!.drawImage(snapshot, 0, 0);
            butterfly.append(halo);
        }
        butterfly.append(rig);
        this.snapshots.set(butterfly, snapshot);
        this.flock.append(butterfly);
        this.element.querySelector("[data-butterfly-seed]")!.textContent = `seed ${frame.seed}`;
        return butterfly;
    }

    colorGlow(source: HTMLCanvasElement) {
        const mask = document.createElement("canvas");
        mask.width = source.width;
        mask.height = source.height;
        mask.className = source.className + " butterfly-bird__mask";
        const context = mask.getContext("2d")!;
        context.drawImage(source, 0, 0);
        return mask;
    }

    leaveTrail(butterfly: HTMLElement) {
        const snapshot = this.snapshots.get(butterfly);
        if (!snapshot || !butterfly.isConnected) return;
        const trail = document.createElement("canvas");
        trail.width = trail.height = 256;
        trail.className = "butterfly-bird butterfly-trail";
        trail.setAttribute("aria-hidden", "true");
        trail.style.width = `${butterfly.offsetWidth}px`;
        trail.style.left = butterfly.style.left;
        trail.style.transform = getComputedStyle(butterfly).transform;
        const context = trail.getContext("2d")!;
        context.drawImage(snapshot, 0, 0);
        this.flock.prepend(trail);
        const fade = trail.animate([{ opacity: 0.12 }, { opacity: 0 }],
            { duration: 340, easing: "ease-out", fill: "forwards" });
        void fade.finished.catch(() => {}).then(() => trail.remove());
    }

    fly(butterfly: HTMLElement) {
        butterfly.classList.add("is-flying");
        const stage = this.stage.getBoundingClientRect();
        const direction = Math.random() * 2 - 1;
        const angle = direction * 70 * Math.PI / 180;
        const dy = -(stage.height * 0.8 + butterfly.offsetHeight);
        const dx = -dy * Math.tan(angle);
        const previousDx = direction * stage.width * 0.85;
        const turn = Math.atan2(dx, -dy) * 180 / Math.PI;
        const transform = (x: number, y: number, angle: number, scale: number) =>
            `translate(calc(-50% + ${x}px), calc(-50% + ${y}px)) rotate(${angle}deg) scale(${scale})`;

        // Preserve the old flight's peak translation speed, with just a 5%
        // lift. Its take-off phase now reaches cruise three times sooner.
        const previousSeconds = 1.55 + Math.random() * 0.35;
        const easing = (time: number) => {
            let lo = 0, hi = 1;
            for (let i = 0; i < 14; ++i) {
                const u = (lo + hi) * 0.5;
                const x = 3 * (1 - u) ** 2 * u * 0.42 + 3 * (1 - u) * u ** 2 * 0.72 + u ** 3;
                if (x < time) lo = u; else hi = u;
            }
            const u = (lo + hi) * 0.5;
            return 3 * (1 - u) * u ** 2 * 0.74 + u ** 3;
        };
        const previousPosition = (progress: number) => {
            if (progress < 0.18) return { x: previousDx * 0.08 * progress / 0.18, y: -20 * progress / 0.18 };
            if (progress < 0.64) {
                const t = (progress - 0.18) / 0.46;
                return { x: previousDx * (0.08 + 0.42 * t), y: -20 + (dy * 0.42 + 20) * t };
            }
            const t = (progress - 0.64) / 0.36;
            return { x: previousDx * (0.5 + 0.5 * t), y: dy * (0.42 + 0.58 * t) };
        };
        let peak = 0, takeoff = previousSeconds * 0.36;
        let last = { x: 0, y: 0 };
        for (let i = 1; i <= 120; ++i) {
            const progress = easing(i / 120);
            if (progress < 0.18) takeoff = i / 120 * previousSeconds;
            const position = previousPosition(progress);
            peak = Math.max(peak, Math.hypot(position.x - last.x, position.y - last.y) * 120 / previousSeconds);
            last = position;
        }
        const maxSpeed = peak * 1.05;
        const takeoffSeconds = takeoff / 3;
        const acceleration = maxSpeed / takeoffSeconds;
        const accelerateDistance = 0.5 * maxSpeed * takeoffSeconds;
        const timeAt = (distance: number) => distance < accelerateDistance
            ? Math.sqrt(2 * distance / acceleration)
            : takeoffSeconds + (distance - accelerateDistance) / maxSpeed;

        // Arc-length timing keeps the speed limit through the curved path.
        const points = [{ x: 0, y: 0, distance: 0 }];
        let distance = 0;
        for (let i = 1; i <= 96; ++i) {
            const t = i / 96, s = 1 - t;
            const x = 3 * s * s * t * dx * 0.08 + 3 * s * t * t * dx * 0.48 + t * t * t * dx;
            const y = 3 * s * s * t * dy * 0.18 + 3 * s * t * t * dy * 0.55 + t * t * t * dy;
            const previous = points[points.length - 1];
            distance += Math.hypot(x - previous.x, y - previous.y);
            points.push({ x, y, distance });
        }
        const seconds = timeAt(distance);
        const frames = points.map((point, index) => {
            const progress = point.distance / distance;
            return { transform: transform(point.x, point.y, turn * Math.min(1, progress * 4), 1 - progress * 0.34),
                opacity: progress < 0.86 ? 1 : (1 - progress) / 0.14,
                offset: index === points.length - 1 ? 1 : timeAt(point.distance) / seconds };
        });
        const animation = butterfly.animate(motion.matches ? [{ opacity: 1 }, { opacity: 0 }] : frames,
            { duration: motion.matches ? 140 : seconds * 1000, easing: "linear", fill: "forwards" });
        const trailTimer = motion.matches ? null : window.setInterval(() => this.leaveTrail(butterfly), 110);
        let flight: Promise<void>;
        flight = animation.finished.catch(() => {}).then(() => {
            if (trailTimer !== null) window.clearInterval(trailTimer);
            butterfly.remove();
            this.flights.delete(flight);
        });
        this.flights.add(flight);
    }

    update(frame?: ButterflyFrame) {
        if (frame) {
            ++this.completed;
            this.totalTime += frame.milliseconds;
            for (const name of PARTS) this.totals[name] += frame.parts[name].fuel.used;
        }
        this.element.querySelector("[data-butterfly-count]")!.textContent = String(this.completed);
        this.element.querySelector("[data-butterfly-average]")!.textContent = this.completed ? (this.totalTime / this.completed).toFixed(1) : "—";
        for (const name of PARTS) this.element.querySelector(`[data-butterfly-fuel="${name}"]`)!.textContent =
            this.completed ? number.format(Math.round(this.totals[name] / this.completed)) : "—";
        const progress = this.element.querySelector<HTMLElement>("[data-butterfly-progress]")!;
        progress.setAttribute("aria-valuenow", String(this.completed));
        progress.style.setProperty("--progress", `${this.completed / RUN_LENGTH * 100}%`);
    }

    updateControls() {
        this.actions.classList.toggle("is-split", this.hasCompleted);
        this.button.querySelector("[data-butterfly-run-label]")!.textContent =
            this.hasCompleted || this.failed ? "Try again" : "Benchmark 50x";
        this.button.disabled = this.running || this.disposed;
        this.autoButton.hidden = !this.hasCompleted;
        this.autoButton.disabled = this.disposed || this.failed || (this.running && !this.autoRolling);
        this.autoButton.setAttribute("aria-pressed", String(this.autoRolling));
        this.autoButton.title = this.autoRolling ? "Stop automatic benchmarks" : "Start automatic benchmarks";
    }

    toggleAutoRoll() {
        if (this.autoButton.disabled || this.disposed) return;
        this.autoRolling = !this.autoRolling;
        this.updateControls();
        if (this.autoRolling) {
            void this.run(true);
        } else if (this.running) {
            this.state.textContent = "Stopping";
            this.status.textContent = "Auto roll will stop after the current butterfly.";
        }
    }

    async run(automatic = false): Promise<void> {
        if (this.running || this.disposed) return;
        this.running = true;
        this.updateControls();
        try {
            if (this.failed) await this.loadPreview();
            if (this.disposed) return;
            this.element.classList.add("is-running");
            this.state.textContent = this.autoRolling ? "Auto roll" : "Running";
            this.completed = this.totalTime = 0;
            this.totals = { forewing: 0, hindwing: 0, body: 0 };
            this.update();
            if (this.preview) { this.fly(this.preview); this.preview = null; }
            for (let i = 0; i < RUN_LENGTH; ++i) {
                if (automatic && !this.autoRolling) break;
                const frame = await this.generate();
                if (this.disposed) return;
                const butterfly = this.makeButterfly(frame);
                this.update(frame);
                this.status.textContent = automatic && !this.autoRolling
                    ? "Auto roll stopped. The remaining butterflies are finishing their flights."
                    : `${this.autoRolling ? "Auto roll · " : ""}Generated ${this.completed} of ${RUN_LENGTH}.`;
                this.fly(butterfly);
            }
            const finished = this.completed === RUN_LENGTH;
            // Automatic batches continue as soon as their last butterfly is
            // rendered; existing flights stay alive across the batch boundary.
            if (!this.autoRolling) await Promise.all(this.flights);
            if (this.disposed) return;
            if (finished) {
                this.hasCompleted = true;
                this.state.textContent = "Complete";
                this.status.textContent = this.autoRolling
                    ? `${RUN_LENGTH} butterflies generated. Auto roll continues with a fresh batch.`
                    : `${RUN_LENGTH} butterflies generated. Try again or turn on Auto roll.`;
            } else {
                this.state.textContent = "Stopped";
                this.status.textContent = `Auto roll stopped after ${this.completed} of ${RUN_LENGTH} butterflies.`;
            }
        } catch (error) {
            this.showError(error);
        } finally {
            this.running = false;
            this.element.classList.remove("is-running");
            this.updateControls();
            if (this.autoRolling && !this.disposed && !this.failed) void this.run(true);
        }
    }

    showError(error: unknown) {
        if (this.disposed) return;
        this.failed = true;
        this.autoRolling = false;
        this.state.textContent = "Try again";
        this.status.textContent = error instanceof Error ? error.message : String(error);
        this.element.querySelector<HTMLElement>("[data-butterfly-loading]")!.hidden = true;
        this.updateControls();
    }
}

const section = document.querySelector<HTMLElement>("[data-butterfly-benchmark]");
if (section) new ButterflyBenchmark(section);
