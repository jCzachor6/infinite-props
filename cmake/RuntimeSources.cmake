# Shared by the native product and the independently configured WASM build.
set(IPROP_RUNTIME_SOURCES
    version.cpp
    opcode_name.cpp
    opcode_spec.cpp
    debug.cpp
    format.cpp
    compression.cpp
    consts.cpp
    meta.cpp
    code.cpp
    validator.cpp
    vm.cpp
    stack.cpp
    control.cpp
    arithmetic.cpp
    vector_ops.cpp
    sdf_ops.cpp
    color_ops.cpp
    curves_ops.cpp
    math/scalar.cpp
    math/vector.cpp
    math/sdf.cpp
    math/bezier.cpp
    math/curves.cpp
    math/color.cpp
    seeded.cpp
    canvas.cpp
    outputs.cpp
    parts.cpp
    part_render_plan.cpp
    utf8.cpp)
list(TRANSFORM IPROP_RUNTIME_SOURCES
    PREPEND "${CMAKE_CURRENT_LIST_DIR}/../runtime/")
