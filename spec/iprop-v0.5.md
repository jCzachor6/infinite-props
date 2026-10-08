# `.iprop` Format and Virtual Machine — Specification v0.5

**Status:** Draft v0.5 contract (2026-10-08), frozen when product 0.6.0 is
released.
**Scope:** Colour channel reads, uniform random ranges and distributions. The
v0.4 section, type, container, mathematical, canvas, output, fuel and
validation semantics remain frozen.
This document inherits [v0.4](iprop-v0.4.md), with the changes below taking
precedence. The `.ips` language and compiler optimizations are separate.

## 1. Versions and header flags

An interpreter supporting 0.5 MUST continue to accept files with versions
0.1–0.4 under their existing validation and execution rules. A plain 0.5
file has the same 20-byte header and section layouts as 0.4.

The reference compiler and assembler write the oldest minor that holds every
instruction of the file, and never older than 0.4: a file without a 0.5
instruction remains a 0.4 file, readable by 0.4 interpreters.

The compressed envelope of v0.4 §2 is unchanged and remains a 0.4
container: header flag bit 1 MUST appear only with major 0, minor 4, so a
0.5 header with bit 1 set is invalid. The inner file of an envelope MAY now
have any supported uncompressed version, 0.1–0.5. A 0.4 interpreter rejects
such an inner 0.5 file by its version, after expansion.

## 2. Colour channel reads

| Op | Mnemonic | Stack | Semantics |
| -- | -------- | ----- | --------- |
| 0xA4 | color.r | `[c] -> [i32]` | the R byte, zero-extended |
| 0xA5 | color.g | `[c] -> [i32]` | the G byte, zero-extended |
| 0xA6 | color.b | `[c] -> [i32]` | the B byte, zero-extended |
| 0xA7 | color.a | `[c] -> [i32]` | the A byte, zero-extended |

Each instruction pops one colour and pushes the stored channel as an `i32`
in 0–255. Alpha is read exactly as stored; no channel is premultiplied,
normalized or otherwise converted. The instructions never trap, do not read
or advance the RNG stream, and do not touch the canvas or outputs. Each
costs the ordinary 1 fuel unit of v0.3 §8.3.

The `aux` byte MUST be zero; the operand is unused, as for the other
operand-less instructions. The 0xA4–0xA7 opcodes MUST appear only in files
with minor version 5 or later, including in unreachable code; an older file
containing one is invalid.

## 3. Uniform random ranges

| Op | Mnemonic | Stack | Semantics |
| -- | -------- | ----- | --------- |
| 0xA8 | rng.int | `[i32 i32] -> [i32]` | uniform integer in `[lo, hi]`, both ends included (§3.1) |
| 0xA9 | rng.float | `[f32 f32] -> [f32]` | uniform value in `[lo, hi]` (§3.2) |

`lo` is the deeper operand and `hi` the top. Both instructions advance the
current stream (v0.3 §8.2) with the step of v0.3 §12.2, `state = state +
0x9e3779b9` (wraparound), and use `hash(state)` (v0.3 §12.1) as the draw of
that step. They do not touch the canvas or outputs. The `aux` byte MUST be
zero; the operand is unused. The 0xA8–0xA9 opcodes MUST appear only in
files with minor version 5 or later. `rng.range` and `rng.index` keep their
v0.3 algorithms.

Bounds are checked before any step. `lo > hi` traps with a numeric-range
error; for `rng.float`, a NaN or infinite bound traps the same way. Equal
bounds are valid: the result is that bound, and the instruction still
advances the stream.

### 3.1 rng.int

All arithmetic below is exact (at least 64-bit); the result wraps to `i32`.

```
width    = hi - lo + 1                       // 1 .. 2^32
accepted = floor(2^32 / width) * width
repeat for draw = 1 .. 32:
    state = state + 0x9e3779b9
    x = hash(state)
    if x < accepted or draw == 32:
        return lo + (x mod width)
```

Draws at or past `accepted` would favour the smallest results, so they are
rejected and the next step is drawn. For a width of 2^32 or any power of two
up to it, the first draw is always accepted. The instruction costs 1 fuel
unit, plus 1 unit for each draw after the first, charged before that draw. If
the budget runs out, generation traps with the ordinary fuel error.

The 32nd draw is accepted unconditionally, which bounds the loop. With this
generator the bound is never reached: every rejection set lies within
`[2^31 + 1, 2^32)`, the set of width 2^31 + 1, and over the full cycle of
2^32 stream states the longest run of draws in that set is 30. An instruction
therefore takes at most 31 draws, and no draw is ever forced.

### 3.2 rng.float

