###############################################################################
# Support functions
###############################################################################
function(setup_target_wall name)
    # Only enable W4 in 64-bit platform, see
    # https://github.com/fmtlib/fmt/issues/4607
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        if (CMAKE_C_COMPILER_ID STREQUAL "MSVC")
            target_compile_options(${name} PRIVATE /W4 /WX /utf-8)
        else ()
            target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)
        endif ()
    endif ()
endfunction()
