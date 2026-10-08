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
  (nearest-neighbour, fitted and centered in the 640×640 window).
- Loads through `iprop_program_load_ex`, copies metadata through
  `iprop_program_query` and generates through `iprop_generate_ex` from an
  `iprop_parameters` set. Failures report the C API message and its stable
  `iprop_error_ex` reason code.
- `--set input=value` (repeatable) overrides a declared input by name:
  `#RRGGBB` or `#RRGGBBAA` colours, decimal `f32` and `u32`, where a leading
  minus sets an `i32` input. The type comes from the program's declaration.
- Reads generated images through `iprop_result_query_image`, checking RGBA8,
  top-left storage, the legacy destination-alpha mode and tight row stride.
  Copies exactly `byte_count` bytes; it does not convert alpha or infer the
  generated dimensions from asset metadata.
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
iprop_sfml file.iprop --set tint=#9dc --export out.png  # an input override
iprop_sfml file.iprop --seed 3 --atlas out.png          # the A-key atlas
iprop_sfml file.iprop --seed 3 --screenshot out.png     # one windowed frame,
                                                        # read back from the
                                                        # window (needs a
                                                        # display)
```

The export contains the asset's native pixels; the atlas contains 4×4
native tiles. The screenshot captures the 640×640 window, including its
background, centered placement and scale.

## The conformance (7.2)

`sfml_conformance <iprop-dir> <png-dir>` runs the golden corpus (the
portrait at five seeds, shapes, noise, parts — the `.iprop` files
assembled by the `capi_golden` target) through the C API, carries the
pixels through `sf::Image` (the same container the host uses on the way
to the texture) and compares them pixel-exactly against the committed
golden PNGs. The shapes entry additionally round-trips
`saveToFile`/`loadFromFile` to pin the PNG export losslessly.
Compiled `.ips` fixtures also exercise the current language features and plant
composition. The source fixture is uploaded and drawn to `sf::RenderTexture`
for every variant/archetype combination; `sfml_render` separately checks
texture resizing and viewport fitting for single sprites and atlases.
Output checks use the same `Host.hpp::copyOutputs` adapter available to the
host: sized slot queries and typed point/rect/circle reads preserve global
order, exact owner IDs (including gaps), empty declarations and all stored
float bits. Eight D4 cases compare against legacy packed data; a binary fixture
checks signed zero, subnormals, infinities, NaN payloads and UTF-8 names with
embedded NUL. Names are copied with their byte count into `std::string`.

The image view is borrowed until `result_free`; freeing its program or
generating another result does not invalidate it. The host's pixel vector and
copied output names/values own their data and remain valid after result release.
The shared header is installed as `include/iprop/backend/ResultQueries.hpp`
with its `iprop.h` dependency. Its query helpers publish a full view on success;
typed array copies are intended for unpublished candidate arrays.

Installed hosts live at `<prefix>/bin/iprop_sfml` with selected shared
SFML libraries under `<prefix>/lib` (DLLs under `bin` on Windows). A Linux
installation still requires system graphics/font libraries and a display
or Xvfb for rendering. Headless PNG export needs no window.

Note: window titles stay ASCII — SFML's `String(std::string)` conversion
is locale-dependent and non-ASCII literals can crash the macOS backend.