```
state = state + 0x9e3779b9
u = (f32)(hash(state) >> 8) / 16777216.0f    // unit(), in [0, 1)
r = lo * (1 - u) + hi * u                    // binary32 operations, no fused multiply-add
if r < lo: r = lo
if r > hi: r = hi
return r
```

`1 - u` is exact. Rounding may move `r` past a bound (or, for bounds near the
binary32 maximum, to infinity); the comparisons return the bound instead, so
the result always lies in `[lo, hi]`, and equal bounds return that bound. The
instruction costs 1 fuel unit and advances the stream once.

## 4. Distributions

### 4.1 Values

Type code 4, `distribution`, MAY appear among function parameters, results
and locals of files with minor version 5 or later; an older file using it is
invalid. A distribution value occupies one stack slot and holds a family and
four binary32 parameters:

| Family | Parameters |
| --- | --- |
| gaussian | mean, sigma |
| triangular | min, peak, max |
| beta | alpha, beta, min, max |

The typed zero of a distribution local (v0.3 §8.2) is a value of no family.
Distributions are not inputs, outputs or constants.

### 4.2 Constructors

| Op | Mnemonic | Stack | Valid parameters |
| -- | -------- | ----- | ---------------- |
| 0xAA | distribution.gaussian | `[f32 f32] -> [dist]` | mean finite, sigma finite and > 0 |
| 0xAB | distribution.triangular | `[f32 f32 f32] -> [dist]` | all finite, min < max, min <= peak <= max |
| 0xAC | distribution.beta | `[f32 f32 f32 f32] -> [dist]` | alpha, beta finite and > 0; min, max finite, min < max |

Parameters are popped in reverse, so the first parameter is the deepest
operand. Invalid parameters trap with a numeric-range error. A constructor
does not touch the stream, canvas or outputs and costs 1 fuel unit.

### 4.3 Draws

| Op | Mnemonic | Stack | Result |
| -- | -------- | ----- | ------ |
| 0xAD | rng.int.with | `[i32 i32 dist] -> [i32]` | §4.4 draw on `[lo, hi]`, rounded |
| 0xAE | rng.float.with | `[f32 f32 dist] -> [f32]` | §4.4 draw on `[lo, hi]` |

The instruction pops the distribution, `hi` and `lo`, then traps with a
numeric-range error, in this order, for: a distribution of no family; for
0xAE a NaN or infinite bound; `lo > hi`. Otherwise it advances the current
stream exactly once (v0.3 §12.2), takes `x = hash(state)` and
`u = (x + 0.5) * 2^-32` in binary64 (so `0 < u < 1`), and runs §4.4 with the
bounds and parameters converted exactly to binary64. Further traps:

| Condition | Message |
| --- | --- |
| `lo == hi` outside the natural range | random range lies outside the distribution |
| no probability, §4.4 | random range has no probability under the distribution |
| an iteration limit, §4.5 | distribution draw did not converge |

0xAD rounds the binary64 result to the nearest integer, halves away from
zero (`t = trunc(r)`, `t + 1` if `r - t >= 0.5`, `t - 1` if `r - t <= -0.5`).
0xAE rounds it to binary32 and replaces a result below `lo` or above `hi`
with that bound. The instruction costs 1 fuel unit plus 1 for every
iteration marked (*) in §4.5, charged after the draw; a trap there is the
ordinary fuel error. The 0xAA-0xAE opcodes MUST appear only in files with
minor version 5 or later.

### 4.4 Truncated sampling

A family has a lower distribution function `F` and a natural range
`[sa, sb]`: unbounded for gaussian, `[min, max]` for triangular and beta.
Its mirror is the distribution of `-X`: gaussian `(-mean, sigma)`;
triangular `(-max, -peak, -min)`; beta `(beta, alpha, -max, -min)`. `F'` is
the mirror's function, so `F'(-x) = P(X >= x)`. Each `F` keeps its relative
accuracy for small results, so every probability below is taken from the
tail in which it is small.

```
if lo == hi:
    trap outside if lo < sa or lo > sb
    return lo
L = max(lo, sa); H = min(hi, sb)
if not (L < H): trap no probability
below = F(L)                       // P(X <= L)
above = F'(-H)                     // P(X >= H)
if below >= 0.5:   mass = F'(-L) - above
elif above >= 0.5: mass = F(H) - below
else:              mass = (1 - above) - below
if not (mass >= 2^-1000): trap no probability
p = below + u * mass
if p <= 0.5: r = Q(p, L, H)
else:        r = -Q'(above + (1 - u) * mass, -H, -L)
r = min(max(r, L), H)
```

`Q(p, L, H)` is the family's quantile for `p <= 0.5` on the bracket
`[L, H]`, and `Q'` the mirror's:

