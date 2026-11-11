# Infinite Props website

A static site: the home page with live examples (real `.iprop` files rendered
in the browser by the reference interpreter compiled to WebAssembly), the
getting-started guide with the documentation, and the source page.

## Pages

- **Home** (`index.html`): the hero, "What is Infinite Props?", the live
  examples (after the butterfly benchmark, the simplest one: a tree of three files,
  `public/intro/tree.ips`, its part `clump.ips` and the 0.8 leaf material
  `foliage.ips` both import (a part takes the leaf colour and makes the
  material from it, since materials cannot be passed to parts); the tree
  bulges its rim with smooth noise and opens gaps with a 0.9 `noise.mask`; shown and
  downloadable beside a forest of 36 seeds (18 on a phone) with a re-roll
  button; recompile `public/intro/tree.iprop` with `ipc tree.ips -o tree.iprop`
  after editing them, and paste the new source into the tabs in `index.html`), the games
  showcase and the main features. The examples are the
  four weapons and three portrait packs under
  `examples/showcase/`, compiled binaries whose
  sources are not public; `prepare.sh` copies them in. Their source commit,
  binary sizes/hashes and imported-source hashes are recorded in
  `examples/showcase/showcase-manifest.json`.
  Every tile shows its file and size and keeps its own seed: auto-roll steps
  it up by one at random moments, and ‹ › (or the arrow keys on a focused
  tile) step it by hand. Weapon tiles mark their `tip`/`grip` outputs on
  hover; the fourth portrait cell lists the latest portrait's structured
  outputs. Weapons lean on their enchanted, ornate looks (chosen per seed
  through the declared `quality` and `enchant` inputs) and glow like in a game: the
  page requests the crisp sprite (`bloom=0`), renders the emission map
  (`renderPass=1`), blurs it by the `glow` output's sigma and adds it back in
  passes that follow the gain. The games showcase's first slot is taken by
  Infinite Formula One (`public/games/infinite-formula-one.webp`, linking to
  its itch.io page); the open slots show only ornate, enchanted weapons
  at full power, as silhouettes until hovered or tapped.
  Every weapon tile carries its loot rarity: common (plain), rare (soldier),
  epic (ornate) or legendary (ornate, enchanted and at glow power 4).
  First, right after "What is Infinite Props?" and before the tree example,
  **Butterfly benchmark** (its live results on the left of the model; above it on a
  phone) composes a 256×256
  butterfly from three compiled programs: forewing, hindwing and body.
  Each wing is generated once and mirrored; five canvas layers animate
  independently on the page. A dedicated worker keeps WASM generation off
  the animation thread. The button releases the preview, then generates and
  releases 50 fresh seeds in sequence, showing the completed count, mean
  generation time and mean VM fuel for each part. When a run ends or auto
  roll is stopped, a fresh preview settles on the stage once the last
  flight is over, ready for the next run. Preview generation,
  downloads and flight time are excluded from the averages. Reduced motion
  replaces flight and flapping with a short fade.
  The files and their byte sizes/hashes are in
  `examples/showcase/butterfly-benchmark.json`; `prepare.sh` copies them with
  the existing showcase assets. The WASM build exports its actual VM fuel
  counters and supports web workers.
  **Make It Swarm** generates 128 butterflies of 128×128 (64 on a phone) from
  the `butterfly-*-128.iprop` parts in `examples/showcase/butterfly-swarm.json`,
  which the worker loads on first use, then releases them all at once from the
  stage butterfly. They fly (`src/swarm.ts`) at the benchmark's speeds, turn at
  random, bounce off the edges of the middle 80% of the screen (all of it on a
  phone) and, when their bodies touch, bounce apart or fly through each other.
  After 5 s they fly off the screen. Reduced motion shows them at rest and
  fades them away.
  **One strike, never the same twice**, between the forest and the live
  examples, plays `public/effects/lightning-strike.iprop`, the lightning strike
  from the iprop-asset-effects skill's examples (its source
  `lightning-strike.ips` is beside it; recompile with `ipc` after editing). It
  follows pixelfx's lightning strike (MIT): `public/effects/NOTICE.md` keeps
  that licence with it. A
  worker (`public/lightning-worker.js`) renders each frame's three passes and
  sends them as bitmaps while the strike plays; the page draws the `renderPass`
  view picked (lit: the baked light added to a night stage under the sprite).
  The first seed is random, each finished strike hands over to seed + 1, and
  the ‹ › arrows or the seed field pick one by hand. `tint` and `branching`
  restart the strike. It plays only on screen; reduced motion holds the hero
  frame until Play.
- **Get started** (`docs/getting-started.html`) with the documentation
  pages beside it: the `.ips` language reference, the binary format, the
  backends and the FAQ. A client-side section search sits in the docs
  sidebar (press `/`).
