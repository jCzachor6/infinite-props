# One implementation list for native libraries and the public WASM binding.
set(IPROP_CAPI_IMPLEMENTATION_SOURCES)
foreach(source iprop.cpp operations.cpp library.cpp descriptors.cpp reflection.cpp
               results.cpp parameters.cpp parameter_setters.cpp outputs.cpp part_results.cpp execution_limits.cpp)
    list(APPEND IPROP_CAPI_IMPLEMENTATION_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../backends/capi/${source}")
endforeach()
