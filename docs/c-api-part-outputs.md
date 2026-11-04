# Typed dynamic parts (C API 0.5, ABI 0.5)

Negotiate `IPROP_CAP_PART_OUTPUTS` and `IPROP_CAP_RESULT_RESOURCES`. Load with
`iprop_program_load_ex` and generate with `iprop_generate_ex`, setting
`options.flags = IPROP_GENERATE_PART_OUTPUTS`. Any part declaration, including
empty and uncalled slots, requires this explicit flag. Without it the checked
generation returns `E_UNSUPPORTED` before execution or result allocation.
This protects API 0.4 callers that use checked generation followed by an old
image accessor. The flag is accepted for older programs too; unknown bits are
`E_ARGUMENT`. Existing public
structures, enums and ABI layouts are unchanged; the API feature version is
0.5, while the ABI/install line remains 0.5. The current bundled API is 0.6; product/source are 0.9.0.

`iprop_result_output_count` and `iprop_result_output_find` cover every declared
slot, including empty part lists and uncalled static instances. Query
`iprop_result_output_slot` for kind-neutral metadata, then
`iprop_result_output_part(result, slot, element, ...)` for each snapshot.
`element_index` is the append position, independent of `id` (generation-wide
creation ID) and `source_id` (region provenance, or -1 for rasters). Multiple
entries may share an ID but have different placement/appearance. Static slot
owners remain 0 for root and static part index + 1 for imported assets.

Each snapshot supplies a region/mask/layer source and absent/fill/texture/source
RGBA appearance. Default layer appearance uses its alpha once; fill and texture
multiply their alpha by the original shape coverage. Mask sources may refer
directly to RGBA alpha without converting to A8. A8 retains every soft byte;
RGBA retains all four channels, including RGB under zero alpha. Export without
appearance is valid and does not render. Active clips do not change snapshots.

For geometry point q, effective root pixels are
`linear_D4(q * source_scale) + translation`; divide by `target_scale` for root
asset units. D4 kind 0–7 is the existing mapping, with no additional rounding.
For rasters, apply `source_d4` about the resource's continuous **edge** extent
and add `source_origin` first. Then apply the snapshot affine transform.
Pixel sampling uses the corresponding extent-minus-one pixel-index rule.
Texture mapping remains in original source coordinates. Center/bounds already
describe effective root placement, including enclosing static calls.

Resource IDs are deterministic, result-local and start at 1. Zero means absent.
Enumeration visits global slots, append order, source then texture; pointer
identity deduplicates immutable stores, without sorting by addresses or merging
distinct equal buffers. `iprop_result_resource_get` returns geometry counts or
the exact borrowed raster bytes, dimensions, tight stride, format and scale.
Mappings belong to snapshots, so different views of one buffer share an ID.
Queries use bounded scratch and allocate no heap storage.

For a region, query its `outer_ring` and each hole with
`iprop_result_resource_region_hole`; ring queries preserve `parent_ring` and
`is_hole`, including nested topology. `resource_ring_copy_vertices` returns
the ordered original vertices, without repeating the first vertex. Geometry
is neither flattened nor rasterized to answer a query. `resource_copy_pixels`
copies the exact full A8/RGBA buffer. Capacities for both copies are **bytes**,
and the required size comes from the resource/ring descriptor.

The result owns all sources, immutable appearance/views and borrowed data.
Program destruction, parameter destruction and another generation leave them
valid. Borrowed names/bytes expire at `iprop_result_free`. Copied descriptor
scalars, host vertex arrays and host pixel buffers remain valid afterward;
their resource IDs/pointers do not extend the original result's lifetime.

New sized descriptors follow the existing four-byte minimum, flags-zero,
complete-field and preserved-padding/tail policy. Fixed leaf records (points,
RGBA) are each one complete field; UTF-8 pointer/count are independently bounded.
Invalid flags, IDs, slot/element/region/ring/hole indices, null outputs, short
capacities and overlapping source copies return `E_ARGUMENT` before a payload
write. Outputs and trailing host capacity remain unchanged on failure.

In an explicitly requested typed result, frozen packed-float `result_output_get` and scalar accessors return
`IPROP_EX_E_UNSUPPORTED` / `IPROP_REASON_UNSUPPORTED_FEATURE` for part slots,
including empty ones. They still work for point/rect/circle slots in a mixed
typed result. Legacy load/generate return `IPROP_E_FORMAT` with an explicit
unsupported message for any program declaring a part output, including only
uncalled/empty slots. `iprop_result_check_legacy` provides the checked error for
the void legacy image accessor: call it before `iprop_result_image`; an
incompatible checked result leaves the void accessor's entire output untouched.
The sized `iprop_result_query_image` works for all successful typed results.
The legacy CLI JSON result similarly rejects a new part declaration before
generation/image output. The explicit `ipgen --omit-part-outputs` preview option
executes the whole generation and emits the PNG/ordinary outputs with
`part_outputs_omitted: true`; it does not serialize part resources. There is no new generated-result serialization.

The normative program/ownership/quota rules are in the
[part export contract](../spec/part-exports-v0.9.md). All seven maintained bindings expose typed results; an older binding must
propagate unsupported errors. See [execution controls](execution-limits.md).
