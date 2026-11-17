# The model runtime: the extended VM of .iprop3d files (spec models v1.2).
# A separate library on top of the 2D runtime, so a 2D host links none of it.
include(../cmake/ModelSources.cmake)
add_library(iprop_model ${IPROP_MODEL_SOURCES})

add_library(iprop::model ALIAS iprop_model)
set_target_properties(iprop_model PROPERTIES
    POSITION_INDEPENDENT_CODE ON EXPORT_NAME model)

# Public headers live in iprop/model/, beside the runtime's iprop/.
target_include_directories(iprop_model PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(iprop_model PUBLIC iprop::runtime)

iprop_warnings(iprop_model)
