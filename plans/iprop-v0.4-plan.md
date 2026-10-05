# iprop v0.4 — plan: the LLM-friendly language and the size/perf pass

Branch: `develop` (off v0.3). Approved scope: the LLM-friendliness of the
`.ips` language plus the compiler and file optimisations. Every language
change is lexical/compiler-level only — no VM or frozen-semantics impact.

Conventions carried over: the frozen golden corpus stays pixel-identical
throughout; the optimised artifacts may shrink but never grow; commits
land after the user's acceptance; the file-format changes update
`spec/iprop-v0.4.md`.

## A. LLM-friendliness — the words

- [x] `A.1` `draw { }` — the only form; `v` and `a` become the builtins
  (like `seed`). The explicit `draw(v: i32, a: i32)` is REMOVED (the
  pre-1.0 break — the corpus sources migrate; the compiler still
  synthesises the entry's `(i32, i32)` params, so the compiled artifacts
  stay byte-identical). The helper params keep their mandatory types;
  the locals stay inferred (no change).
- [x] `A.2` the consistent `color.*` namespace — the ONLY form; the bare
  `mix`/`shade` are REMOVED (the pre-1.0 break; the corpus sources
  migrate to `color.mix`/`color.shade`, the compiled artifacts stay
  byte-identical).
- [x] `A.3` the conventional aliases: `int(x)` / `float(x)`, `rgb()` /
  `rgba()`, one `lerp` for the colours and the f32s.
- [x] `A.4` the RNG sugar: `pick(a, b, c, …)`, `chance(p)`, `pick(n)`
  (today's `index`).
- [x] `A.5` the named arguments for every builtin and canvas op
  (`rect(x = 0, y = 0, w = 10, h = 10, color = #fff)`), mirroring the
  part-call's named args — the order-proof, self-documenting calls.
- [x] `A.6` the seed-direct draws: `roll(seed, a, b)` and
  `choose(seed, …)` — the deterministic values from an explicit seed
  without the stream push/pop juggling.

## B. LLM-friendliness — the loop

- [x] `B.1` the error messages with the suggestions ("did you mean …")
  and the codepoint-based columns (the emoji groundwork).
- [x] `B.2` the canonical formatting at the compile (the printer
  normalises; the deterministic round-trip).
- [x] `B.3` the recipe gallery: 20–40 small `.ips` examples as the
  pattern corpus.
- [x] `B.4` the emoji keyword aliases — the 18 pinned in
  `quants-space/iprop-v0.4-emoji-aliases.md` (the exact single
  codepoints, the word canon in the AST/diagnostics/printer, both forms
  parallel).

## C. The compiler optimisations (bit-exact)

- [x] `C.1` the DCE (the dead locals, the unused palette elements).
- [x] `C.2` the peephole + the constant fold extension (the const
  chains, the redundant `local.set`/`get` pairs, the constant `color.*`
  and `vec2.*` folds).
- [x] `C.3` the CSE (the common subexpressions — the LLM-generated code
  repeats them).
- [x] `C.4` the constant-bound loop unrolling and the strength reduction
  (`*2` → `+`, `/2` → the shift).
- [x] `C.5` the acceptance: the pixels bit-identical, the artifacts ≤
  the baseline.

## D. The file optimisation

- [x] `D.1` the `.iprop` compression (the deflate/zstd, the `flags`
  bit, the lossless load-time decompression) — spec v0.4.
- [x] `D.2` the `--strip` by default for the production artifacts.

## Cross-cutting acceptance

- [x] The frozen golden corpus stays pixel-identical after every task.
- [x] The optimised artifacts never grow.
- [x] The frozen pins (native/WASM/C/SFML/Godot) stay green throughout.
