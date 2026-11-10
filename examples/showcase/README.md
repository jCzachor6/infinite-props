# Showcase props

The compiled props rendered live on the website's home page: four weapon
packs (`sword`, `axe`, `spear`, `staff`, 96×96) and three portrait packs
(`portraits_rpg`, `portraits_scifi`, `portraits_civilian`, 64×64), plus three
butterfly parts (`butterfly-forewing`, `butterfly-hindwing`, `butterfly-body`)
and the three parts of the smaller 128×128 butterfly released by the swarm
(`butterfly-forewing-128`, `butterfly-hindwing-128`, `butterfly-body-128`).

The `.ips` sources are kept outside this repository; the programs here are
compiled binaries, in the unversioned format. The weapon and portrait packs
are compiled by product 0.9.0 from the props working tree at commit `855ba99`,
with its default fuel budget of 5,000,000. `showcase-manifest.json` records
the full source commit, every binary's byte size and SHA-256, and the hashes
of all imported sources.
`butterfly-benchmark.json` and `butterfly-swarm.json` record the same for the
two butterflies. Their six programs are written in the 1.0 language and
compiled by product 1.0.0, whose default budget of 25,000,000 they need
(a 256×256 wing spends up to about 10 million fuel), from the `butterfly`
folder of the props repository at commit `2228edd`; the source hashes in the
manifests identify the exact files. The 128×128 parts are the
same sources drawn at half the scale. Selection is implemented by ordinary inputs named after what they choose:
`enchant` and `quality` (plus `bladeStyle`, `bladeSteel`, `headStyle` and
`coating` where declared) for weapons, `sex` and `role` for portraits;
default 0 rolls from the seed. Weapons use `renderPass` for color/emission selection and `bloom` for
native glow. The website uses `bloom=0` and adds its own blurred emission
pass (`renderPass=1`).
The runtime provides no variant/archetype counts or selectors.

```sh
ipgen sword.iprop --seed 7 -o sword.png
```

`website/prepare.sh` copies them into `website/public/showcase/`. The website
release smoke test checks that the site ships these exact bytes and compares
their WebAssembly renders with the native tools. The compiled programs are
not installed with the SDK.

## README collector display

`readme-collection.png` is the root README's 4×4 butterfly display.
`readme-collection.svg` is its self-contained vector layout with embedded
original renders. `readme-collection.json` records all sixteen seeds, the
worker and program hashes, the raw RGBA hashes of each part, and the output
image hashes.

The renderer runs the original website WASM worker and follows the resting
snapshot assembly in `website/src/butterfly.ts`. It adds the collector frame,
velvet background, shadows and labels around those generated specimens.
The images on file show the butterflies of the earlier programs, whose hashes
the manifest records; regenerating draws the current ones.

To regenerate both images from a built WASM runtime, install `puppeteer-core`
in a temporary tools directory, then run from the repository root:

```sh
npm install --prefix /tmp/iprop-readme-tools puppeteer-core
IPROP_BROWSER=/path/to/chromium \
IPROP_PUPPETEER=/tmp/iprop-readme-tools/node_modules/puppeteer-core \
  node scripts/render_readme_collection.cjs --png
```

The PNG export needs a Chromium-based browser compatible with Puppeteer.
An optional positional argument selects a WASM build directory other than
`build/wasm`. Without `--png`, only the SVG and its manifest are regenerated.
The SDK includes only the PNG preview and its license notice.

The same PNG export also refreshes `website/public/social-preview.png` from
`website/templates/social-preview.svg` and records its hash in the manifest.
This is the website's shared 1200×630 Open Graph and X/Twitter image.

Unlike the rest of the repository, these props are **all rights reserved**:
see [LICENSE.md](LICENSE.md).
