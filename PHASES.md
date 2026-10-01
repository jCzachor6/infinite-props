# iprop v0.3 — plan: the beauty toolkit

Branch: `develop` (off the v0.2 mainline). Approved scope: the `vec2` value
type first, then the operation groups 1–9. The showcase examples (the
butterfly, the Bob Ross landscape) are a separate pass after the toolkit.

Conventions carried over from v0.2: each task updates the spec (§9/§12) and
ships pinned algorithms with bit-exact tables; the frozen golden corpus stays
pixel-identical after every task (the additions are additive); commits land
only after the user's acceptance; the first task that adds opcodes bumps the
format version (minor).

## A. `vec2` (the foundation — first)

- [x] `A.1` spec §3/§9: the `vec2` value type — the stack and local slots,
  the function params/results, the point outputs become `vec2`. The ops:
  `vec2.add/sub`, `vec2.mul/div` (vector × scalar and vector × vector),
  `vec2.neg`, `vec2.len`, `vec2.dot`, `vec2.rot` (rotation — the domain
  rotation for noise and shapes), `vec2.make`, `vec2.x/y` extraction.
- [x] `A.2` the runtime: the value kind, the arithmetic, the validator, the
  opcode names, the assembler/disassembler.
- [x] `A.3` the language: the `vec2` type in the checker, the `{x, y}`
  literals, the operators, the `.x`/`.y` swizzles, the output writes as
  `vec2`.
- [x] `A.4` the compiler lowering + the `.ipa` surface.
- [x] `A.5` tests: the bit tables and the language tests; the proof — the
  frozen corpus recompiles byte-identical (the old compound point literals
  lower to the same code).

## 1 + 6. The SDF package and the Bézier strokes

- [x] `1.1` spec: `dist.circle`, `dist.rect`, `dist.roundrect`, `dist.line`
  — the pinned binary32 formulas (the distance functions), plus
  `fill(dist, w)` (the smoothstep threshold render).
- [x] `1.2` the runtime + the language builtins + the bit tables.
- [x] `6.1` spec: `dist.bezier` / `curve(...)` — the quadratic/cubic Bézier
  distance and the stroked rendering, same pinned style.
- [x] `6.2` the runtime + the builtins + the bit tables.

## 2 + 7. The noise v2 (gradient, fbm, ridge, cell)

- [x] `2.1` spec: `noise.grad(x, y)` — the pinned permutation lattice and
  the fade curve (the standard quintic, pinned internally — independent of
  the task-3 builtins); `noise.fbm(x, y, octaves)` — the pinned
  lacunarity/gain octave summation; `noise.cell(x, y)` — the Worley
  distance; `warp(x, y, amount)` — the domain warping (the pinned inner
  fbm — the most-used noise composition as one op); `cell.jitter(x, y)` —
  the hash-based point per lattice cell (the blue-noise detail placement:
  stars, speckles, scattered stones).
- [x] `2.2` the runtime + the builtins + the bit tables.
- [x] `7.1` spec: `noise.ridge(x, y, octaves)` — the ridged multifractal
  over the same gradient noise.
- [x] `7.2` the runtime + the builtin + the bit tables.

## 3. The curves and the ramps

- [x] `3.1` spec: `gain(t, k)` (the Schlick contrast curve), `quintic(t)`,
  `ramp(palette, t)` — the continuous interpolation over a palette (the
  pinned stop selection and lerp, extending the task-3 arrays).
- [x] `3.2` the runtime + the builtins + the bit tables.

## 4. The colours

- [x] `4.1` spec: `color.hsv(c, hueShift, satScale, valScale)` — the pinned
  RGB↔HSV round-trip (the hue in turns [0,1)); `color.mul` (the
  channel-wise multiply, truncating like `color.mix`); `color.scale`
  (`f32` × channels, clamped); the decision on the reserved 0x52:
  `color.set.a` `[c i32] -> [c]`, clamped to [0,255].
- [x] `4.2` the runtime + the builtins + the bit tables.

