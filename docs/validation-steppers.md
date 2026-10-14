# Stepper implementation and validation

Verified locally on macOS arm64 on 2026-10-14. `stepper<T>` supports i32,
f32, color and vec2, with immutable captured descriptions and required
`.pick(n)` counts in 0–256. Uniform steps and truncated distribution quantiles
are independent of RNG. Operand expressions retain their normal effects.

| Gate | Result |
| --- | --- |
| Native and WASM builds | Passed |
| Stepper C++ tests | 13 cases, 10,501 assertions |
| Full CTest regression | All 72 passed; SFML/Godot tests require system graphics/log access |
| Stepper native/WASM conformance | 144 bit-exact generations, 16 independent pins, 9 matching traps, 26 malformed/version rejections |
| Existing sampler regression | 1,455 exact generations, 16 matching traps, 45 malformed/version rejections |
| Other native/WASM suites | Math, vectors, colors, constants, lists, list methods, palettes, point distributions, density, surfaces, alpha composition, masks and resource exports passed |
| Export schema and CLI | Pinned JSON Schema validation: 144 mutations and 141 real CLI cases |
| Handbook and LLM reference | 82 global builtins and 141 executable examples passed |
| Editor | Formatting, comment preservation, required counts and stepper highlighting passed |
| Website | Clean handbook build, five documentation tests and release smoke passed; 42 native/WASM showcase images matched |
| Public C API | Existing ABI retained; C-host stepper fixture verifies rounding, vector endpoints, count behavior and unchanged RNG across three seeds |

The stepper suites cover all four types, counts 0/1/2/10/256, constants and
equal bounds, signed zeros, positive/negative halves, adjacent integers above
2^24, the full i32 range and extreme finite f32 endpoints. All five distribution
families cover closed/truncated support, zero weights, density gaps and missing
mass. Tests check eager capture/count order, helper and const transfers,
separate sampler/stepper pools, structural distinctness, count/type diagnostics,
uninitialized handles, exact kernel fuel, the shared 64 MiB limit and private
partial results on failure.

Compiler configurations include optimized/reference, debug/stripped and
plain/compressed files, plus formatted-source and IPA/IPDIS round trips.
The full native suite includes 676 C++ cases. Native/WASM comparisons use raw
f32 output encodings, pixels and caller stream state.

Reproduce the focused checks with:

```sh
cmake --build build --parallel 6
./build/tests/iprop_tests '[stepper]'
cmake --build build/wasm --parallel 4
node backends/html5/test/run-tests.js build/wasm build/tools
ctest --test-dir build --output-on-failure
```

Implementation: [runtime](../runtime/steppers.cpp),
[native tests](../tests/compiler/steppers_test.cpp),
[WASM conformance](../backends/html5/test/stepper-conformance.js),
[language reference](language/steppers.md), [format contract](../spec/iprop-v0.7.md).
