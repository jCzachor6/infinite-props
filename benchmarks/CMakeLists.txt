add_executable(iprop_render_benchmark EXCLUDE_FROM_ALL render.cpp)
target_link_libraries(iprop_render_benchmark PRIVATE iprop::runtime)
iprop_warnings(iprop_render_benchmark)

# Link the static adapter so the executable's operator new replacement
# observes C++ allocations inside the C API and runtime on every platform.
add_executable(iprop_capi_benchmark EXCLUDE_FROM_ALL capi.cpp)
target_link_libraries(iprop_capi_benchmark PRIVATE iprop::capi_static)
iprop_warnings(iprop_capi_benchmark)

add_executable(iprop_operations_benchmark EXCLUDE_FROM_ALL operations.cpp)
target_link_libraries(iprop_operations_benchmark PRIVATE iprop::runtime)
iprop_warnings(iprop_operations_benchmark)

add_executable(iprop_execution_benchmark EXCLUDE_FROM_ALL execution-limits.cpp)
target_link_libraries(iprop_execution_benchmark PRIVATE iprop::capi_static)
iprop_warnings(iprop_execution_benchmark)

# The normal map tariff against blur and the other raster kernels. It compiles
# its own sources, so it needs the compiler and is absent from runtime-only builds.
if(TARGET iprop_compiler)
    add_executable(iprop_normal_map_tariff EXCLUDE_FROM_ALL normal-map-tariff.cpp)
    target_link_libraries(iprop_normal_map_tariff PRIVATE iprop_compiler)
    iprop_warnings(iprop_normal_map_tariff)
endif()
