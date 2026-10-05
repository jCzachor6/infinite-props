cmake_minimum_required(VERSION 3.24)
project(iprop_legacy_v02 LANGUAGES C)

# Standalone deliberately: this client must never include the current header
# or link a static copy of the current runtime. Preserve the built executable
# and run it again with a replacement shared library for ABI acceptance.
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(IPROP_LEGACY_LIBRARY "" CACHE FILEPATH "Released shared ipropc library")
if(NOT EXISTS "${IPROP_LEGACY_LIBRARY}")
    message(FATAL_ERROR "Set IPROP_LEGACY_LIBRARY to a shared ipropc library")
endif()
add_executable(iprop_legacy_v02 client.c)
target_include_directories(iprop_legacy_v02 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(iprop_legacy_v02 PRIVATE "${IPROP_LEGACY_LIBRARY}")
if(MSVC)
    target_compile_options(iprop_legacy_v02 PRIVATE /W4 /WX)
else()
    target_compile_options(iprop_legacy_v02 PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
