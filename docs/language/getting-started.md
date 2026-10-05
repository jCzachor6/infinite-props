# Your first asset

Save [first-asset.ips](../../examples/language/first-asset.ips), then run the
installed tools:

```sh
ipc first-asset.ips -o first-asset.iprop
ipval first-asset.iprop
ipgen first-asset.iprop --seed 7 -o first-asset.png
```

<!-- example: ../../examples/language/first-asset.ips -->

`asset` names the program. `size 16x16` declares its canvas. The required
`draw { ... }` entry draws a background and a rectangle. Calls execute in
written order, so later shapes paint over earlier ones. Coordinates use
the asset's downward-y plane: x increases rightward, y downward.

Running the same executable with the same invocation reproduces the same
pixels and structured outputs across conforming native and WASM hosts.
Change a seed to vary random draws; change a variant/archetype to select
an authored variation. A program with no random operations ignores the seed.

For a repository build, configure with `cmake -B build -DCMAKE_BUILD_TYPE=Release`
and run `cmake --build build --parallel`. Replace tool names with
`build/tools/ipc`, `build/tools/ipval` and `build/tools/ipgen` (Windows adds
`.exe`). A native SDK already contains those tools under `bin/`.
