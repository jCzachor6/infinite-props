# Infinite Props website

A static site: the home page with live examples (real `.iprop` files rendered
in the browser by the reference interpreter compiled to WebAssembly), the
getting-started guide with the documentation, and the source page.

## Pages

- **Home** (`index.html`): the hero, "What is Infinite Props?", the live
  examples, the main features and the games showcase. The examples are the
  four weapons and three portrait packs under
  [`../examples/showcase/`](../examples/showcase/), compiled binaries whose
  sources are not public; `prepare.sh` copies them in.
  Every tile shows its file and size and keeps its own seed: auto-roll steps
  it up by one at random moments, and ‹ › (or the arrow keys on a focused
  tile) step it by hand. Weapon tiles mark their `tip`/`grip` outputs on
  hover; the fourth portrait cell lists the latest portrait's structured
  outputs. Weapons lean on their enchanted, ornate looks (chosen per seed
  through the `quality` input and the variant) and glow like in a game: the
  page renders the emission map (`layer` 1), blurs it by the `glow`
  output's sigma and adds it back in passes that follow the gain. The game slots show only ornate, enchanted weapons
  at full power, as silhouettes until hovered or tapped.
  Every weapon tile carries its loot rarity: common (plain), rare (soldier),
  epic (ornate) or legendary (ornate, enchanted and at glow power 4).
  "Roll a legendary" in the hero opens a weapon case: a strip of 40 real
  rolled weapons (fillers 45% common, 33% rare, 22% epic) runs under a
  marker for 6.5 s and stops on a legendary one, then the win shows it big
  with golden rays, its inputs, glow and tip. The next case is prepared in
  idle time; with reduced motion the strip is skipped. The share link
  `#roll/<weapon>/<seed>` reopens the same win, because the look follows from
  the seed alone.
- **The numbers** under the hero: the sword pack's size (filled in from the
  file), 2^32 seeds × 2 enchantment variants = 8.6 billion combinations, and
  ≈ 5 TB for all of them as PNGs, from the 583-byte mean of 150 `ipgen` PNGs
  (`sword.iprop`, seeds 1–150, measured on 2026-10-09). Remeasure when the
  sword pack changes.
- **Get started** (`docs/getting-started.html`) with the documentation
  pages beside it: the `.ips` language reference, the binary format, the
  backends and the FAQ. A client-side section search sits in the docs
  sidebar (press `/`).
- **Source** (`source.html`): the repository, the build and the layout.
- **Asset store**: an empty link for now.

The complete language page is generated from
[`../docs/language/`](../docs/language/index.md), compiler-exported signatures
and the actual executable examples. Edit those sources and
`templates/language.html`; `docs/language.html` is generated and ignored.
Its downloadable Markdown, chapter files, catalog and version/source manifest
live under `public/docs/`, with linked examples under `public/examples/`.
The search index is generated from the same headings. The specifications
(`public/spec/`), the reference downloads (`public/reference/`) and the 24
recipes (`public/recipes/`) are copied in as well.

## Shared chrome

The header, the footer and the docs sidebar live in `templates/partials/`.
A small Vite plugin (`vite.config.ts`) expands markers such as
`<!-- @header home -->` at build time, marks the current page in the
navigation and fills `{{BASE}}` and the version placeholders
(`{{PRODUCT_VERSION}}` from `package.json`, the rest from
`public/docs/language-manifest.json`), so the navigation works without JS.
All styles are in `src/styles/site.css`; `src/site.ts` makes the header
opaque past the home hero and opens the mobile menu.

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
server).

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
| `src/site.ts` | the header and the mobile menu |
| `src/styles/site.css` | every style of the site |
| `scripts/build-docs.mjs` | Markdown rendering and generated search anchors |
| `public/` | the WASM module, the showcase binaries, the specs and the docs assets (synced by `prepare.sh` and `npm run docs`) |