- **Source** (`source.html`): the repository, the build and the layout.
- **Asset store**: available at [iprop.store](https://iprop.store/), linked from
  the navigation, footer and home-page buttons.

The complete language page is generated from
[`../docs/language/`](../docs/language/index.md), compiler-exported signatures
and the actual executable examples. Edit those sources and
`templates/language.html`; `docs/language.html` is generated and ignored.
Its downloadable Markdown, chapter files, catalog and version/source manifest
live under `public/docs/`, with linked examples under `public/examples/`.
The search index is generated from the same headings. The specifications
(`public/spec/`) and the 24 recipes (`public/recipes/`) are copied in as well.
The handbook download uses the canonical file in `public/docs/`; there is no
second copy under `reference/`.

## Shared chrome

The header, the footer and the docs sidebar live in `templates/partials/`.
A small Vite plugin (`vite.config.ts`) expands markers such as
`<!-- @header home -->` at build time, marks the current page in the
navigation and fills `{{BASE}}` and the version placeholders
(`{{PRODUCT_VERSION}}` from `package.json`, the rest from
`public/docs/language-manifest.json`), so the navigation works without JS.
All styles are in `src/styles/site.css`; `src/site.ts` makes the header
opaque past the home hero and opens the mobile menu.

## Search and link previews

Each page owns its title and meta description in its HTML source; the generated
language page uses `templates/language.html`. The metadata plugin in
`vite.config.ts` adds canonical URLs on `https://infiniteprops.dev/`, Open Graph,
X/Twitter cards, crawl directives and JSON-LD to the static HTML. The home page
identifies the website and software; documentation pages describe their content
as technical articles. No client-side JavaScript is needed to read these tags.

The production build also emits `robots.txt` and `sitemap.xml`. Sitemap entries
come from the same `PAGES` inventory as the Vite build; the home page uses `/`
as its canonical URL. No synthetic modification dates are published.

The shared 1200×630 card is `public/social-preview.png`. Its vector template is
`templates/social-preview.svg`; regenerate it together with the README
collector display using `scripts/render_readme_collection.cjs --png` from the
repository root, as described in `examples/showcase/README.md`. Its generated
butterflies retain the reserved rights in `examples/showcase/LICENSE.md`.

## Build

```sh
emcmake cmake -S backends/html5 -B build/wasm -DCMAKE_BUILD_TYPE=Release
cmake --build build/wasm --parallel
sh website/prepare.sh build/wasm build/tools
npm --prefix website ci
npm --prefix website run test:docs
npm --prefix website run build    # generates docs, type-checks; output in website/dist/
node website/test/release-smoke.js website/dist build/tools
```

Serve `dist/` with any static server (or `npm run dev` for the Vite dev
server). On Apache, `public/.htaccess` (copied into `dist/`) serves WASM with
its MIME type, redirects to HTTPS and makes browsers revalidate every file
outside `assets/` (the pages, `iprop.js`, `iprop.wasm`, the worker, the
`.iprop` files and their manifests), which keep their names across releases;
other servers need the same `Cache-Control: no-cache` for them. Only Vite's
hashed files under `assets/` can be cached long term.

Run these commands from the repository root after building the native tools.
The documentation generator requires Python 3.10 or newer and uses
`build/tools/ipc` by default; override it with `IPROP_COMPILER` and choose
Python with `IPROP_PYTHON` when needed.
Set `IPROP_LANGUAGE_DOCS_DIR` to an absolute directory outside cloud
synchronization to generate the handbook there; the default is
`build/language-docs`. CMake exposes the same directory setting as a cache
option for native builds and SDK installation.
`prepare.sh` reads the requested WASM build directly, preventing stale
modules from entering the website. The release smoke check resolves every
local page resource/link, checks that each showcase binary is the one in
`examples/showcase`, and compares the site's own WASM renders to native
pixels. CI runs
this complete pipeline in its WASM job.

## Layout

| Path | What |
| ---- | ---- |
| `index.html`, `source.html` | the home and source pages |
| `docs/` | the guide and documentation pages |
| `templates/partials/` | the header, footer and docs sidebar |
| `templates/language.html` | the presentation shell for the generated handbook |
| `src/home.ts` | the live examples, the outputs card and the game slots |
| `src/butterfly.ts` | benchmark and swarm controls, live averages and canvas-part flight animation |
| `src/swarm.ts` | the swarm's steering model, free of the DOM |
| `src/lightning.ts` | the animated lightning strike: its inputs, seed, views, playback and the next seed |
| `public/lightning-worker.js` | WASM rendering of the strike's frames, three passes each |
| `public/butterfly-worker.js` | three-part WASM generation of either butterfly model and measured fuel/time |
| `src/site.ts` | the header and the mobile menu |
| `src/styles/site.css` | every style of the site |
| `scripts/build-docs.mjs` | Markdown rendering and generated search anchors |
| `public/` | the WASM module, the showcase binaries, the specs and the docs assets (synced by `prepare.sh` and `npm run docs`) |