## 5. The dither, the fold and the polar

- [x] `5.1` spec: `dither(t, x, y)` — the pinned Bayer matrix threshold;
  `fold(v, n)` and `polar(v)` as the `vec2` ops (the kaleidoscope and the
  cartesian→polar conversion).
- [x] `5.2` the runtime + the builtins + the bit tables.

## 8. The curve toolkit (the micro-ops)

- [x] `8.1` spec: `smoothstep(a, b, t)` (the two-sided), `remap(t, a, b, c, d)`,
  `clamp(t, a, b)`.
- [x] `8.2` the runtime + the builtins + the bit tables.

## 9. Part manipulation — the transforms (the D4 group)

- [x] `9.1` spec §9.10/§10.5: the part-call transform kind in the `aux`
  byte. The v0.3 kinds are the D4 dihedral group — the
  eight exact pixel permutations (the identity, `mirror_x`, `mirror_y`,
  the 90/180/270-degree rotations and their combinations) — no resampling,
  pinned in an exact permutation table. The kind enum stays open: the
  future general-affine transforms (the arbitrary rotation/scale with the
  pinned nearest-neighbour resampling) get new kinds, which older runtimes
  reject at validation — the format does not break.
- [x] `9.2` the structured-output propagation: the same transform applies
  to the part's outputs (the exact sign/swap permutations — e.g. a
  mirrored wing propagates a mirrored anchor point).
- [x] `9.3` the language (`part.draw(..., mirror_x = true, rot = 90)` — the
  checker folds the named args into one D4 kind), the compiler, the
  runtime blit (the permutation copy) and the bit tests.
- [ ] `9.4` proof: the butterfly showcase uses it — one `wing.ips` part
  placed four times (the fore/hind wings × the mirror).

## Cross-cutting acceptance

- [x] The existing golden corpus stays pixel-identical — the frozen PNG
  and structured-output pins pass after the review fixes.
- [x] Every new op has the pinned algorithm in the spec and the bit-exact
  tests — §12.9–12.14 carry the formulas and the generated vectors.
- [x] The frozen pins (native/WASM/C/SFML/Godot) stay green throughout —
  the WASM conformance reached 116 pixel-exact images and structured-output
  comparisons plus 19 trap matches.

## Deferred (not in this pass)

- The showcase examples (a separate pass, after the toolkit lands):
  1. **The butterfly** — the single-asset showcase: the SDF wing
     silhouettes (the seeded shape families), the Bézier veins and
     antennae, the `fold` mirror symmetry, the `hsv`/`ramp` colour harmony,
     the `cell.jitter` eyespots, the `fbm` mottling and the `dither`
     shading — every seed clearly different in shape, colour and spots,
     rendered at 512×512.
  2. **The Bob Ross landscape** — the scene showcase: the `ramp` sky, the
     `ridge`/`fbm` layered mountains with the atmospheric perspective, the
     jittered trees, the `fold` water reflections, the `fbm` mist, and the
     biome system (the seed-picked parameter bundles + the per-biome
     palettes) — every seed a unique painting.
- The game mods (the GTA SA faces, the Crusader Kings heraldry, the
  Stardew fish, …) are parked for now — revisited only if the showcase
  pass grows.
- The masks/multi-pass, the full colour arithmetic, the `switch`
  statement, the Bevy/Unity glue, the v0.3.0 release.
- The general-affine part transforms (the arbitrary rotation/scale with
  the resampling) — a future transform kind, after the D4 group (task 9).
- The whole-canvas generator algorithms — the diamond-square, the
  cellular automata (caves), the midpoint displacement — they need
  buffers/heightmap passes; revisit them together with the masks. The
  L-systems need strings and a turtle interpreter — out of the language
  scope (the ports already hand-roll their plants with loops).
- A dedicated noise-object/graph DSL — the functional composition (user
  functions + `warp` + the curve builtins) already covers the noise
  graphs; the node-graph direction is deliberately out of scope.