- gaussian: `mean + sigma * normalQuantile(p)`;
- triangular, with `a = min`, `c = peak`, `b = max`: `a + sqrt(p * (b - a) * (c - a))`
  if `p < (c - a) / (b - a)`, else `b - sqrt((1 - p) * (b - a) * (b - c))`;
- beta: `min + betaQuantile(alpha, beta, p, (L - min) / w, (H - min) / w) * w`
  with `w = max - min`.

`F` is `normalCdf((x - mean) / sigma)` for gaussian;
`incompleteBeta(alpha, beta, (x - min) / (max - min))` for beta; for
triangular `0` if `x <= a`, `1` if `x >= b`, `(x - a) * (x - a) / ((b - a) * (c - a))`
if `x < c`, else `1 - (b - x) * (b - x) / ((b - a) * (b - c))`.

### 4.5 Kernels

All arithmetic is IEEE-754 binary64 with round-to-nearest; expressions are
evaluated exactly as written, left to right, without fused multiply-add or
reassociation. Only `+ - * /`, `sqrt`, `abs`, `trunc`, `floor`, `frexp` and
`ldexp` are used, so every result is reproducible bit for bit. An iteration
marked (*) costs 1 fuel unit; reaching its limit stops the draw with
"did not converge".

```
exp(x):  if x > 709.782712893384: return +inf
         if x < -745.2: return 0
         k = floor(x * 1.4426950408889634 + 0.5)
         r = (x - k * 6.93147180369123816490e-01) - k * 1.90821492927058770002e-10
         e = 1; for n = 13 down to 1: e = 1 + r * e / n
         return ldexp(e, k)

log(x):  (m, e) = frexp(x)                    // m in [1/2, 1)
         if m < 0.7071067811865476: m = m * 2; e = e - 1
         s = (m - 1) / (m + 1); s2 = s * s
         q = 1 / 25; for k = 11 down to 0: q = q * s2 + 1 / (2k + 1)
         return e * 6.93147180369123816490e-01 + (2 * s * q + e * 1.90821492927058770002e-10)

gaussExp(z):  a = abs(z); h = trunc(a * 16) / 16
              return exp(-(h * h) * 0.5) * exp(-((a - h) * (a + h)) * 0.5)

normalCdf(z):
    x = abs(z) * 0.7071067811865476
    if x < 1.5:
        x2 = x * x; term = x; total = x
        for n = 1, 2, ... (*, limit 100):
            term = term * (2 * x2) / (2n + 1); total = total + term
            stop if term <= total * 2^-53
        erf = 1.1283791670955126 * gaussExp(z) * total
        return z < 0 ? 0.5 * (1 - erf) : 0.5 * (1 + erf)
    f = x; c = x; d = 0
    for n = 1, 2, ... (*, limit 256):
        a = n * 0.5
        d = x + a * d; if d == 0: d = 1e-300
        c = x + a / c; if c == 0: c = 1e-300
        d = 1 / d; delta = c * d; f = f * delta
        stop if abs(delta - 1) <= 2^-52
    erfc = gaussExp(z) * 0.5641895835477563 / f
    return z < 0 ? 0.5 * erfc : 1 - 0.5 * erfc
```

`normalQuantile(p)` is Wichura's algorithm AS241 (PPND16), evaluated with the
`log` above: `q = p - 0.5`; if `abs(q) <= 0.425` then with
`r = 0.180625 - q * q` it returns `A(r) * q / B(r)`; otherwise
`r = sqrt(-log(q <= 0 ? p : 1 - p))`, and for `r <= 5` the value is
`C(r - 1.6) / D(r - 1.6)`, else `E(r - 5) / F(r - 5)`, negated when `q < 0`.
Each polynomial is evaluated by Horner's rule from its highest coefficient
(`(...(k7 * r + k6) * r + ...) * r + k0`):

