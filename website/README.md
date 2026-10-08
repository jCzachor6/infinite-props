# iprop — website

A static site with the playground: the reference interpreter compiled to
WebAssembly, running the example assets entirely in the browser.

## Docs

Documentation under `docs/` (multi-page Vite build): getting started
(with a verifiable first-asset walkthrough), the `.ips` language reference,
the format summary, the per-backend installation guides and the FAQ. A
client-side section search lives in the header; the canonical spec is
copied into the site (`public/spec/iprop-v0.4.md`, alongside the frozen
0.1–0.3 specifications). The complete language page is generated from
[`../docs/language/`](../docs/language/index.md), compiler-exported signatures
and the actual executable examples. Edit those sources and
`templates/language.html`; `docs/language.html` is generated and ignored.
Its downloadable Markdown, chapter files, catalog and version/source manifest
live under `public/docs/`, with linked examples under `public/examples/`.
The search index is generated from the same headings. Previous download URLs
under `public/reference/` and the 24 recipes under `public/recipes/` remain
available.

## Playground

- pick an asset (portrait / plant / noise) — every frame is
  `generate(file, seed, variant, archetype)`, pixel-identical to the desktop
  tools;
- seed/variant/archetype controls, clamped to the asset's declared ranges;
- **shareable links**: the URL hash (`#portrait/1234/3/1`) reproduces the
  exact view — the same link always shows the same pixels.

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
`prepare.sh` reads the requested WASM build directly and compiles the noise
asset with the current compiler, preventing stale demo modules from entering
the website. The release smoke check resolves every local page resource/link
and compares the site's own WASM/assets to native pixels, including packed 0.4
noise. CI runs this complete pipeline in its WASM job.

## Layout

| Path | What |
| ---- | ---- |
| `index.html` | the page shell |
| `src/main.ts` | the playground: the asset picker, the controls, the hash sync |
| `docs/` | the documentation pages |
| `templates/language.html` | the presentation shell for the generated handbook |
| `scripts/build-docs.mjs` | Markdown rendering and generated search anchors |
| `public/` | the WASM module, the example binaries, the spec copy and the docs assets (synced by `prepare.sh`) |
