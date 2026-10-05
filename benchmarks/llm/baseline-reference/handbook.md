# The .ips language

Version: **0.4.0** · Status: **released** · Source: `66d34c2831585afb231022da9296528a912e962e`

This is the complete authoring reference for the current compiler. `.ips`
programs generate deterministic raster images and structured geometry. The
compiler produces a self-contained `.iprop` file; hosts execute it with a
seed, variant, archetype and optional input uniforms.

The language version follows the product version. Development documentation
describes the checkout being built; a release ships the documentation from
its exact tag. The generated book identifies its version, status and source
commit. Historical contracts are available in the corresponding Git tags.

Read the chapters in order to learn the language, or use the function index
in the generated book to look up a builtin. Each chapter links actual `.ips`
files; their contents are included in the generated book and tested by CI.

1. [Your first asset](#your-first-asset)
2. [Assets, inputs and outputs](#assets-inputs-and-outputs)
3. [Source syntax and names](#source-syntax-and-names)
4. [Types, expressions and variables](#types-expressions-and-variables)
5. [Control flow](#control-flow)
6. [User functions and argument evaluation](#user-functions-and-argument-evaluation)
7. [Randomness and seeds](#randomness-and-seeds)
8. [Numbers and interpolation](#numbers-and-interpolation)
9. [Drawing and colors](#drawing-and-colors)
10. [Vectors and distance fields](#vectors-and-distance-fields)
11. [Noise and procedural utilities](#noise-and-procedural-utilities)
12. [Palettes](#palettes)
13. [Imports and part composition](#imports-and-part-composition)
14. [Compiler tools, diagnostics and emoji](#compiler-tools-diagnostics-and-emoji)
15. [Grammar](#grammar)
16. [Maintaining this documentation](#maintaining-this-documentation)

The [format and VM specifications](../spec/README.md) define binary
encoding, exact numerical/raster algorithms, validation and deterministic
execution. The authoring reference links those contracts where relevant.
The [recipe gallery](../examples/recipes/README.md) contains 24 larger
patterns combining the features described here.

## Your first asset

Save [first-asset.ips](../examples/language/first-asset.ips), then run the
installed tools:

```sh
ipc first-asset.ips -o first-asset.iprop
ipval first-asset.iprop
ipgen first-asset.iprop --seed 7 -o first-asset.png
```

Expected result: pass.

```ips
asset FirstAsset {
  size 16x16
  draw {
    rect(0, 0, 16, 16, #123)
    rect(x = 4, y = 4, w = 8, h = 8, color = #9dc)
  }
}
```

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

## Assets, inputs and outputs

A source file contains imports followed by exactly one `asset name { ... }`.
An asset requires `size` and one `draw` entry. It may declare an origin,
variant/archetype counts, inputs, outputs, palettes and helpers. Metadata
declarations are outside function bodies. `size 16x16` and `size 16 16` are
accepted. Origin coordinates are constant numeric expressions; an omitted
origin is `(0, 0)`. Omitted variant/archetype counts are both 1.

The host supplies zero-based variant and archetype indices. In `draw`, the
read-only `i32` names `v` and `a` contain those indices. An invocation outside
the declared counts traps. `draw` declares no parameters or return type.

Expected result: pass.

```ips
asset UniformAsset {
  size 16x16
  origin 8, 8
  variants 3
  archetypes 2
  input color tint = #9dc
  input f32 scale = 1.0
  input u32 density = 2
  input u32 offset = 0
  output point anchor
  output rect bounds
  output circle area
  output list point dots
  draw {
    rect(0, 0, 16, 16, #123)
    rect(2 + offset, 2, 4 * scale + v, 4 + a, tint)
    anchor = {8, 8}
    bounds = {2, 2, 4 * scale + v, 4 + a}
    area = {8, 8, 3}
    for i in 0..density { dots = {i, 8} }
  }
}
```

### Input uniforms

`input type name = literal` declares a host-settable, read-only uniform.

| Input type | Literal default | Value inside `draw` |
| --- | --- | --- |
| `color` | `#RGB`, `#RGBA`, `#RRGGBB`, `#RRGGBBAA` | `color` |
| `f32` | integer or floating literal | `f32` |
| `u32` | integer literal | `i32`, retaining its 32-bit pattern |

The parser also recognizes `input i32`, but the current compiler rejects
it because the binary format provides `u32` uniforms. Use `input u32` for
integer inputs; expressions interpret the resulting pattern as `i32`.

Defaults are literal tokens, not expressions. A leading minus is a unary
operator, so `input f32 amount = -1.0` is rejected. For an integer bit pattern
such as signed -1, use `0xffffffff`. Inside expressions, negative values are
written normally. Boolean tokens are not accepted as integer input defaults.
Uniforms are visible in `draw`; pass their values explicitly to helpers.

Expected result: compile-error (`input i32 is not supported`).

```ips
asset InvalidSignedInput {
  size 8x8
  input i32 amount = 0
  draw { }
}
```

Input names `seed`, `v`, `a`, `at`, `rot`, `mirror_x` and `mirror_y` are
reserved. Inputs and outputs cannot share a name; declarations of the same
kind must have unique names. Inputs cannot be assigned or used as loop counters.

### Structured outputs

`output kind name` declares a scalar slot. Assignment replaces its current
value; an unwritten slot remains unset. `output list kind name` appends each
assignment in execution order. Outputs are written by `draw` or a helper
belonging to the same asset, and returned to the host alongside the image.

| Kind | Assignment value |
| --- | --- |
| `point` | `{x, y}`, a `vec2` |
| `circle` | `{x, y, radius}` |
| `rect` | `{x, y, width, height}` |

Components must be numeric and widen to `f32`. Scalar part outputs can be
read and propagated; list outputs cannot be read as expressions. Outputs
are not ordinary readable local variables. See [composition](#imports-and-part-composition)
for placement and transformation of part-owned outputs.

The VM validates the resulting executable and bounds its resources. Generation
is subject to the host's canvas limits and fuel budget; see
[execution limits](#execution-limits).

## Source syntax and names

Source must be valid UTF-8. Whitespace separates tokens; newlines do not end
statements. `//` comments extend to the next newline and `/* ... */` comments
end at the first closing delimiter (they do not nest). Semicolons are optional
separators inside function blocks; metadata declarations do not use them.

Ordinary identifiers match `[A-Za-z_][A-Za-z0-9_]*` and are case-sensitive.
Control/declaration keywords (`asset`, `input`, `output`, `import`, `from`,
`if`, `else`, `while`, `for`, `in`, `break`, `continue`, `return`, `list`,
`true`, `false`) are reserved. `draw`, `at`, type names and geometry kind
names are soft keywords accepted in name positions; `draw` still selects
the entry block when used as an asset-level function name. Asset names and
`for` counter names require an ordinary identifier. Asset-level `size`,
`origin`, `variants`, `archetypes` and `palette` introduce metadata.

Use descriptive names distinct from builtin functions and the namespaces
`color`, `vec2`, `dist`, `noise` and `cell`. Builtin call resolution takes
precedence over user helpers with the same name.

### Literals

- Integers: decimal digits or `0x`/`0X` plus hexadecimal digits. They become
  32-bit patterns, so `0xffffffff` is the expression value -1. Use ordinary
  decimal without leading zeroes; literal parsing also recognizes leading-zero
  octal forms. Values outside the literal parser's range are rejected.
- Floats: digits with a fractional part and/or exponent, such as `2.5`,
  `1e3`, `4.0e-2`. The fractional part requires digits on both sides of the
  dot; write `0.5`, not `.5`. Evaluation uses IEEE-754 binary32.
- Colors: `#RGB` and `#RGBA` repeat each digit; `#abc` is `#aabbcc`.
  Long forms are `#RRGGBB` and `#RRGGBBAA`. Missing alpha is 255.
- `true` and `false` are `i32` values 1 and 0.
- Strings are quoted import paths, with escapes `\n`, `\t`, `\r`, `\"` and
  `\\`. Strings are not runtime values.

There are no source literals for infinity or NaN; floating operations can
produce them. Numeric minus is an operator, including in palette expressions:
palettes require literal tokens and therefore do not accept negative unary
expressions. Use suitable integer bit-pattern literals when needed.

Expected result: pass.

```ips
// Comments can contain arbitrary valid UTF-8: współrzędne.
asset SourceSyntax {
  size 8x8
  palette colors = [#abc, #1234, #abcdef, #abcdef80,]
  draw {
    /* Newlines and semicolons are optional block separators. */
    x = 0x1; y = 1e0
    point = {x, y,}
    pixel(int(point.x), int(point.y), colors[0],)
    value = -3 + int(2.5)
    if (true && !false) { pixel(value + 2, 2, #fff) }
  }
}
```

Trailing commas are accepted in call arguments, helper parameters, palette
elements and compound literals. [Emoji aliases](#emoji-aliases)
normalize to the same word tokens. Arbitrary Unicode identifiers, variation
selectors and joined emoji sequences are not accepted as syntax.

## Types, expressions and variables

### Value types and conversions

| Type | Meaning | Can be stored, passed and returned? |
| --- | --- | --- |
| `i32` | signed interpretation of a 32-bit integer pattern | yes |
| `f32` | IEEE-754 binary32 | yes |
| `color` | RGBA8 channels | yes |
| `vec2` | two `f32` components | yes |
| `circle`, `rect` | three/four numeric components for outputs | output writes only |
| `void` | absence of a result | call statements only |

`u32` is an input declaration type, not a local/helper type. A point is
represented by `vec2`; there is no separate storable `point` type.

Numeric arithmetic mixes `i32` and `f32` by widening the integer to `f32`.
Builtin/helper arguments and return values also permit this widening.
Local reassignment requires an exact type match. Use `int(x)` or `float(x)`
when an explicit conversion is needed; no implicit float-to-integer conversion
is performed. Integer-to-float conversion can lose precision.

### Variables and scope

The first assignment to a fresh name declares a local and fixes its type.
Locals belong to the whole function, including those first assigned inside
a branch or loop; braces do not introduce shadowing scopes. Source is checked
in traversal order. Initialize a local before branching if it is needed later:
the language does not perform definite-assignment analysis, and untouched
runtime local slots have the default value of their type.

Helpers have their own locals. Parameters are read-only. `draw` also has
read-only `v`, `a` and inputs; helpers receive these values through parameters.
`seed` reads the current RNG state in either kind of function and cannot be
assigned. Palettes are asset-level constants visible to draw and helpers.

Expected result: pass.

```ips
asset ValueTypes {
  size 8x8
  draw {
    count = 2
    amount = float(count)
    count = int(amount + 0.5)
    selected = 0
    if (count > 1) { selected = 1 }
    p = {1, 2}
    q = -p + {4, 4}
    q = q * {1, 2} / {1, 2}
    q = 2 * q / 2
    mask = ((1 << count) | 1) ^ 2
    mask = (mask & 7) >> 1
    if (mask < (1 << 4) && selected) {
      pixel(int(q.x), int(q.y), #9dc)
    }
    notMask = ~mask
    remainder = count % 2
  }
}
```

### Operators

Binary operators associate leftward. The precedence below goes from lowest
to highest; parentheses explicitly select grouping.

| Precedence | Operators | Operands |
| --- | --- | --- |
| 1 | `\|\|` | `i32`, short-circuit |
| 2 | `&&` | `i32`, short-circuit |
| 3–5 | `\|`, `^`, `&` | `i32` |
| 6 | `==`, `!=` | numeric |
| 7 | `<`, `<=`, `>`, `>=` | numeric |
| 8 | `<<`, `>>` | `i32` |
| 9 | `+`, `-` | numeric or vector/vector |
| 10 | `*`, `/`, `%` | numeric; `%` only `i32`; vector forms below |
| 11 | unary `-`, `!`, `~` | numeric/vector minus; `i32` logical/bitwise not |
| 12 | call, member access, palette index | postfix expressions |

The current parser accepts a shift expression as the first comparison operand,
but subsequent comparison operands use additive expressions. Parenthesize a
shift on the right: `value < (1 << bits)`. The [grammar](#grammar) records
this restriction explicitly.

Comparisons and logical operations return normalized `i32` 0/1. `&&` and
`||` skip the right operand when its value cannot affect the result, including
its RNG effects. Other binary operands evaluate left to right.
Integer arithmetic wraps modulo 2³². `/` and `%` use signed integer semantics
when both operands are integers; division by zero traps. Shift counts are
masked to five bits, and `>>` is logical (zero-filling). Floating division
uses IEEE-754 results, including NaN/infinity.

Vectors use `{x, y}`, expose `.x` and `.y`, support componentwise `+`, `-`,
`*`, `/` with another vector, vector `*`/`/` scalar, scalar `*` vector and
unary minus. Scalar/vector `+`, `-`, `/`, vector comparisons and vector `%`
are rejected. Components are values rather than assignable fields.

Expected result: compile-error (`cannot assign f32`).

```ips
asset InvalidLocal {
  size 8x8
  draw { x = 1; x = 1.5 }
}
```

This example is intentionally rejected: a local fixed as `i32` cannot be
reassigned an `f32`. CI checks the expected diagnostic.

## Control flow

`if (condition) { ... }`, optional `else if` branches and an optional `else`
select blocks. `while (condition) { ... }` re-evaluates its condition before
each iteration. Conditions must be `i32`: zero is false and any nonzero
value is true. Compare a float or explicitly convert it instead of using it
directly as a condition.

`for i in start..end { ... }` iterates over a half-open integer range.
The start is evaluated, assigned to the counter, then the end is evaluated
once and retained. The counter must be an `i32` local; a fresh counter is
declared and remains visible after the loop. An existing `i32` local is reused.
The loop tests `i < end`, runs the body and increments `i`. Start ≥ end
produces no iterations. Assigning to the counter inside the body is allowed
and changes subsequent iteration behavior.

`break` exits the innermost loop in the current function. `continue` moves
to its next iteration; in a `for` loop it still executes the increment.
Neither statement can target a caller's loop. Inputs, outputs, palettes,
parameters and reserved names cannot serve as counters.

`return value` returns from a typed helper; `return;` returns from a void
helper or draw. Since newlines are whitespace, use the semicolon when a bare
return precedes another statement; before `}` a bare `return` is sufficient.
A value-returning helper must return on every reachable path, as required
by the bytecode validator.

Expected result: pass.

```ips
asset Loops {
  size 8x8
  draw {
    rect(0, 0, 8, 8, #123)
    for i in 0..8 {
      if (i == 1) { continue }
      if (i >= 6) { break }
      pixel(i, 2, #9dc)
    }
    j = 0
    while (j < 3) {
      pixel(j, 4, #fc9)
      j = j + 1
    }
    if (j == 0) { pixel(0, 0, #f00) }
    else if (j == 3) { pixel(3, 3, #0f0) }
    else { pixel(0, 0, #00f) }
    return;
  }
}
```

Expected result: compile-error (`condition must be an integer`).

```ips
asset InvalidCondition {
  size 8x8
  draw { if (0.5) { pixel(1, 1, #fff) } }
}
```

The second example deliberately fails the integer condition check. A
nonterminating loop instead compiles and exhausts runtime fuel:

Expected result: trap (`fuel exhausted`).

```ips
asset FuelTrap {
  size 1x1
  draw { while (1) { } }
}
```

See [execution limits](#execution-limits) for host control of fuel.

## User functions and argument evaluation

A helper is an asset-level `name(parameter: type, ...) { ... }` declaration.
All parameter types are explicit: `i32`, `f32`, `color` or `vec2`.
A value-returning helper declares `-> type`; omission means a void helper.
Arguments are positional, arity must match, and `i32` may widen to `f32`.
Forward calls are allowed. Direct and indirect recursion are rejected.

Pass uniforms and `v`/`a` to helpers explicitly. Helpers share the executing
asset's RNG stream and may draw or write its declared output slots. A helper
cannot access a caller's local variables or mutate its own parameters.
Calling a value-returning function as a statement discards its result.

Expected result: pass.

```ips
asset Helpers {
  size 8x8
  input color tint = #9dc
  output point anchor
  draw {
    p = shifted({1, 1}, 2)
    paint(p, tint)
    amount = twice(2)
  }
  shifted(p: vec2, amount: f32) -> vec2 { return p + {amount, amount} }
  paint(p: vec2, tint: color) {
    pixel(int(p.x), int(p.y), tint)
    anchor = p
    return;
  }
  twice(n: i32) -> f32 { return n * 2 }
}
```

### Argument binding and evaluation

Builtin and canvas calls accept positional and named arguments, including
a mixture. Positional arguments fill signature slots from the start;
named arguments bind the indicated slot. Names do not advance the positional
counter. Duplicate, missing and unknown arguments are rejected.

Ordinary builtin arguments evaluate once, in signature order, independent
of written name order. `rect(color = #fff, h = 2, x = 1, w = 2, y = 1)` has
the same argument evaluation order as `rect(1, 1, 2, 2, #fff)`.
`poly` evaluates coordinate pairs before its color, although its positional
color argument comes first. Helpers evaluate positional arguments left to
right. Part calls have their [own order](#part-call-order).

`pick`/`choose` evaluate all choices eagerly; only logical `&&`/`||` provide
short-circuit evaluation. RNG effects remain even when a selected result is
discarded. `roll`/`choose` isolate their stream after argument evaluation.

Expected result: compile-error (`has no argument 'colour'`).

```ips
asset InvalidArgument {
  size 8x8
  draw { pixel(x = 1, y = 1, colour = #fff) }
}
```

Expected result: compile-error (`only visible inside 'draw'`).

```ips
asset InvalidUniform {
  size 8x8
  input color tint = #9dc
  draw { paint() }
  paint() { pixel(1, 1, tint) }
}
```

Expected result: compile-error (`recursive call`).

```ips
asset InvalidRecursion {
  size 8x8
  draw { recurse() }
  recurse() { recurse() }
}
```

These rejected examples demonstrate unknown parameter names, direct uniform
access from a helper and recursion respectively.

## Randomness and seeds

Random operations use the current deterministic 32-bit stream. `seed` reads
its current state, so saving `saved = seed` before drawing preserves an
explicit seed value. Changing the number or order of stream draws changes
later results. Hash/noise operations with explicit seeds do not advance it.

Expected result: pass.

```ips
asset RandomChoices {
  size 8x8
  draw {
    saved = seed
    u = unit()
    x = int(range(0, 8))
    y = index(8)
    indexAlias = pick(8)
    c = pick(value1 = #fc9, value0 = #9dc)
    if (chance(0.75)) { pixel(x, y, c) }
    isolated = roll(hash(saved), 0, 8)
    nested = choose(saved, choose(saved ^ 1, 1, 2), 3)
    stream_push(saved ^ 2)
    pixel(int(isolated), nested, #fff)
    stream_pop()
  }
}
```

### unit
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `unit() -> f32` | rng.unit | 0.1 |

Consumes one stream draw and returns an `f32` in `[0, 1)`. The frozen stream
advance and conversion are specified in [RNG](../spec/iprop-v0.3.md#122-stream-advance-rngunit).

### range
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `range(a: f32, b: f32) -> f32` | rng.range | 0.1 |

Consumes one draw and computes `a + (b - a) * unit()` with pinned binary32
steps. Bounds are not reordered. Ordinary ordered bounds describe a uniform
range; floating rounding follows the exact arithmetic contract.

### index
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `index(n: i32) -> i32` | rng.index | 0.1 |

Consumes one draw and selects using the unsigned interpretation of `n`.
Zero returns zero; a negative `i32` is a large unsigned bound, not an error.
Use a positive bound for ordinary palette/array selection.

### pick
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `pick(n: i32) -> i32` | rng.index | 0.1 |
| `pick(value0: T, value1: T, ...) -> T` | compiler lowering | — |

The variadic form requires at least 2 arguments.
`T` is a shared storable type; mixed numeric choices widen to `f32`.

`pick(n)` is exactly `index(n)`. With two or more arguments it eagerly
evaluates choices, then consumes one selection draw. Choices must share a
storable type; mixed numeric choices widen to `f32`. Named choice arguments
are `value0`, `value1`, etc. A single non-integer choice is rejected because
the single-argument overload is the index alias.

### chance
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `chance(p: f32) -> i32` | compiler lowering | — |

Returns `i32` 0/1 for `unit() < p` after evaluating `p`. It consumes a draw
even when `p <= 0`, `p >= 1`, or `p` is NaN. NaN yields false.

### roll
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `roll(seed: i32, a: f32, b: f32) -> f32` | compiler lowering | — |

Draws a range using an explicit `i32` seed. Arguments evaluate in signature
order before seed isolation. The isolated operation consumes one draw and
restores the outer stream to its state after argument evaluation.

### choose
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `choose(seed: i32, value0: T, ...) -> T` | compiler lowering | — |

The variadic form requires at least 2 arguments.
`T` is a shared storable type; mixed numeric choices widen to `f32`.

Selects from one or more eager typed choices using an explicit seed. Choice
type rules match multi-choice `pick`. A single choice still performs an
isolated draw. Nested isolated calls restore each caller's stream. Named
arguments are `seed`, `value0`, `value1`, etc.

### hash
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `hash(x: i32) -> i32` | hash | 0.1 |

Returns the frozen splitmix32 hash of the argument's bit pattern as `i32`.
It does not change the current stream. It is useful for deriving independent
seeds from a saved seed and a coordinate or index.

### stream_push
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `stream_push(seed: i32) -> void` | stream.push | 0.1 |

Saves the current stream and derives a nested stream from the supplied seed
according to the VM contract. Pair it with `stream_pop` before returning.
This is stream derivation, not the direct seed assignment used by `roll`.

### stream_pop
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `stream_pop() -> void` | stream.pop | 0.1 |

Restores the most recently pushed stream. An unmatched pop traps. Nesting
is bounded by the VM's stream-stack limit. Balanced calls isolate a sequence
of draws while retaining the caller's stream.

## Numbers and interpolation

The signatures below come from the compiler. Numeric parameters typed `f32`
accept `i32` widening. Domain errors in floating math generally produce the
specified IEEE result rather than a compile error; consuming a nonfinite
result in an operation with a checked domain can trap.

Expected result: pass.

```ips
asset NumberHelpers {
  size 8x8
  output point result
  draw {
    x = int(2.8)
    f = float(x)
    truncated = i32trunc(-2.8)
    rounded = round(2.5)
    root = sqrt(f)
    angle = atan2(1, 1)
    wave = sin(angle) + cos(angle)
    bounds = min(floor(abs(-f)), ceil(root))
    bounds = max(bounds, 1)
    growth = pow(bounds, 2) + exp(0)
    blend = lerp(0, growth, smooth(0.5))
    contrast = gain(quintic(0.25), 0.5)
    edge = smoothstep(0, 1, contrast)
    mapped = remap(edge, 0, 1, 1, 4)
    result = {clamp(mapped, 0, 7), blend}
    pixel(x, rounded, #9dc)
  }
}
```

### int
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `int(x: i32) -> i32` | compiler lowering | — |
| `int(x: f32) -> i32` | f32.to.i32 | 0.1 |

An `i32` is preserved exactly. An `f32` truncates toward zero with saturation
to the signed 32-bit bounds; NaN becomes 0. Use this conversion for integer
pixel coordinates and conditions.

### float
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `float(x: f32) -> f32` | compiler lowering | — |
| `float(x: i32) -> f32` | i32.to.f32 | 0.1 |

Widens `i32` to binary32. An existing `f32` is preserved, including negative
zero. Integers beyond binary32 precision can round.

### i32trunc
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `i32trunc(x: f32) -> i32` | f32.to.i32 | 0.1 |

The explicit floating truncation operation: same saturated/NaN conversion
as the `f32` overload of `int`.

### round
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `round(x: f32) -> i32` | compiler lowering | — |

Returns an `i32` using the compiler's `floor(x + 0.5)` lowering followed by
saturated truncation. Halfway values therefore round toward positive infinity,
not away from zero.

### sqrt
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `sqrt(x: f32) -> f32` | f32.sqrt | 0.1 |

Binary32 square root. Negative arguments yield NaN; negative zero is
preserved according to the floating contract.

### sin
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `sin(x: f32) -> f32` | f32.sin | 0.2 |

Pinned sine approximation, with angle in radians. Exact range reduction,
nonfinite behavior and polynomial evaluation are specified in
[trigonometry](../spec/iprop-v0.3.md#127-trigonometry-v02).

### cos
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `cos(x: f32) -> f32` | f32.cos | 0.2 |

Pinned cosine approximation, with angle in radians and the same normative
trigonometry contract as `sin`.

### abs
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `abs(x: f32) -> f32` | f32.abs | 0.1 |

Floating magnitude. It accepts numeric widening and returns `f32`.

### floor
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `floor(x: f32) -> f32` | f32.floor | 0.2 |

Rounds a float downward and returns a float. Convert explicitly when an
integer result is required.

### ceil
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `ceil(x: f32) -> f32` | f32.ceil | 0.2 |

Rounds a float upward and returns a float.

### min
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `min(x: f32, y: f32) -> f32` | f32.min | 0.2 |

Returns the lesser value with pinned minNum semantics: one NaN selects the
other argument, two NaNs produce canonical NaN, and negative zero precedes
positive zero.

### max
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `max(x: f32, y: f32) -> f32` | f32.max | 0.2 |

Returns the greater value with the corresponding maxNum semantics; positive
zero succeeds negative zero.

### pow
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `pow(x: f32, y: f32) -> f32` | f32.pow | 0.2 |

Pinned floating exponentiation. Zero, negative bases, integer/noninteger
exponents, NaN and infinity cases follow
[the math contract](../spec/iprop-v0.3.md#128-the-math-package-v02).

### exp
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `exp(x: f32) -> f32` | f32.exp | 0.2 |

Pinned base-e exponential. Overflow/underflow and nonfinite handling are
defined by the same math contract.

### atan2
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `atan2(y: f32, x: f32) -> f32` | f32.atan2 | 0.2 |

Returns the pinned angle in radians. The argument order is `y, x`; signed
zero and quadrant handling follow the math specification.

### smooth
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `smooth(t: f32) -> f32` | smooth | 0.1 |

Clamps `t` to `[0, 1]` and applies cubic smoothstep `t*t*(3 - 2*t)` using
the frozen operation order.

### lerp
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `lerp(a: f32, b: f32, t: f32) -> f32` | lerp | 0.1 |
| `lerp(a: color, b: color, t: f32) -> color` | color.mix | 0.1 |

Interpolates numbers or colors with clamped `t`. Numeric inputs widen to
`f32`. Color interpolation has the same behavior as `color.mix`, including
preservation of the first color's alpha.

### gain
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `gain(t: f32, k: f32) -> f32` | gain | 0.3 |

Schlick contrast curve. `t` clamps to `[0, 1]`, `k` to `[1e-4, 1]`.
`k = 0.5` is identity; `k = 1` returns 0.5 for interior positions and retains
endpoints. A NaN argument produces canonical NaN before clamping.

### quintic
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `quintic(t: f32) -> f32` | quintic | 0.3 |

Clamped quintic smootherstep, also used by gradient noise's fade function.

### smoothstep
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `smoothstep(a: f32, b: f32, t: f32) -> f32` | smoothstep | 0.3 |

Maps `t` between two edges through clamped smooth interpolation. Exact
equal-edge and reversed-edge behavior follows
[the curve toolkit](../spec/iprop-v0.3.md#1214-the-curve-toolkit-v03).

### remap
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `remap(t: f32, a: f32, b: f32, c: f32, d: f32) -> f32` | remap | 0.3 |

Maps a value from interval `[a, b]` to `[c, d]`; this is a range transform,
not an automatic clamp. Degenerate intervals follow the frozen float rules.

### clamp
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `clamp(x: f32, min: f32, max: f32) -> f32` | clamp | 0.3 |

Bounds a numeric value using the pinned min/max operations. Parameter names
are `x`, `min`, `max`. NaN and signed-zero behavior comes from those operations.

## Drawing and colors

Raster calls paint the current canvas in call order. Pixel centers and edge
coverage are fixed by [canvas algorithms](../spec/iprop-v0.3.md#11-canvas-operations--exact-algorithms).
Out-of-canvas coverage is clipped. Geometry arguments must be finite and
within the operation's representable domain; invalid geometry can trap.
The generated signatures distinguish integer pixels/thickness from floating
geometry.

The frozen compositor interpolates RGB for translucent sources while retaining
the destination alpha. Start with an opaque background for translucent glaze
examples. Exact blending, alpha endpoints and rounding are specified in
[pixel blending](../spec/iprop-v0.3.md#111-pixelx-y-colour).

Expected result: pass.

```ips
asset CanvasCalls {
  size 16x16
  draw {
    rect(0, 0, 16, 16, #123)
    pixel(1, 1, #fff)
    line(1, 2, 14, 2, 1, #9dc)
    oval(4, 8, 2, 3, #fc9)
    poly(color = #99f, x0 = 8, y0 = 5, x1 = 14, y1 = 8, x2 = 8, y2 = 11)
    curve({1, 12}, {4, 7}, {7, 12}, 1.0, #fff)
    curve({8, 12}, {10, 7}, {12, 7}, {14, 12}, 1.0, #9dc)
    weather(seed, 1)
  }
}
```

### pixel
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `pixel(x: i32, y: i32, color: color) -> void` | pixel | 0.1 |

Paints one integer pixel with a color, subject to clipping and blending.

### rect
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `rect(x: f32, y: f32, w: f32, h: f32, color: color) -> void` | rect | 0.1 |

Paints an axis-aligned rectangle using floating x/y/width/height and the
normative pixel-center fill rules.

### line
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `line(ax: f32, ay: f32, bx: f32, by: f32, thickness: i32, color: color) -> void` | line | 0.1 |

Paints a segment using floating endpoints and an integer thickness. The
segment rasterizer's endpoint and cap rules are frozen.

### curve
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `curve(a: vec2, b: vec2, c: vec2, thickness: f32, color: color) -> void` | curve | 0.3 |
| `curve(a: vec2, b: vec2, c: vec2, d: vec2, thickness: f32, color: color) -> void` | curve.cubic | 0.3 |

Paints a quadratic or cubic Bézier stroke depending on whether three or
four control points are supplied. Thickness is a floating diameter in target
pixels; strokes have round caps. Nonfinite geometry is rejected. Distance
estimation and coverage follow the pinned bounded Bézier kernel.

### oval
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `oval(x: f32, y: f32, rx: f32, ry: f32, color: color) -> void` | oval | 0.1 |

Paints an ellipse centered at x/y with radii rx/ry, using the exact raster
contract. Use valid positive radii for ordinary ellipses.

### poly
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `poly(color: color, x0: f32, y0: f32, x1: f32, y1: f32, x2: f32, y2: f32, ...) -> void` | compiler lowering | — |

The variadic form requires at least 7 arguments.
Additional coordinates repeat as `xN, yN` pairs; positional color comes first.

Paints a filled polygon with at least three `(x, y)` pairs. Positional color
comes first. Named coordinates are `x0`, `y0`, `x1`, `y1`, etc., with `color`.
Coordinates evaluate first in vertex order, then color, even for reordered
named arguments. The point-in-polygon rule is specified in the canvas contract.

### weather
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `weather(seed: i32, amount: i32) -> void` | weather | 0.1 |

Applies the pinned seeded wear operation to the current image. Amount is an
integer wear parameter; the explicit seed makes it independent of stream
draws. It operates on pixels already drawn, so place it after the base shapes.

### Color helpers

Expected result: pass.

```ips
asset ColorHelpers {
  size 8x8
  draw {
    base = rgb(r = 32, g = 64, b = 96)
    glaze = rgba(128, 192, 160, 128)
    mixed = color.mix(base, #9dc, 0.5)
    alias = lerp(base, #9dc, 0.5)
    shaded = color.shade(mixed, 10)
    opaque = color.set.a(glaze, 255)
    multiplied = color.mul(opaque, #ccc)
    scaled = color.scale(multiplied, 0.8)
    adjusted = color.hsv(scaled, 0.25, 1.0, 1.0)
    rect(0, 0, 8, 8, base)
    rect(2, 2, 4, 4, glaze)
    pixel(1, 1, adjusted)
  }
}
```

#### rgb
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `rgb(r: i32, g: i32, b: i32) -> color` | compiler lowering | — |

Builds an opaque color from integer red/green/blue channels clamped to 0–255.

#### rgba
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `rgba(r: i32, g: i32, b: i32, a: i32) -> color` | compiler lowering | — |

Builds a color with an integer alpha channel; all four channels clamp to 0–255.

#### color.mix
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `color.mix(a: color, b: color, t: f32) -> color` | color.mix | 0.1 |

Interpolates RGB with clamped floating `t`, retaining the first argument's
alpha. The color overload of `lerp` is equivalent.

#### color.shade
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `color.shade(color: color, amount: i32) -> color` | color.shade | 0.1 |

Offsets RGB brightness by the integer amount with channel clamping. Alpha
is preserved. Bare `shade`/`mix` names are not builtins in the current language.

#### color.set.a
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `color.set.a(color: color, alpha: i32) -> color` | color.set.a | 0.3 |

Replaces alpha with a clamped integer channel while retaining RGB.

#### color.mul
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `color.mul(a: color, b: color) -> color` | color.mul | 0.3 |

Multiplies color channels using the pinned integer conversion. See
[color utilities](../spec/iprop-v0.3.md#1212-the-colours-v03) for alpha and rounding.

#### color.scale
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `color.scale(color: color, scale: f32) -> color` | color.scale | 0.3 |

Scales RGB by a floating factor with pinned conversion/clamping; preserves
alpha. Nonfinite behavior follows the color utility contract.

#### color.hsv
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `color.hsv(color: color, hue: f32, saturation: f32, value: f32) -> color` | color.hsv | 0.3 |

Adjusts hue by turns and scales saturation/value according to the frozen HSV
conversion, preserving alpha. Nonfinite hue shifts trap; this is a color
adjustment, not a constructor from three standalone HSV channels.

## Vectors and distance fields

Vectors and all geometry calculations use binary32. Signed distances are
negative inside closed shapes, zero on their boundary and positive outside.
Segment/Bézier distances are unsigned. Distance queries do not paint pixels;
combine them with `fill` and a color operation to author coverage.

Expected result: pass.

```ips
asset GeometryHelpers {
  size 8x8
  output point result
  draw {
    p = {2, 3}
    length = vec2.len(p)
    product = vec2.dot(p, {1, 0})
    rotated = vec2.rot(p, 0.25)
    polarValue = polar(rotated)
    folded = fold(p, 4)
    circleDistance = dist.circle(p, {4, 4}, 2)
    rectDistance = dist.rect(p, {4, 4}, {2, 2})
    rounded = dist.roundrect(p, {4, 4}, {2, 2}, 0.5)
    segment = dist.line(p, {0, 0}, {7, 7})
    quadratic = dist.bezier(p, {0, 0}, {3, 7}, {7, 0})
    cubic = dist.bezier(p, {0, 0}, {2, 7}, {5, 7}, {7, 0})
    coverage = fill(circleDistance, 1)
    result = folded
    pixel(2, 3, color.scale(#9dc, coverage))
  }
}
```

### vec2.len
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `vec2.len(v: vec2) -> f32` | vec2.len | 0.3 |

Returns vector length using the pinned multiply/add/square-root order.

### vec2.dot
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `vec2.dot(a: vec2, b: vec2) -> f32` | vec2.dot | 0.3 |

Returns the scalar dot product of two vectors.

### vec2.rot
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `vec2.rot(v: vec2, a: f32) -> vec2` | vec2.rot | 0.3 |

Rotates a vector by a floating angle in radians using pinned sine/cosine.
Positive angles are counterclockwise in the downward-y plane. This continuous
vector rotation is distinct from the exact discrete part transforms.

### dist.circle
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `dist.circle(p: vec2, center: vec2, radius: f32) -> f32` | dist.circle | 0.3 |

Signed distance to a circle defined by a center and radius.

### dist.rect
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `dist.rect(p: vec2, center: vec2, half_size: vec2) -> f32` | dist.rect | 0.3 |

Signed distance to a centered axis-aligned rectangle. `half_size` contains
half-width and half-height, not its full dimensions.

### dist.roundrect
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `dist.roundrect(p: vec2, center: vec2, half_size: vec2, radius: f32) -> f32` | dist.roundrect | 0.3 |

Signed distance to a centered rounded rectangle with half-dimensions and a
corner radius. Supply dimensions/radius suitable for the shape being authored.

### dist.line
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `dist.line(p: vec2, a: vec2, b: vec2) -> f32` | dist.line | 0.3 |

Distance to a finite segment. Degenerate segments and nonfinite inputs follow
the exact [distance-field contract](../spec/iprop-v0.3.md#129-signed-distance-fields-v03).

### dist.bezier
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `dist.bezier(p: vec2, a: vec2, b: vec2, c: vec2) -> f32` | dist.bezier | 0.3 |
| `dist.bezier(p: vec2, a: vec2, b: vec2, c: vec2, d: vec2) -> f32` | dist.bezier.cubic | 0.3 |

Distance estimate to a quadratic/cubic Bézier curve on `[0, 1]`, selected by
the control-point count. The kernel performs a fixed bounded search rather
than an arbitrary host-library approximation. Nonfinite geometry traps.

### fill
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `fill(d: f32, w: f32) -> f32` | fill | 0.3 |

Converts signed distance and a transition width to floating coverage.
The exact zero/nonpositive width and NaN rules are in the distance-field spec.

### fold
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `fold(v: vec2, n: i32) -> vec2` | vec2.fold | 0.3 |

Folds a vector into an n-sector kaleidoscope domain, returning a `vec2`.
The sector count is an integer. Valid counts and angular folding follow
[fold and polar](../spec/iprop-v0.3.md#1213-the-dither-the-fold-and-the-polar-v03).

### polar
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `polar(v: vec2) -> vec2` | vec2.polar | 0.3 |

Returns `{radius, angle}` for a vector; angle is in radians under the same
downward-y convention as vector rotation. Signed-zero behavior is pinned.

## Noise and procedural utilities

These functions use explicit seeds and do not consume the current RNG stream.
Keep coordinates modest unless testing the documented domains. All derived
coordinates are checked too; a finite starting coordinate can leave a valid
domain after octave scaling or warping.

Expected result: pass.

```ips
asset NoiseHelpers {
  size 8x8
  output point feature
  draw {
    saved = seed
    value = noise(0.5, 0.5, saved)
    triangle = fault(0.5, 0.5, saved)
    gradient = noise.grad(0.5, 0.5, saved)
    fractal = noise.fbm(0.5, 0.5, 3, saved)
    cells = noise.cell(0.5, 0.5, saved)
    ridge = noise.ridge(0.5, 0.5, 3, saved)
    warped = warp(0.5, 0.5, 0.2, saved)
    feature = cell.jitter(3, 4, saved)
    coverage = dither(0.5, 2, 3)
    pixel(2, 3, color.scale(#9dc, coverage))
  }
}
```

### noise
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `noise(x: f32, y: f32, seed: i32) -> f32` | noise | 0.1 |

Frozen value noise over the lattice, with interpolation defined by
[the original noise contract](../spec/iprop-v0.3.md#124-noisex-y-seed).

### fault
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `fault(x: f32, y: f32, seed: i32) -> f32` | fault | 0.1 |

Frozen triangular noise/fault primitive with explicit coordinates and seed;
its exact hashing and interpolation are in the adjacent fault specification.

### noise.grad
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `noise.grad(x: f32, y: f32, seed: i32) -> f32` | noise.grad | 0.3 |

Gradient noise. Coordinates must be finite, with each floor in
`[-2147483648, 2147483646]`. A failure traps with `noise coordinate out of range`.

### noise.fbm
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `noise.fbm(x: f32, y: f32, octaves: i32, seed: i32) -> f32` | noise.fbm | 0.3 |

Normalized octave sum of gradient noise. The integer octave count clamps to
1–8. The gradient coordinate domain is checked at every octave, after
frequency scaling.

### noise.cell
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `noise.cell(x: f32, y: f32, seed: i32) -> f32` | noise.cell | 0.3 |

Worley F1 distance using the complete pinned 5×5 search window. Coordinates
must be finite, with floors in `[-16777214, 16777214]`.

### noise.ridge
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `noise.ridge(x: f32, y: f32, octaves: i32, seed: i32) -> f32` | noise.ridge | 0.3 |

Normalized ridged octave sum. Octaves clamp to 1–8, with the same per-octave
gradient domain checks as `noise.fbm`.

### warp
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `warp(x: f32, y: f32, amount: f32, seed: i32) -> f32` | warp | 0.3 |

Domain-warped noise using the fixed inner/outer four-octave fbms. Amount must
be finite; all offset and warped samples must meet the gradient domains.

### cell.jitter
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `cell.jitter(cx: i32, cy: i32, seed: i32) -> vec2` | cell.jitter | 0.3 |

Returns an absolute point inside `[cx, cx+1) × [cy, cy+1)`. Integer cell
origins must be exactly representable as binary32. All integers within
±16777216 qualify; larger ones must also be representable. A rejected origin
traps with `cell index is not representable as f32`.

### dither
| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `dither(t: f32, x: i32, y: i32) -> f32` | dither | 0.3 |

Applies the pinned ordered Bayer threshold to `t` at integer pixel x/y.
It returns floating coverage. Matrix indexing and threshold comparisons are
specified in [dither](../spec/iprop-v0.3.md#1213-the-dither-the-fold-and-the-polar-v03).

The complete algorithms and domain checks are in
[noise v2](../spec/iprop-v0.3.md#1210-the-noise-v2-v03).
This example deliberately traps on a nonfinite derived coordinate:

Expected result: trap (`noise coordinate out of range`).

```ips
asset NoiseTrap {
  size 1x1
  input f32 coordinate = 1.0
  draw { value = noise.grad(coordinate / 0.0, 0, seed) }
}
```

## Palettes

`palette name = [literal, ...]` declares a non-empty, immutable asset-level
array. All elements must be literal tokens of one type: `i32`, `f32` or
`color`; boolean literals count as `i32`. Mixed integer/float palettes and
computed elements are rejected. Palettes are available in draw and helpers,
and cannot share a name with an input, output, helper or another palette.

`name[index]` requires an `i32` index and returns the element type. A negative
or out-of-range index selects the last element. Palettes are the only values
that can be indexed; they cannot be passed to ordinary helpers, assigned to
locals or used bare except as the first `ramp` argument.

Expected result: pass.

```ips
asset PaletteHelpers {
  size 8x8
  palette colors = [#123, #9dc, #fc9]
  palette levels = [0.0, 0.5, 1.0]
  palette integers = [1, 2, 3]
  draw {
    c = colors[index(3)]
    last = integers[-1]
    continuous = ramp(colors, 0.5)
    intensity = ramp(t = 0.5, palette = levels)
    rect(0, 0, 8, 8, c)
    pixel(last, 2, color.scale(continuous, intensity))
  }
}
```

### ramp

| Signature | VM instruction | Minimum VM format |
| --- | --- | --- |
| `ramp(palette: palette<f32>, t: f32) -> f32` | compiler lowering | — |
| `ramp(palette: palette<color>, t: f32) -> color` | compiler lowering | — |

Interpolates a `color` or `f32` palette with numeric `t` clamped to `[0, 1]`.
It returns the palette's element type. Integer palettes are rejected; write
floating literals when continuous numeric interpolation is wanted. A single
element returns that element, but still evaluates `t` exactly once, including
its RNG effects. Named arguments are `palette` and `t`.

Color interpolation preserves the first selected color's alpha. Exact
clamping, NaN behavior and lowering are described in
[the v0.3 ramp contract](../spec/iprop-v0.3.md#1211-the-curves-and-the-ramps-v03).

Expected result: compile-error (`ramp requires a color or f32 palette`).

```ips
asset InvalidRamp {
  size 8x8
  palette values = [1, 2, 3]
  draw { result = ramp(values, 0.5) }
}
```

## Imports and part composition

`import alias from "path.ips"` precedes the asset. The current compiler
supports direct parts only: an imported asset cannot declare imports of its
own. Duplicate aliases, missing files, cycles and nested part imports are
errors. The CLI resolves import paths against the entry source's directory.
Keep imported files together using paths relative to that entry directory.
The compiler links parts into one executable; generation does not load
source files.

Expected result: pass.

```ips
asset Part {
  size 4x4
  origin 2, 2
  input color tint = #9dc
  output point anchor
  output list point dots
  draw {
    rect(0, 0, 4, 4, tint)
    anchor = {2, 1}
    dots = {1, 1}
  }
}
```

Expected result: pass.

```ips
import leaf from "part.ips"
asset Composition {
  size 16x16
  output point attached
  draw {
    leaf.draw(seed, v, a, at = {8, 8}, tint = #fc9, rot = 90, mirror_x = 1)
    attached = leaf.outputs.anchor
    pixel(int(leaf.outputs.anchor.x), int(leaf.outputs.anchor.y), #fff)
  }
}
```

`alias.draw(seed, variant, archetype, at = {x, y}, uniforms...)` supplies
exactly three positional integers, a required named placement and optional
named input overrides. Unspecified uniforms use the part's defaults. `at`
positions the part's declared origin. Call order determines paint order;
the parent's RNG stream is restored after the part executes.

Optional transform arguments are compile-time integers: `rot` is 0, 90, 180
or 270; `mirror_x`/`mirror_y` are 0 or 1, including boolean constants.
Each can occur once. Rotation is applied before the mirrors. Positive angles
are counterclockwise in the downward-y plane; transforms are exact D4 pixel
permutations, not filtered image rotation.

Scalar `alias.outputs.name` reads the last called part's output, including
point `.x`/`.y`. A read before that slot has been set traps. Assigning that
value to a parent's output propagates it. List outputs are host-facing and
cannot be read this way. Part-owned points, circles, rectangles and appended
lists receive placement/transforms, including writes in part helpers. Parent
outputs retain parent coordinates.

### Part call order

Uniform overrides evaluate in the part's input declaration order. Then the
seed, variant, archetype and placement expressions evaluate. Transform
arguments are compile-time constants. Each dynamic argument evaluates once.
See [parts and D4 transforms](../spec/iprop-v0.3.md#105-part-transforms-v03)
for the exact geometry mapping and rectangle bounds.

Expected result: compile-error (`cannot read list output`).

```ips
import leaf from "../part.ips"
asset InvalidListRead {
  size 8x8
  draw { p = leaf.outputs.dots }
}
```

The following imports the composition example, which itself imports a part,
and is therefore deliberately rejected:

Expected result: compile-error (`imported part 'leaf' was not provided`).

```ips
import branch from "composition.ips"
asset InvalidNestedPart {
  size 16x16
  draw { branch.draw(7, 0, 0, at = {0, 0}) }
}
```

## Compiler tools, diagnostics and emoji

### Compiler output

`ipc file.ips -o file.iprop` validates and compiles the entry and imports.
Production output is optimized, debug-stripped and losslessly compressed
when compression saves space. `--debug` retains source maps/symbols;
`--strip` restores the default (the last flag wins). `--no-compress` writes
an expanded file; `--no-opt` disables compiler optimization for inspection.
`ipval` validates a binary, `ipdis` disassembles it and `ipgen` generates
pixels/outputs. The `.ipa` assembly language is the VM-level tool format.

`ipc entry.ips --format canonical.ips` also writes canonical parseable source.
The output uses two-space indentation, explicit expression grouping, word
keywords, normalized literals and named builtin arguments in signature order.
It must differ from the input and binary output. Comments are absent from
the AST and therefore from formatted output. Keep the formatted file beside
its imports when recompiling it. Formatting is idempotent and preserves
executable bytes after stripping source-line debug data.

The C++ compilation API retains debug information so callers can select an
artifact policy. The editor requests it for inspection; binary exports use
production defaults. [Optimization behavior](optimizations-v0.4.md) describes
the passes and their preservation contract.

### Diagnostics

Errors identify the file and one-based line/Unicode codepoint column; tabs
count as one column. The VS Code extension maps these locations to UTF-16
positions. Close misspellings suggest canonical names, including transpositions.
Parsing/type errors fail compilation without producing a valid executable.
Runtime traps are distinct: the program can compile but a given invocation
can exceed a resource limit or encounter invalid data.

Expected result: compile-error (`cannot assign to input`).

```ips
asset InvalidInputWrite {
  size 8x8
  input color tint = #9dc
  draw { tint = #fff }
}
```

Expected result: compile-error (`literal default value`).

```ips
asset InvalidDefault {
  size 8x8
  input f32 amount = -1.0
  draw { }
}
```

Expected result: compile-error (`cannot store rect`).

```ips
asset InvalidCompound {
  size 8x8
  draw { bounds = {0, 0, 4, 4} }
}
```

### Execution limits

The compiler currently emits a default fuel budget of 1,000,000. The host
can supply its invocation budget; each executed instruction/drawing operation
retains its normative cost. Optimizations may reduce total fuel without
changing image/output bits. A loop need not be statically bounded, but
execution traps when fuel runs out.

Canvas dimensions are limited to 16384 per side in the reference runtime.
Hosts may impose smaller limits on dimensions or total pixel counts.
Binary pools/indices, function nesting, stream stacks
and structured outputs also have bounds; see
[validation and resource limits](../spec/iprop-v0.3.md#14-validation).
Compressed v0.4 files bound both encoded and decoded size to 64 MiB.
Host-specific limits and invalid variant/archetype indices can reject an
invocation before draw starts.

Integer division/remainder by zero traps; float division follows IEEE-754.
Noise coordinate checks, nonfinite HSV hue, invalid geometry, unset part
output reads and unbalanced stream pops can also trap. Traps must be handled
by the host; a partial image is not a successful generation result.

Expected result: trap (`divide by zero`).

```ips
asset DivisionTrap {
  size 1x1
  input u32 divisor = 0
  draw { value = 1 / divisor }
}
```

### Emoji aliases

The lexer accepts 18 exact single Unicode scalars alongside their canonical
word spellings. The alias table is generated from the lexer's actual registry.

| Word | Alias | Unicode scalar |
| --- | --- | --- |
| `asset` | 📦 | U+1F4E6 |
| `draw` | ✏ | U+270F |
| `input` | 📥 | U+1F4E5 |
| `output` | 📤 | U+1F4E4 |
| `palette` | 🌈 | U+1F308 |
| `import` | 🔗 | U+1F517 |
| `color` | 🎨 | U+1F3A8 |
| `vec2` | 📐 | U+1F4D0 |
| `variants` | 🔀 | U+1F500 |
| `archetypes` | 🗂 | U+1F5C2 |
| `point` | 🔘 | U+1F518 |
| `rect` | 🟦 | U+1F7E6 |
| `circle` | ⭕ | U+2B55 |
| `true` | 👍 | U+1F44D |
| `false` | 👎 | U+1F44E |
| `noise` | 🌀 | U+1F300 |
| `dist` | 📏 | U+1F4CF |
| `cell` | 🦠 | U+1F9A0 |

Aliases normalize before parsing, so diagnostics, the AST and formatter use
word names. Strings/comments retain their original text. Variation selectors
(including U+FE0F) and ZWJ sequences are rejected in syntax. `draw` uses
U+270F alone and `archetypes` U+1F5C2 alone. Namespace members retain words:
`📏.circle(...)` is valid, while `dist.⭕(...)` is rejected. Other syntax keeps
its word spelling. Both forms compile to the same executable.

Expected result: pass.

```ips
📦 EmojiAsset {
  size 8x8
  📥 🎨 tint = #9dc
  📤 🔘 anchor
  🌈 colors = [#123, #9dc]
  ✏ {
    🟦(0, 0, 8, 8, colors[0])
    anchor = {4, 4}
    if (👍 && !👎) { pixel(4, 4, tint) }
  }
}
```

## Grammar

This EBNF records the current parser. Braces in the grammar mean repetition,
square brackets mean optional syntax and quoted strings are literal tokens.
`identifier` is an ordinary identifier; `name` additionally accepts soft
keywords as described in [source syntax](#source-syntax-and-names). Semantic checks such as
types, literal-only defaults/palettes, required declarations and acyclic calls
apply after parsing. Strings are only valid as import paths after typing.

```ebnf
program       = { import }, asset, EOF ;
import        = "import", name, "from", string ;
asset         = "asset", identifier, "{", { declaration }, "}" ;
declaration   = size | origin | variants | archetypes | input | output
              | palette | draw | helper ;
size          = "size", integer, (integer | xdigits) ;
origin        = "origin", expression, ",", expression ;
variants      = "variants", integer ;
archetypes    = "archetypes", integer ;
input         = "input", input_type, name, "=", literal ;
input_type    = "color" | "f32" | "u32" | "i32" ;
output        = "output", [ "list" ], output_kind, name ;
output_kind   = "point" | "circle" | "rect" ;
palette       = "palette", name, "=", "[", [ expressions ], "]" ;
draw          = "draw", block ;
helper        = name, "(", [ parameters ], ")", [ "->", value_type ], block ;
parameters    = parameter, { ",", parameter }, [ "," ] ;
parameter     = name, ":", value_type ;
value_type    = "i32" | "f32" | "color" | "vec2" ;
block         = "{", { ";" | statement }, "}" ;
statement     = assignment | call_statement | if | while | for
              | "break" | "continue" | return ;
assignment    = name, "=", expression ;
call_statement = name, { postfix_suffix } ;  (* must end as a call AST *)
if            = "if", "(", expression, ")", block,
                [ "else", (if | block) ] ;
while         = "while", "(", expression, ")", block ;
for           = "for", identifier, "in", expression, "..", expression, block ;
return        = "return", [ expression ] ;
expression    = logical_or ;
logical_or    = logical_and, { "||", logical_and } ;
logical_and   = bit_or, { "&&", bit_or } ;
bit_or        = bit_xor, { "|", bit_xor } ;
bit_xor       = bit_and, { "^", bit_and } ;
bit_and       = equality, { "&", equality } ;
equality      = comparison, { ("==" | "!="), comparison } ;
comparison    = shift, { ("<" | "<=" | ">" | ">="), additive } ;
shift         = additive, { ("<<" | ">>"), additive } ;
additive      = multiplicative, { ("+" | "-"), multiplicative } ;
multiplicative = unary, { ("*" | "/" | "%"), unary } ;
unary         = ("-" | "!" | "~"), unary | postfix ;
postfix       = primary, { postfix_suffix } ;
postfix_suffix = ".", name | "(", [ arguments ], ")"
               | "[", expression, "]" ;
primary       = literal | name | compound | "(", expression, ")" ;
compound      = "{", [ expressions ], "}" ;
expressions   = expression, { ",", expression }, [ "," ] ;
arguments     = argument, { ",", argument }, [ "," ] ;
argument      = [ name, "=" ], expression ;
literal       = integer | floating | color | string | "true" | "false" ;
identifier    = (letter | "_"), { letter | digit | "_" } ;
xdigits       = ("x" | "X"), digit, { digit } ;
integer       = digit, { digit } | "0", ("x" | "X"), hex, { hex } ;
floating      = digits, ".", digits, [ exponent ] | digits, exponent ;
exponent      = ("e" | "E"), [ "+" | "-" ], digits ;
color         = "#", (3 hex | 4 hex | 6 hex | 8 hex) ;
```

In `return`, the optional expression is omitted when the next token is `;`
or `}`; newline alone does not terminate it. Numeric token conversion has
the [literal restrictions](#literals) described above. The parser
allows empty palette/compound syntax, but semantic checks reject empty
palettes and compounds whose arity is not 2, 3 or 4. Metadata keywords are
recognized contextually; their names cannot introduce helpers in that position.

The comparison production intentionally reflects the current asymmetric
shift restriction. Parentheses make shifted right operands unambiguous.

## Maintaining this documentation

Edit these Markdown chapters for explanations, limitations and examples.
`book.json` fixes their reading order. The site, downloadable book, function
signatures and search index are generated during the build. The overview
must link every chapter in book order; CI checks the contents and fragments.

An `example` directive includes a real source file; keep its relative path
valid from the chapter. Register each example in
[cases.json](../examples/language/cases.json) as a successful generation,
an expected compile error or an expected runtime trap. CI requires every
example to be registered and referenced by the book, and exercises every
documented builtin in the successful examples.

Each public builtin has exactly one `builtin` directive followed by its
handwritten behavior and limitations. Signatures/types/named parameter order
come from `ipc --describe-language`, including variadic forms. The emoji
table comes from the same export and the lexer registry. Adding a builtin
requires an explanation and executable example; missing/duplicate descriptions
fail documentation checks.

```sh
python3 scripts/language_docs.py check --compiler build/tools/ipc
python3 scripts/language_docs.py build --compiler build/tools/ipc
npm --prefix website run docs
npm --prefix website run test:docs
```

Checks validate relative links and fragments, compile and execute examples,
verify expected failures, and compare generated content with the current
compiler/version. HTML build checks validate rendered anchors and search entries.
The native install, WASM archive and website include the generated documentation
and its source/version manifest; packaging verifies their extracted copies.
Full native builds generate `build/language-docs/` when Python 3.10 or newer
is available. Installed SDKs contain it under `share/iprop/`; WASM archives
and the website use the same `docs/`, `examples/` and `spec/` layout.
The manifest binds all handbook chapters, numerical contracts and examples
to their file digests, product version and source commit. Release collection
rejects missing files, changed inputs, foreign commits and documentation
generated from uncommitted changes. Native install checks also compare the
catalog with `ipc --describe-language` from the installed compiler.

Release documentation is built from the exact release tag. Development builds
are labeled as development/candidate documentation and identify their source
commit. Historical versions remain in their Git tags, rather than copies
updated accidentally alongside the current chapters.
