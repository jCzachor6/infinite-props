# Shared by the native product and the independently configured WASM build.
set(IPROP_RUNTIME_SOURCES
    version.cpp
    opcode_name.cpp
    debug.cpp
    format.cpp
    consts.cpp
    meta.cpp
    code.cpp
    validator.cpp
    vm.cpp
    stack.cpp
    control.cpp
    arithmetic.cpp
    seeded.cpp
    canvas.cpp
    outputs.cpp
    parts.cpp
    part_render_plan.cpp
    utf8.cpp)
list(TRANSFORM IPROP_RUNTIME_SOURCES
    PREPEND "${CMAKE_CURRENT_LIST_DIR}/../runtime/")
