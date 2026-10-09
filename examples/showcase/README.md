# Showcase props

The compiled props rendered live on the website's home page: four weapon
packs (`sword`, `axe`, `spear`, `staff`, 96×96) and three portrait packs
(`portraits_rpg`, `portraits_scifi`, `portraits_civilian`, 64×64).

Only the binaries live here; their `.ips` sources are kept outside this
repository. They were compiled with the 0.6 compiler (stripped, packed) from
the props workspace at commit `30a7dd6`. In every root asset, variant 0 and
archetype 0 roll from the seed, so a seed alone gives a completely random
prop:

```sh
ipgen sword.iprop --seed 7 -o sword.png
```

`website/prepare.sh` copies them into `website/public/showcase/`. The website
release smoke test checks that the site ships these exact bytes and compares
their WebAssembly renders with the native tools. They are not installed with
the SDK.

Unlike the rest of the repository, these props are **all rights reserved**:
see [LICENSE.md](LICENSE.md).
