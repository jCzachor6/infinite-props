# The model runtime (spec models v1.2). Shared by the
# native product and the independently configured WASM build of iprop3d.
set(IPROP_MODEL_SOURCES
    ops.cpp
    geometry.cpp
    blocks.cpp
    loader.cpp
    validation.cpp
    machine.cpp)
list(TRANSFORM IPROP_MODEL_SOURCES
    PREPEND "${CMAKE_CURRENT_LIST_DIR}/../model/")
