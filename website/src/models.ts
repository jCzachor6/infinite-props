// The models section: one .iprop3d, a room of chairs. A worker generates a
// chair for every seed with the model module (public/iprop3d.wasm) and the
// page draws them with three.js: each chair turns slowly and a lamp follows
// the pointer. This file is a chunk of its own, loaded together with
// three.js only when the section comes near the screen.

// Named imports, and for the adapter an object of exactly what it uses, so
// that the bundler leaves the rest of three.js out of the chunk.
import {
    ACESFilmicToneMapping, BufferAttribute, BufferGeometry, ClampToEdgeWrapping, Color, DataTexture, DirectionalLight,
    DoubleSide, Fog, FrontSide, Group, HemisphereLight, LinearFilter, LinearMipmapLinearFilter, MathUtils, Mesh,
    MeshStandardMaterial, NearestFilter, NoColorSpace, PCFShadowMap, PerspectiveCamera, Plane, PlaneGeometry, Raycaster,
    RepeatWrapping, RGBAFormat, Scene, SpotLight, SRGBColorSpace, UnsignedByteType, Vector2, Vector3, WebGLRenderer,
    type Object3D,
} from "three";
import { disposeObject3D, toObject3D, type ModelResult } from "../../backends/html5/js/three-adapter.js";

const THREE = {
    BufferAttribute, BufferGeometry, ClampToEdgeWrapping, Color, DataTexture, DoubleSide, FrontSide, Group, LinearFilter,
    LinearMipmapLinearFilter, Mesh, MeshStandardMaterial, NearestFilter, NoColorSpace, RepeatWrapping, RGBAFormat,
    SRGBColorSpace, UnsignedByteType,
};

type Reply =
    | { type: "ready" }
    | { type: "loaded"; model: string }
    | { type: "result"; model: string; request: number; result: ModelResult; milliseconds: number }
    | { type: "error"; model?: string; request?: number; message: string };

const CHAIRS = 6;
const SEED_LIMIT = 0xffffffff;
// Metres between chairs across the room and between its rows.
const ACROSS = 0.84;
const DEEP = 1.1;
const BACKGROUND = 0x1b2128;

const formatSize = (bytes: number) => (bytes < 1024 ? `${bytes} B` : `${(bytes / 1024).toFixed(1)} KB`);

