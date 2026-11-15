# Strict compiler warnings shared by every target.
# The project compiles with warnings-as-errors everywhere: determinism work
# must not hide behind sloppy code.
function(iprop_warnings target)
    if(MSVC)
        # Unicode literals and diagnostics must not depend on the host code page.
        target_compile_options(${target} PRIVATE /W4 /WX /permissive- /fp:strict /utf-8)
        # Use the standard C file APIs on every platform. MSVC otherwise
        # diagnoses fopen/strncpy as deprecated extensions under /WX.
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
        # GCC 15 (ubuntu-26.04, the ubuntu-latest image since November 2026)
        # reports two diagnostics on code that GCC 13 and Clang accept:
        # -Wmaybe-uninitialized inside std::variant copies, such as copying a
        # compilation plan, and -Wmismatched-new-delete where a test or a
        # benchmark replaces the global operator new/delete with malloc/free
        # to count or fail allocations. Both stay visible warnings there,
        # without failing the build; AddressSanitizer still checks real
        # allocation mismatches at run time.
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 15)
            target_compile_options(${target} PRIVATE -Wno-error=maybe-uninitialized
                $<$<COMPILE_LANGUAGE:CXX>:-Wno-error=mismatched-new-delete>)
        endif()
        # spec §13.2: implementations MUST NOT fuse multiply-add. Disable
        # contraction so the VM's f32 results are bit-reproducible.
        target_compile_options(${target} PRIVATE -ffp-contract=off)
    endif()
endfunction()
