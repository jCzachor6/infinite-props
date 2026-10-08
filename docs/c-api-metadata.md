# Emission and physical container metadata

`IPROP_CAP_METADATA_EXTENDED` advertises eight fields appended after `fuel` in
the existing `iprop_program_info_ex`. The existing `iprop_program_query` and
`iprop_program_info_ex_init` handle them; no second metadata descriptor or new
query function is introduced. Frozen `iprop_meta` is unchanged.

| Fields | Meaning |
| --- | --- |
| `has_emission` | Fixed-width presence, exactly zero or one |
| `emission_color` | Exact stored RGBA8 channels |
| `emission_offset_x`, `emission_offset_y`, `emission_radius` | Exact stored binary32 bits |
| `format_major`, `format_minor` | Version of the actual loaded physical container |
| `container_flags` | Flags of that physical container, including debug/compression |

When `has_emission` is zero all inactive emission fields are zero, including
RGBA alpha and positive-zero float representations. When it is one, the
query performs no arithmetic: negative zero, subnormals and stored NaN payloads
are preserved. These fields report executable metadata; no source-only ranges
or signed type aliases are inferred.

Physical format fields come from `LoadedFile.containerHeader`, which is retained
before expanding a compressed container. Consequently a compressed debug
container reports flags 3 even when the expanded execution layout has the
compression bit cleared. Flags zero, debug flag 1 and compression flag 2
retain their binary meaning. These are output metadata, not query options.
Program format version is distinct from the library query's accepted format
range and API/product version. An accepted format 0.1 file reports minor 1;
a 0.2 file reports minor 2.

Every original field through `fuel` keeps its offset, and the extension begins
after the previous descriptor size. A client compiled with the older header
and allocating exactly its former `sizeof` receives only that bounded prefix.
New clients negotiate the capability before interpreting appended fields.
The initializer clears only complete known fields; query preserves size,
padding, partial fields and unknown tail bytes. Only `struct_size` is an input,
with the existing four-byte minimum. Color is one frozen four-byte leaf record
and is written only when all channels fit.

Queries use the same [checked error and alias contract](c-api-checked.md),
including optional bounded errors, success reset and complete main-output
preservation on failure. Error descriptor/message output must not overlap
any declared query output capacity, including the appended fields. Metadata
querying and reporting allocate no new memory and execute no VM instructions.
Both legacy-loaded and checked-loaded handles support these fields independently
of caller load-buffer lifetime, debug presence or compression.