| | k0 | k1 | k2 | k3 | k4 | k5 | k6 | k7 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| A | 3.3871328727963666080e+0 | 1.3314166789178437745e+2 | 1.9715909503065514427e+3 | 1.3731693765509461125e+4 | 4.5921953931549871457e+4 | 6.7265770927008700853e+4 | 3.3430575583588128105e+4 | 2.5090809287301226727e+3 |
| B | 1 | 4.2313330701600911252e+1 | 6.8718700749205790830e+2 | 5.3941960214247511077e+3 | 2.1213794301586595867e+4 | 3.9307895800092710610e+4 | 2.8729085735721942674e+4 | 5.2264952788528545610e+3 |
| C | 1.42343711074968357734e+0 | 4.63033784615654529590e+0 | 5.76949722146069140550e+0 | 3.64784832476320460504e+0 | 1.27045825245236838258e+0 | 2.41780725177450611770e-1 | 2.27238449892691845833e-2 | 7.74545014278341407640e-4 |
| D | 1 | 2.05319162663775882187e+0 | 1.67638483018380384940e+0 | 6.89767334985100004550e-1 | 1.48103976427480074590e-1 | 1.51986665636164571966e-2 | 5.47593808499534494600e-4 | 1.05075007164441684324e-9 |
| E | 6.65790464350110377720e+0 | 5.46378491116411436990e+0 | 1.78482653991729133580e+0 | 2.96560571828504891230e-1 | 2.65321895265761230930e-2 | 1.24266094738807843860e-3 | 2.71155556874348757815e-5 | 2.01033439929228813265e-7 |
| F | 1 | 5.99832206555887937690e-1 | 1.36929880922735805310e-1 | 1.48753612908506148525e-2 | 7.86869131145613259100e-4 | 1.84631831751005468180e-5 | 1.42151175831644588870e-7 | 2.04426310338993978564e-15 |

```
logGamma(x):  if x < 0.5: return logGamma(x + 1) - log(x)
              x = x - 1; s = g0; for i = 1 .. 8: s = s + g_i / (x + i)
              t = x + 7.5
              return (0.9189385332046728 + (x + 0.5) * log(t)) - t + log(s)
    g = 0.99999999999980993, 676.5203681218851, -1259.1392167224028,
        771.32342877765313, -176.61502916214059, 12.507343278686905,
        -0.13857109526572012, 9.9843695780195716e-6, 1.5056327351493116e-7

logBeta(a, b) = logGamma(a) + logGamma(b) - logGamma(a + b)

fraction(a, b, x):                              // modified Lentz
    c = 1; d = 1 - (a + b) * x / (a + 1); d = tiny(d); d = 1 / d; h = d
    for m = 1, 2, ... (*, limit 300):
        m2 = 2 * m
        aa = m * (b - m) * x / (((a - 1) + m2) * (a + m2))
        d = tiny(1 + aa * d); c = tiny(1 + aa / c); d = 1 / d; h = h * (d * c)
        aa = -(a + m) * ((a + b) + m) * x / ((a + m2) * ((a + 1) + m2))
        d = tiny(1 + aa * d); c = tiny(1 + aa / c); d = 1 / d
        delta = d * c; h = h * delta
        stop if abs(delta - 1) <= 2^-52
    return h
    tiny(v) = abs(v) < 1e-300 ? 1e-300 : v; a + b, a + 1, a - 1 are computed once

incompleteBeta(a, b, t):
    if t <= 0: return 0;  if t >= 1: return 1
    front = exp(a * log(t) + b * log(1 - t) - logBeta(a, b))
    if t < (a + 1) / (a + b + 2): return front * fraction(a, b, t) / a
    return 1 - front * fraction(b, a, 1 - t) / b

logIncompleteBeta(a, b, t, lb):               // log of the above, 0 < t < 1
    front = a * log(t) + b * log(1 - t) - lb
    if t < (a + 1) / (a + b + 2): return front + log(fraction(a, b, t) / a)
    return log(1 - exp(front) * fraction(b, a, 1 - t) / b)

betaQuantile(a, b, p, lo, hi):                // Newton on log I over log t
    lb = logBeta(a, b); lp = log(p); t = 0.5 * (lo + hi)
    for each iteration (*, limit 64):
        h = logIncompleteBeta(a, b, t, lb) - lp
        if abs(h) <= 2^-30: return t
        if h < 0: lo = t else hi = t
        next = lo == 0 ? hi * 2^-64 : hi > 2 * lo ? sqrt(lo) * sqrt(hi) : 0.5 * (lo + hi)
        ratio = exp((h + lp) - (a * log(t) + (b - 1) * log(1 - t) - lb))
        candidate = t * exp(-h * ratio)
        if lo < candidate < hi: next = candidate
        if next <= lo: return lo
        if next >= hi: return hi
        if abs(next - t) <= 2^-50 * next or hi - lo <= 2^-50 * hi: return next
        t = next
```

The limits are never reached by `normalCdf` (at most 97 iterations) and, in
the reference test corpus, by `betaQuantile` for shapes between 0.01 and
2000. Very large or very small beta shapes can reach them; the draw then
traps instead of returning an inaccurate value. The `2^-30` tolerance is
finer than the `2^-32` resolution of `u`.

## 5. Conformance

Every 0.1–0.4 file MUST produce the same pixels, structured output bits,
RNG state, traps and fuel use as under v0.4. Backends MUST return the four
channel bytes of every colour exactly, including alpha 0 and 255, and MUST
reproduce the range and distribution results, stream states, traps and fuel
use of §3 and §4 bit for bit.
