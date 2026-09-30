# Strict compiler warnings shared by every target.
# The project compiles with warnings-as-errors everywhere: determinism work
# must not hide behind sloppy code.
function(iprop_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive- /fp:strict)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
        # spec §13.2: implementations MUST NOT fuse multiply-add. Disable
        # contraction so the VM's f32 results are bit-reproducible.
        target_compile_options(${target} PRIVATE -ffp-contract=off)
    endif()
endfunction()
