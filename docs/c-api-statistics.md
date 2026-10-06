# Completed result statistics

`IPROP_CAP_RESULT_STATISTICS` advertises `iprop_result_stats_ex_init` and
`iprop_result_query_stats(result, &stats, error)`. They use API 0.3's sized
descriptor and checked error rules. Legacy generation creates results with
these statistics too; checked generation is introduced separately.

| Output | Meaning |
| --- | --- |
| `fuel_budget` | Existing VM budget at execution |
| `fuel_remaining` | Existing VM fuel counter after successful execution |
| `fuel_used` | `fuel_budget - fuel_remaining` |
| `image_byte_count` | Bytes of the result's RGBA8 pixel storage |
| `output_element_count` | Sum of generated values across all root/part output slots |

Fuel is the VM's established accounting unit. It is neither elapsed time nor
an instruction count; different operations have different costs and an empty
draw can consume zero fuel. Rendering tools measure wall-clock time separately.
The query neither executes instructions nor changes RNG or fuel. It allocates
no memory. Element counting scans the completed C slot table.

Unset singulars and empty lists contribute zero elements, assigned singulars
contribute one, and lists contribute their full count. Point/rectangle/circle
components are not separate elements. Declared slot count is independently
available in the legacy image view. Pixel bytes exclude names, packed output
values, table storage and metadata; this value is not total or peak memory.
Requested size overrides are reflected in the actual pixel buffer size.

Only `struct_size` and `flags` are inputs. At least four bytes are required;
an absent flags field means zero and any complete nonzero flags field is an
argument error. Initialization clears complete known fields. Query writes
complete output scalars only, preserving size/flags, padding, partial fields
and unknown tail. Null required result/output, malformed error storage and
unsupported flags leave the statistics output unchanged. Optional bounded
errors, alias rejection and success clearing follow the
[checked operation contract](c-api-checked.md#sized-query-outputs-and-error-storage).

Statistics are copied values, without borrowed pointers. A result owns its
fuel counters independently of the loaded program. Freeing the program,
generating another result, freeing that result or a failed later generation
does not invalidate an earlier result or change its statistics. Copied
statistics remain valid after freeing their result. A failed generation
produces no partial statistics; querying an earlier successful result still
reports that earlier execution.

The C99 shared/static tests exercise every descriptor capacity, unknown flags,
small/absent/malformed error buffers, overlapping outputs and independent
result lifetime. The C++ oracle compares 40 real C generations with independent
VM fuel counters and typed output vectors at four seeds and two target sizes,
covering empty output tables, singulars, lists and composed parts. Its
allocation-failure guard proves the query and its argument-reporting paths
perform no C++ allocation.
