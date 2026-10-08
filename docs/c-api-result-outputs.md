# Generated output slots and typed values

`IPROP_CAP_RESULT_OUTPUTS` (bit 16) advertises result output count/get/find and
the sized `iprop_result_output_view_ex` initializer. `IPROP_CAP_TYPED_OUTPUTS`
(bit 17) independently advertises point/rect/circle copies. These additive
API 0.3 operations preserve ABI line 0.4, legacy structs and packed floats.

## Slots and names

`iprop_result_output_count` counts every declared slot, not only assigned
values. `iprop_result_output_get(result, output_index, &view, error)` reads one
global slot in root declaration order followed by each part's declarations.
The IDs match [program output reflection](c-api-output-declarations.md) for the
program that generated this result. `owner == 0` is root; owner N+1 is part N.
Parts with no declarations still consume an owner number, so gaps are retained.

| View field | Meaning |
| --- | --- |
| `output_index`, `owner` | Global slot ID and its original owner |
| `kind`, `is_list` | Fixed-width point/rect/circle tag and exact 0/1 list flag |
| `element_count` | Singular 0/1, or list length; zero does not remove the slot |
| `stride_floats` | Point 2, rect 4, circle 3, matching legacy packed values |
| `name` | Exact byte-counted UTF-8 declaration name, including embedded NUL |
| `values` | Exact legacy packed-float pointer, with no copy or conversion |

Unassigned singular outputs and empty lists have `element_count == 0`. Ignore
their `values` pointer: it is NULL when the entire packed buffer is empty and
can be non-NULL otherwise, including a pointer to the end of that buffer. The
new view preserves that legacy pointer exactly. Nonempty values use the frozen
component order x/y, x/y/w/h, or x/y/radius. These are the generated values;
helpers and part D4 transformations have already applied their effects. In a
file with a parts section (format 0.5) part values are in root asset
coordinates, with every enclosing placement and transform applied once; in
older files they stay local to the part after its D4 transform. The accessors
neither repeat transforms nor reinterpret owner-relative geometry.

`iprop_result_output_find(result, owner, name, &output_index, error)` matches
both owner and complete UTF-8 name bytes. It never chooses a same-named output
under another owner. NULL name data is valid only with byte count zero; that
empty name is a real lookup, not a wildcard. Unknown names, valid empty owners,
invalid owners, malformed UTF-8 and null required outputs return `E_ARGUMENT`.
The result retains the owner count even after its program is freed.

The view follows the existing sized query contract: four-byte minimum,
absent flags zero, nonzero complete flags rejected, complete known fields only.
Size/flags, padding, partial fields and unknown tail are preserved. The name
pointer and byte count are independently bounded; inspect both only when both
fit. Initialize with actual allocation capacity, and inspect other fields only
when their complete offsets fit that capacity.

## Typed copies

`iprop_result_output_point`, `iprop_result_output_rect` and
`iprop_result_output_circle` take global slot ID, element index, a typed output
buffer, explicit `out_capacity` and an optional bounded error descriptor. Their
fixed leaf records are respectively `iprop_point_value { x, y }`,
`iprop_rect_value { x, y, w, h }` and `iprop_circle_value { x, y, radius }`.
They are not extensible descriptors and contain no hidden size/flags fields.

```c
uint32_t slot;
iprop_utf8_view name = {"anchors", 7};
if (iprop_result_output_find(result, 0, name, &slot, NULL) == IPROP_EX_OK) {
    iprop_point_value point;
    if (iprop_result_output_point(result, slot, 0, &point,
                                  sizeof(point), NULL) == IPROP_EX_OK) {
        /* point is caller-owned; it survives iprop_result_free(result). */
    }
}
```

The required buffer capacity is the whole corresponding leaf record. A smaller
capacity returns `E_ARGUMENT` without a partial copy. Larger allocations are
allowed; bytes beyond the record remain untouched. The caller promises actual
writable capacity; a false allocation claim or dangling pointer cannot be
detected. The accessor checks kind and `element_index < element_count`; no
element index is valid for an empty slot. It copies each binary32 bit pattern
without arithmetic or normalization, including negative zero, subnormal values,
NaN payloads and infinities. New reads give the same bits as legacy packed
floats, while bindings need no knowledge of the 2/4/3 component layout.

All these queries allocate no storage and run no VM. A failure leaves the
scalar/view/typed output unchanged. Optional errors follow the shared
[checked query policy](c-api-checked.md#sized-query-outputs-and-error-storage).
Malformed error storage prevents publication. Error descriptor or writable
message spans overlapping the entire claimed output allocation, or a message
span overlapping its error descriptor, return `E_ARGUMENT` without changing
either output. Success clears available error fields/text; argument failures
have `INVALID_ARGUMENT` reason, `ARGUMENTS` stage and no location coordinates.

Names and packed views belong to the result until `iprop_result_free`.
`program_free`, another successful generation, freeing that later result and a
failed generation do not invalidate them. Typed copies belong to the caller
and remain valid after the result is freed. Legacy image/output pointers and
the packed-float layout remain unchanged.
