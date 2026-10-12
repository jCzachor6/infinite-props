# Immutable lists

`list<T>` holds zero to 256 values of one element type: `i32`, `f32`, `color`,
`vec2` or `distribution`. These are ordinary value types for locals, helper
parameters, helper returns and asset constants. Runtime lists also accept
[`sampler<T>` descriptions](samplers.md). Lists of lists and
surfaces are outside the implemented stage. A list owns immutable data;
passing, returning or assigning its handle shares that data.

<!-- example: ../../examples/language/collections.ips -->

## Literals and types

Square brackets construct a list. Elements evaluate once, from left to right,
in the caller's stream. `[10 to 15, 20 to 30]` contains two sampled integers;
reading either element again performs no draw. A distribution element is a
description, so `[gaussian(0.5, 0.18)]` itself performs no draw.

All elements must agree in type. Mixed numeric elements widen `i32` to `f32`.
When a literal has an expected `list<f32>` type, its integers widen element
by element. Existing list values never convert between `list<i32>` and
`list<f32>` automatically. A local retains its original element type.

An empty literal needs a known type from an asset constant, helper parameter,
helper return, typed local or reassignment to an existing list local:

```ips
const list<color> noColors = []
empty() -> list<vec2> { return [] }
draw {
  list<i32> items = []
  items = [1, 2]
  items = []
}
```

`items = []` with a fresh untyped name and `list.len([])` have no element
context and are rejected. Element assignment, append, comprehensions and
dynamic list growth are not supported. Assign a complete new list to a local.

## Index and length

`items[index]` evaluates the list before its `i32` index, each once. An index
below zero or at least the length traps with `IndexRange` and `list index out
of range`. An unassigned list handle traps when used; it is different from
an assigned empty list. There is no implicit conversion of an `f32` index.

<!-- builtin: list.len -->

Returns the list length as `i32`, without iteration or RNG. Its overloads,
like all builtin signatures, come from the compiler catalogue. It also accepts
a first-class palette of at most 256 elements.

## Polygons

`poly(points, tint)` accepts `list<vec2>` and `color`; named arguments are
`points` and `color`. Points evaluate before color, independent of written
named order. A list with fewer than three vertices traps at execution.
Rasterization, scaling, clipping, alpha and polygon visit fuel use exactly
the existing variadic polygon kernel. Constructing a dynamic list adds its
own construction fuel; a previously stored list is shared on each call.

## Constants and palettes

`const list<color> stone = [#343a42, #929ca8, #d8dce2]` uses the same acyclic,
pure compile-time resolver as scalar constants. Forward references, indexing,
`list.len` and valid distribution constructors are allowed. Invalid constant
indices or constructors are source errors, including in unused declarations.
RNG, generation inputs, `seed`, user helpers and part calls remain forbidden.

Legacy `palette` declarations retain literal-only elements, `ramp` semantics
and the last-element fallback for out-of-range direct indexing. Their old
size limits are unchanged. A palette used as a value or passed to a helper
becomes `list<T>` and must contain at most 256 elements. Indexing that shared
list through a local or helper parameter uses the ordinary checked index.

## Resources, format and errors

List construction uses an internal builder and freezes its element buffer;
it never pushes every component simultaneously onto the 512-slot stack.
Builder values cannot be declared, copied or passed by source code. A trap
releases unfinished builders and prevents publishing a partial result.

Each generation owns an arena. Immutable allocations are charged once even
when many handles share them. The 64 MiB arena limit also includes additional
part canvases in format 0.6. Older files keep their existing allocation rules.
Accounting uses fixed conservative 128-byte headers and 64-byte element slots
on both native and WASM, and four bytes per part pixel. Memory and fuel are
checked before allocation or rasterization.

List types or instructions retained in the file require format 0.6. A list
used only to compute a scalar asset constant introduces no runtime collection
and may still emit 0.4. Public C input/output enums and layouts are unchanged;
lists are internal values, not new host uniforms or geometric outputs.

<!-- example: ../../examples/language/errors/list-empty.ips -->
<!-- example: ../../examples/language/errors/list-mixed.ips -->
<!-- example: ../../examples/language/errors/list-nested.ips -->
<!-- example: ../../examples/language/errors/list-assignment.ips -->
<!-- example: ../../examples/language/errors/list-index-type.ips -->
<!-- example: ../../examples/language/errors/const-list-index.ips -->
<!-- example: ../../examples/language/traps/list-index.ips -->
<!-- example: ../../examples/language/traps/list-unassigned.ips -->
<!-- example: ../../examples/language/traps/poly-list-size.ips -->
