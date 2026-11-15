# Named seeds — 1.1 implementation contract

**Status:** Draft 1.1 extension, in development on top of
[the 1.0 contract set](iprop-v1.0.md). Product and source version, the C API
and the ABI stay as released until the release integration stage.
It builds on [the RNG contract](iprop-v0.8.md#12-seeded-primitives--exact-algorithms) and the
[shared fuel tariff](fuel-v0.9.md). One opcode value is added.

A program that uses none of this extension keeps its bytes, image, outputs,
RNG, fuel and work. `.fork` compiles to existing instructions only, so a
program that forks but never reads `seed.initial()` loads and runs unchanged
on a 1.0 runtime. A program that reads `seed.initial()` contains the new
instruction, which a 1.0 runtime rejects during validation, also in
unreachable code.

## Source surface

| Expression | Meaning | Source type |
| --- | --- | --- |
| `seed` | the current stream state, unchanged from 1.0 | `i32` |
| `seed.initial()` | the seed the running asset or part call started with | `seed` |
| `x.fork("name")` | a seed derived from `x` (an `i32` or a `seed`) and a name | `seed` |
| `x.fork(i)` | a seed derived from `x` and an `i32` index | `seed` |
| `int(s)` | the 32 bits of a seed | `i32` |

`seed` is a nominal **source** type. Its value is 32 bits, carried in the VM's
`i32` locals, parameters, results, temporaries and stack: no type descriptor,
value variant, pool entry or host type exists for it, and the compiler maps it
to `i32` at one boundary for every slot. A seed converts to nothing
implicitly. It may be a local (typed by its first assignment), a helper
parameter (`name: seed`) or a helper result (`-> seed`). `==` and `!=` compare
two seeds; every other operator, `float`, `hash` and a mixed comparison with
an `i32` need `int(s)`. Seeds cannot be host inputs or outputs, asset or
material constants, material fields, palette or list elements, samplers,
steppers, or choices of `pick` and `choose`; each is a compile error.

`seed` names the type only after `:` and `->`. `fork` and `initial` are not
reserved: helpers, locals, inputs and import aliases may use these names.

A **seed argument** takes an `i32` or a `seed`: every builtin parameter whose
registry role is *seed* (`stream.push`, `roll`, `weather`, the seeded noise,
cell, mask, Voronoi and gap functions), the first argument of `choose` and the
seed of a part call. The role is recorded in the builtin registry, never read
from a parameter name, and other `i32` parameters keep rejecting a seed. Both
types arrive as the same 32 bits.

## Initial seed

Every call frame holds an **initial seed** beside its stream state:

| Frame | Initial seed |
| --- | --- |
| entry `draw` | the generation's input seed |
| helper call | its caller's initial seed |
| part call | the seed argument of this call, the same value that starts its stream |

`stream.push` and `stream.pop` leave it unchanged, and a recycled frame takes
the value of the call that reuses it. Calling one part alias twice with
different seeds gives each call its own initial seed; the value is not bound
to the alias. A seed value has no history: `s.initial()` on any receiver other
than the bare `seed` is a compile error.

### `seed.initial` (0x6B)

| Opcode | Mnemonic | Aux | Stack |
| --- | --- | --- | --- |
| 0x6B | `seed.initial` | 0 | `[] -> [i32]` |

The operand and aux are zero; a nonzero operand or aux fails validation, also
in unreachable code. The instruction
pushes the frame's initial seed, reads and advances no stream, and costs one
fuel unit at the 100 % tariff, with no kernel work. Its effect is
`read_initial`: it never folds to a compile-time constant, and the compiler
does not drop or merge it. Instructions that existed in 1.0 keep their
numbers, effects and prices; `stream.state` (0x66) is unchanged.

## Fork contract

All arithmetic is on `u32` modulo 2^32. `hash32` is the frozen splitmix32 of
[§12.1](iprop-v0.8.md#121-hashx); FNV-1a 32 is:

```
fnv1a32(bytes): h = 2166136261
                for b in bytes: h = (h xor b) * 16777619   (mod 2^32)
TEXT_TAG  = fnv1a32("iprop.seed.fork/text/v1")   = 0x80d7e5dc
INDEX_TAG = fnv1a32("iprop.seed.fork/index/v1")  = 0x986d7397

fork(base, "name") = hash32(bits(base) xor hash32(TEXT_TAG xor fnv1a32(utf8(name))))
fork(base, index)  = hash32(bits(base) xor hash32(INDEX_TAG xor bits(index)))
```

A name key is a string literal; its bytes are the literal after escape
decoding, in UTF-8, case-sensitive and not normalized. The empty name is
valid. An index key is any `i32` expression; a negative index uses its
two's-complement bits. No other type is a key: an `f32` is never converted,
and a seed key needs `int(s)`. The two tags keep the domains apart, so
`fork("12")` and `fork(12)` differ. Forks are hierarchical: `s.fork(a).fork(b)`
is not `s.fork(b).fork(a)`, and nothing sorts or concatenates keys. Variable
names, lines, files, import aliases and compiler traversal never enter a
result. 32 bits can collide; the contract is determinism, not independence
or freedom from collisions.

### Lowering

A fork is no instruction of its own. The compiler evaluates the receiver,
then the key, each exactly once, and emits:

```
name key:   <receiver>  const.i32 SALT  i32.xor  hash
            where SALT = hash32(TEXT_TAG xor fnv1a32(name)), computed while compiling
index key:  <receiver>  <index>  const.i32 INDEX_TAG  i32.xor  hash  i32.xor  hash
```

Fuel and work are exactly those of the emitted instructions; a name costs
nothing at run time. A fork reads and advances no stream and changes neither
its receiver nor any other value; the effects of the receiver and key remain
theirs and run in that order. `seed.fork(index(8))` therefore reads the
stream state before `index(8)` advances it. The optimizer may fold a fork of
constant operands with the VM's own `hash` implementation.

### Vectors

| Base | Key | Result |
| --- | --- | --- |
| 0x00000000 | `"leaves"` | 0x4b490890 |
| 0x00000000 | `""` | 0x482a6c4f |
| 0x00000000 | `"Leaves"` | 0x6efe9489 |
| 0x00000000 | `"zażółć"` | 0xfe3496aa |
| 0x00000000 | `"12"` | 0xcb86f183 |
| 0x00000000 | `0` | 0xc159cf34 |
| 0x00000000 | `12` | 0xded86737 |
| 0x00000000 | `-1` | 0x6afb18c6 |
| 0x00000007 | `"leaves"` | 0x6333835b |
| 0x00000007 | `""` | 0x065fad84 |
| 0x00000007 | `"Leaves"` | 0x36f943eb |
| 0x00000007 | `"zażółć"` | 0x462a66bc |
| 0x00000007 | `"12"` | 0x7baf49b4 |
| 0x00000007 | `0` | 0xe8c4e56a |
| 0x00000007 | `12` | 0x4aede6c7 |
| 0x00000007 | `-1` | 0x12c38c8f |
| 0x80000000 | `"leaves"` | 0xae82492e |
| 0x80000000 | `""` | 0x8c2f1b51 |
| 0x80000000 | `"Leaves"` | 0xb9356667 |
| 0x80000000 | `"zażółć"` | 0x2ac658db |
| 0x80000000 | `"12"` | 0x0cecfcd2 |
| 0x80000000 | `0` | 0xafd53ae9 |
| 0x80000000 | `12` | 0xa64bdaa1 |
| 0x80000000 | `-1` | 0x8fdf8537 |
| 0xffffffff | `"leaves"` | 0xd7927265 |
| 0xffffffff | `""` | 0x33aa4e41 |
| 0xffffffff | `"Leaves"` | 0x443c66ad |
| 0xffffffff | `"zażółć"` | 0x53501834 |
| 0xffffffff | `"12"` | 0xa0095af6 |
| 0xffffffff | `0` | 0x41846e5f |
| 0xffffffff | `12` | 0x34704a5d |
| 0xffffffff | `-1` | 0x667c0477 |

A chain composes: `7.fork("clump").fork(3).fork(7)` is 0x613201b5 and
`7.fork("clump").fork(7).fork(3)` is 0x29acae70. The vectors come from an
independent implementation of the formulas above; `tests/fixtures/named-seeds-v1.1.json`
holds the full set that native and WebAssembly runs reproduce.

## Rewriting existing sources

Existing programs, including `hash(seed ^ K)` and `stream.push`, are
untouched. Rewriting a hand-made key as a fork changes the derived seed and
therefore the result for the same input seed; no name maps to a historical
constant. `start = seed` stays the way to branch from a chosen point of the
stream, and `seed.initial()` the way to branch from the call's own seed.

## Language description

`ipc --describe-language` uses schema 7: the type name `seed` in signatures
and the storable, helper parameter and helper result lists; the `read_initial`
effect; `"role": "seed"` on seed parameters, `choose`'s seed and a part call's
seed; `effect_rules.seed_initial`; and `source_rules.named_seeds` with the
tags, formulas and restrictions of this document. `seed.initial` and the
`int(seed)` overload are builtins with `constant_evaluable: false`.
