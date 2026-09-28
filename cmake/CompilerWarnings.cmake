# Strict compiler warnings shared by every target.
# The project compiles with warnings-as-errors everywhere: determinism work
# must not hide behind sloppy code.
function(iprop_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()
