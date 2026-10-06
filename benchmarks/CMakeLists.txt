add_executable(iprop_render_benchmark EXCLUDE_FROM_ALL render.cpp)
target_link_libraries(iprop_render_benchmark PRIVATE iprop::runtime)
iprop_warnings(iprop_render_benchmark)

# Link the static adapter so the executable's operator new replacement
# observes C++ allocations inside the C API and runtime on every platform.
add_executable(iprop_capi_benchmark EXCLUDE_FROM_ALL capi.cpp)
target_link_libraries(iprop_capi_benchmark PRIVATE iprop::capi_static)
iprop_warnings(iprop_capi_benchmark)