export function startModels(root: HTMLElement) {
    const $ = <T extends Element = HTMLElement>(selector: string) => root.querySelector<T>(selector)!;
    const canvas = $<HTMLCanvasElement>("[data-models-canvas]");
    const stage = canvas.parentElement!;
    const labels = $<HTMLOListElement>("[data-models-seeds]");
    const roll = $<HTMLButtonElement>("[data-models-roll]");
    const status = $<HTMLElement>("[data-models-status]");
    const checked = (name: string) => Number(root.querySelector<HTMLInputElement>(`input[name=${name}]:checked`)?.value ?? 0);
    const still = matchMedia("(prefers-reduced-motion: reduce)");

    function fail(message: string) {
        status.textContent = message;
        status.hidden = false;
    }

    let renderer: WebGLRenderer;
    try {
        renderer = new WebGLRenderer({ canvas, antialias: true });
    } catch {
        fail("This browser cannot draw 3D here, so the chairs stay away.");
        return;
    }
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    renderer.shadowMap.enabled = true;
    renderer.shadowMap.type = PCFShadowMap;
    renderer.toneMapping = ACESFilmicToneMapping;

    // ---- the room: a floor that fades into the dark, soft sky light and a lamp ----
    const scene = new Scene();
    scene.background = new Color(BACKGROUND);
    scene.fog = new Fog(BACKGROUND, 6, 15);
    // Enough light everywhere to read a chair's colour; the lamp adds the drama.
    scene.add(new HemisphereLight(0xe4e9f5, 0x2c2822, 1.05));
    const fill = new DirectionalLight(0xfff4e6, 0.6);
    fill.position.set(-2, 3, 4);
    scene.add(fill);

    const floor = new Mesh(new PlaneGeometry(40, 40),
        new MeshStandardMaterial({ color: 0x2a323c, roughness: 0.92, metalness: 0 }));
    floor.rotation.x = -Math.PI / 2;
    floor.receiveShadow = true;
    scene.add(floor);

    const lamp = new SpotLight(0xffe0b0, 85, 14, 0.82, 0.75, 1.5);
    lamp.castShadow = true;
    lamp.shadow.mapSize.set(2048, 2048);
    lamp.shadow.radius = 4;
    lamp.shadow.bias = -0.0005;
    lamp.shadow.normalBias = 0.012;
    lamp.shadow.camera.near = 0.3;
    lamp.shadow.camera.far = 12;
    scene.add(lamp, lamp.target);

    const camera = new PerspectiveCamera(26, 1, 0.1, 50);

    // ---- the chairs: one turning holder per seed ----
    interface Chair { holder: Group; object: Object3D | null; label: HTMLLIElement; seed: number; spin: number }
    const chairs: Chair[] = [];
    for (let i = 0; i < CHAIRS; ++i) {
        const holder = new Group();
        scene.add(holder);
        const label = document.createElement("li");
        labels.append(label);
        // Each chair starts at its own angle, three quarters toward the viewer.
        chairs.push({ holder, object: null, label, seed: 0, spin: 0.55 + ((i * 2.4) % 1.3) - 0.6 });
    }

    // The room has one row on a wide stage and more rows on a narrow one. The
    // camera then stands back until every chair is inside the picture: the
    // corners of the space each chair turns in are projected and the camera
    // moves until they fill the stage.
    const centre = new Vector3();
    const corner = new Vector3();
    function arrange() {
        const width = stage.clientWidth, height = Math.max(1, stage.clientHeight);
        renderer.setSize(width, height, false);
        const aspect = width / height;
        const columns = aspect >= 2.1 ? 6 : aspect >= 1.05 ? 3 : 2;
        const rows = CHAIRS / columns;
        chairs.forEach((chair, i) => {
            const column = i % columns, row = Math.floor(i / columns);
            // Rows behind the first stand in the gaps of the row before.
            const shift = rows > 1 && row % 2 === 1 ? 0.5 : 0;
            chair.holder.position.set((column - (columns - 1) / 2 + shift * (columns > 2 ? 1 : 0.6)) * ACROSS, 0, -row * DEEP);
        });
        // Seeds are shown under one row; in a deeper room they would cover chairs.
        labels.hidden = rows > 1;

        const pitch = 0.3 + 0.09 * (rows - 1);
        const half = Math.tan(MathUtils.degToRad(camera.fov / 2));
        centre.set(0, 0.5, -(rows - 1) * DEEP / 2);
        let distance = 6;
        camera.aspect = aspect;
        camera.updateProjectionMatrix();
        // One row leaves room for the seeds below it.
        const top = 0.86, bottom = rows > 1 ? -0.88 : -0.7, side = 0.92;
        for (let pass = 0; pass < 4; ++pass) {
            camera.position.set(centre.x, centre.y + Math.sin(pitch) * distance, centre.z + Math.cos(pitch) * distance);
            camera.lookAt(centre);
            camera.updateMatrixWorld();
            let left = Infinity, right = -Infinity, low = Infinity, high = -Infinity;
            for (const chair of chairs) {
                for (let k = 0; k < 8; ++k) {
                    corner.set(k & 1 ? 0.36 : -0.36, k & 2 ? 1.02 : 0, k & 4 ? 0.36 : -0.36).add(chair.holder.position).project(camera);
                    left = Math.min(left, corner.x); right = Math.max(right, corner.x);
                    low = Math.min(low, corner.y); high = Math.max(high, corner.y);
                }
            }
            // Closer or farther until the room fills the stage, then the
            // room's middle to the middle of the space left for it.
            distance *= Math.max((right - left) / (2 * side), (high - low) / (top - bottom));
            centre.x += (left + right) / 2 * distance * half * aspect;
            centre.y += ((low + high) / 2 - (top + bottom) / 2) * distance * half;
        }
        camera.position.set(centre.x, centre.y + Math.sin(pitch) * distance, centre.z + Math.cos(pitch) * distance);
        camera.lookAt(centre);
        // The lamp and the fog follow the room, not the camera's aim.
        centre.set(chairs.reduce((sum, chair) => sum + chair.holder.position.x, 0) / CHAIRS, 0.44, -(rows - 1) * DEEP / 2);
        (scene.fog as Fog).near = distance + 1.5;
        (scene.fog as Fog).far = distance + 9;
        placeLabels();
        aimLamp(pointer);
    }

    // A seed under each chair, where its feet meet the floor on screen.
    const spot = new Vector3();
    function placeLabels() {
        const width = stage.clientWidth, height = stage.clientHeight;
        camera.updateMatrixWorld();
        for (const chair of chairs) {
            spot.copy(chair.holder.position).add(new Vector3(0, -0.02, 0.36)).project(camera);
            chair.label.style.transform =
                `translate(-50%, 0) translate(${((spot.x + 1) / 2 * width).toFixed(1)}px, ${((1 - spot.y) / 2 * height).toFixed(1)}px)`;
        }
    }

    // ---- the lamp: above the floor under the pointer, or resting over the room ----
    let pointer: { x: number; y: number } | null = null;
    const ray = new Raycaster();
    const ground = new Plane(new Vector3(0, 1, 0), 0);
    const lampGoal = new Vector3(), lampAim = new Vector3(), hit = new Vector3();
    function aimLamp(at: { x: number; y: number } | null) {
        pointer = at;
        if (at) {
            ray.setFromCamera(new Vector2(at.x, at.y), camera);
            if (ray.ray.intersectPlane(ground, hit)) {
                hit.x = MathUtils.clamp(hit.x, centre.x - 4, centre.x + 4);
                hit.z = MathUtils.clamp(hit.z, centre.z - 3, centre.z + 2.5);
                lampAim.set(hit.x, 0.35, hit.z);
                // A little toward the viewer, so the lit side faces the camera.
                lampGoal.set(hit.x, 2.9, hit.z + 1.3);
                return;
            }
        }
        lampAim.set(centre.x, 0.35, centre.z);
        lampGoal.set(centre.x - 1.4, 3.0, centre.z + 1.7);
    }
    const fromEvent = (event: PointerEvent) => {
        const box = canvas.getBoundingClientRect();
        aimLamp({ x: (event.clientX - box.left) / box.width * 2 - 1, y: 1 - (event.clientY - box.top) / box.height * 2 });
    };
    canvas.addEventListener("pointermove", fromEvent);
    canvas.addEventListener("pointerdown", fromEvent);
    canvas.addEventListener("pointerleave", () => aimLamp(null));
    canvas.addEventListener("pointercancel", () => aimLamp(null));

    // ---- generation: a worker answers each seed; stale answers are dropped ----
    const base = new URL(".", document.baseURI);
    const url = (name: string) => new URL(name, base).href;
    const worker = new Worker(url("model-worker.js"));
    let rolled = 0, pending = 0;
    worker.postMessage({ type: "init", scripts: [url("iprop3d.js"), url("api3d.js")], wasm: url("iprop3d.wasm") });

    function generate(fresh: boolean) {
        ++rolled;
        pending = CHAIRS;
        const uniforms = { finish: checked("models-finish"), fabric: checked("models-fabric") };
        chairs.forEach((chair, slot) => {
            if (fresh || chair.seed === 0) chair.seed = 1 + Math.floor(Math.random() * 99999);
            worker.postMessage({ type: "generate", model: "chair", request: rolled * CHAIRS + slot,
                options: { seed: Math.min(chair.seed, SEED_LIMIT), uniforms } });
        });
    }

    function show(slot: number, result: ModelResult) {
        const chair = chairs[slot];
        if (chair.object) { chair.holder.remove(chair.object); disposeObject3D(chair.object); }
        // Without a shared cache the chair owns its textures and releases them with itself.
        chair.object = toObject3D(result, THREE, { anisotropy: renderer.capabilities.getMaxAnisotropy() });
        chair.holder.add(chair.object);
        chair.label.textContent = `seed ${chair.seed}`;
    }

    worker.onmessage = async ({ data }: MessageEvent<Reply>) => {
        if (data.type === "ready") {
            try {
                const response = await fetch(url("models/chair.iprop3d"));
                if (!response.ok) throw new Error(`models/chair.iprop3d: HTTP ${response.status}`);
                const bytes = new Uint8Array(await response.arrayBuffer());
                for (const size of Array.from(root.querySelectorAll("[data-models-size]"))) size.textContent = formatSize(bytes.byteLength);
                worker.postMessage({ type: "load", model: "chair", bytes }, [bytes.buffer]);
            } catch (error) {
                fail("The chair could not be loaded: " + (error as Error).message);
            }
        } else if (data.type === "loaded") {
            generate(true);
            roll.disabled = false;
        } else if (data.type === "result") {
            if (Math.floor(data.request / CHAIRS) !== rolled) return;
            show(data.request % CHAIRS, data.result);
            if (--pending === 0) status.hidden = true;
        } else if (data.type === "error") {
            fail("The chairs could not be generated: " + data.message);
        }
    };
    worker.onerror = () => fail("The chairs could not be generated in this browser.");

    roll.textContent = `Roll ${CHAIRS} new chairs`;
    roll.addEventListener("click", () => generate(true));
    // Another finish or fabric keeps the seeds: the same chairs, dressed anew.
    for (const input of Array.from(root.querySelectorAll<HTMLInputElement>("input[type=radio]")))
        input.addEventListener("change", () => generate(false));

    // ---- drawing: only while the section is on screen ----
    let last = 0;
    function frame(now: number) {
        const elapsed = Math.min(0.1, (now - last) / 1000 || 0);
        last = now;
        if (!still.matches) {
            for (const chair of chairs) chair.spin += elapsed * 0.3;
            // Without a pointer the lamp sways over the room.
            if (!pointer) lampGoal.x = centre.x - 1.4 + Math.sin(now / 2600) * 1.6;
        }
        for (const chair of chairs) chair.holder.rotation.y = chair.spin;
        const ease = 1 - Math.exp(-elapsed * 9);
        lamp.position.lerp(lampGoal, still.matches ? 1 : ease);
        lamp.target.position.lerp(lampAim, still.matches ? 1 : ease);
        renderer.render(scene, camera);
    }
    aimLamp(null);
    lamp.position.copy(lampGoal);
    lamp.target.position.copy(lampAim);
    new ResizeObserver(arrange).observe(stage);
    arrange();
    new IntersectionObserver((entries) => {
        renderer.setAnimationLoop(entries[0].isIntersecting ? frame : null);
    }).observe(stage);
}
