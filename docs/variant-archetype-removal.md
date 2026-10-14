# Migrating to explicit asset inputs

The current 0.7 language removes implicit variant/archetype controls. Format
0.7, C API 0.4 and ABI 0.5 require rebuilding binaries and host clients.

Declare only the selections the asset needs:

```ips
asset example {
  size 1x1
  input i32 style = 0
  input i32 characterClass = 0
  draw {
    if (style == 1) { pixel(0, 0, #ffcc88) }
  }
}
```

Pass values through ordinary uniforms, for example
`ipgen example.iprop --seed 7 --uniform style=1 -o example.png`.
Parts declare their own inputs and receive them as named arguments. The
author defines any supported values or validation; the runtime adds no
variant/archetype counts, selection or range checks.

`v`, `a`, `variants` and `archetypes` are ordinary names. Bare `v`/`a` reads
without a declaration fail, and the old count declarations and generator
options are rejected. No `variant()`/`archetype()` builtins are introduced.
Seed selection and explicit sampler `.pick()` behavior are unchanged.

The seven private showcase sources were migrated in an isolated source
snapshot from props commit `30a7dd6`. They use explicit selection inputs and
`renderLayer` for color/emission selection; their sources remain outside this
repository. Rebuilt binaries match all 765 pre-change native image/output
references across five seeds and their authored selection combinations.
