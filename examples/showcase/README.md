# Showcase props

The compiled props rendered live on the website's home page: four weapon
packs (`sword`, `axe`, `spear`, `staff`, 96×96) and three portrait packs
(`portraits_rpg`, `portraits_scifi`, `portraits_civilian`, 64×64).

Only the binaries live here; their `.ips` sources are kept outside this
repository. They are compiled by product 0.7.1, in the unversioned format,
from props commit `6697876`. The compiler's default fuel budget is
5,000,000. `showcase-manifest.json` records the full source commit, every
binary's byte size and SHA-256, and the hashes of all imported sources. Selection is implemented by ordinary inputs: `v` selects
authored styles and `a` selects an authored class where declared; default 0
rolls from the seed. Weapons use `renderPass` for color/emission selection and `bloom` for
native glow. The website uses `bloom=0` and adds its own blurred emission
pass (`renderPass=1`).
The runtime provides no variant/archetype counts or selectors.

```sh
ipgen sword.iprop --seed 7 -o sword.png
```

`website/prepare.sh` copies them into `website/public/showcase/`. The website
release smoke test checks that the site ships these exact bytes and compares
their WebAssembly renders with the native tools. They are not installed with
the SDK.

Unlike the rest of the repository, these props are **all rights reserved**:
see [LICENSE.md](LICENSE.md).
