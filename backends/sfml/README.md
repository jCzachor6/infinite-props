# The SFML host (`backends/sfml/`)

A native desktop window over the C API — the first backend that proves
the `ipropc` library end to end (task 7).

## Build

SFML 3.1 is discovered with `find_package`; point CMake at the base-code
install (or any SFML 3.1 prefix):

```
cmake -S . -B build -DIPROP_SFML_PREFIX=$PWD/../base-code/QuantSpace/.deps/sfml
cmake --build build --parallel
```

Without SFML the backend is skipped gracefully. This produces:

- `iprop_sfml` — the windowed host (tasks 7.1/7.3);
- `sfml_conformance` — the headless golden check (task 7.2), registered
  with `ctest`.

## The host (7.1/7.3)

```
./build/backends/sfml/iprop_sfml [file.iprop]     # default: examples/plant.iprop
```

- Loads the `.iprop` through the C API and draws the generated RGBA
  (nearest-neighbour, integer-scaled to the 640×640 window).
- **Left/Right** cycle the seed; **Up/Down** cycle the variant (wrapped at
  the meta's `variants`).
- **A** toggles the atlas view: a 4×4 grid of variants (rows) × seeds
  (columns) rendered in one texture — the variant × seed map of one asset.
- **S** exports the current image to
  `iprop-export-seed<N>-variant<V>.png` through `sf::Image::saveToFile`.
- **Escape** closes the window; the title tracks the asset, seed, variant
  and the atlas state.

The same code paths run headlessly (scriptable, byte-verified against the
reference `ipgen`):

```
iprop_sfml file.iprop --seed 3 --export out.png         # the S-key path
iprop_sfml file.iprop --seed 3 --atlas out.png          # the A-key atlas
iprop_sfml file.iprop --seed 3 --screenshot out.png     # one windowed frame,
                                                        # read back from the
                                                        # texture (needs a
                                                        # display)
```

The export, the atlas (4×4 vs the per-tile `ipgen` assembly) and the
screenshot readback were verified pixel-identical to the reference
generator.

## The conformance (7.2)

`sfml_conformance <iprop-dir> <png-dir>` runs the golden corpus (the
portrait at five seeds, shapes, noise, parts — the `.iprop` files
assembled by the `capi_golden` target) through the C API, carries the
pixels through `sf::Image` (the same container the host uses on the way
to the texture) and compares them pixel-exactly against the committed
golden PNGs. The shapes entry additionally round-trips
`saveToFile`/`loadFromFile` to pin the PNG export losslessly.

Note: window titles stay ASCII — SFML's `String(std::string)` conversion
is locale-dependent and non-ASCII literals can crash the macOS backend.
