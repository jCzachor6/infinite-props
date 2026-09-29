# iprop — website

A static site with the playground: the reference interpreter compiled to
WebAssembly, running the example assets entirely in the browser.

## Docs

Static documentation under `docs/` (multi-page Vite build): getting started
(with a verifiable first-asset walkthrough), the `.ips` language reference,
the format summary, the per-backend installation guides and the FAQ. A
client-side section search lives in the header; the canonical spec is
copied into the site (`public/spec/iprop-v0.1.md`) so the docs can link it.

## Playground

- pick an asset (portrait / plant / noise) — every frame is
  `generate(file, seed, variant, archetype)`, pixel-identical to the desktop
  tools;
- seed/variant/archetype controls, clamped to the asset's declared ranges;
- **shareable links**: the URL hash (`#portrait/1234/3/1`) reproduces the
  exact view — the same link always shows the same pixels.

## Build

```sh
cd website
npm install
sh prepare.sh          # syncs the WASM module (build backends/html5 first)
                       # and the example .iprop files into public/
npm run build          # static output in dist/
```

Serve `dist/` with any static server (or `npm run dev` for the Vite dev
server).

## Layout

| Path | What |
| ---- | ---- |
| `index.html` | the page shell |
| `src/main.ts` | the playground: the asset picker, the controls, the hash sync |
| `docs/` | the documentation pages |
| `public/` | the WASM module, the example binaries, the spec copy and the docs assets (synced by `prepare.sh`) |
