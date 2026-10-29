###############################################################################
# Arguments of a foreign build
#
# Two directories of this build tree start a project of their own for a bit size
# which the build itself does not run in: launcher/ configures the two sandbox
# injection modules (sandbox_x86_build and sandbox_x64_build), because the
# launcher carries them as resources of its own executable, and test/ configures
# the two probe executables (probe_x86_build and probe_x64_build). The arguments
# they forward to those builds are the same three ones: the bit size, the build
# type and the toolchain file of the build which starts them.
#
# A subdirectory inherits the variables of its parent but not those of a
# sibling, so every directory which starts a foreign build includes this file
# itself and gets the arguments in its own directory scope. That is also why the
# file carries a directory scoped include guard and writes no cache variable:
# the values describe the configuration of the including directory and not a
# state of the build tree.
#
# The Visual Studio generator picks the bit size with the platform option of the
# configure step, while the other generators pick it with the toolchain file
# which CMAKE_TOOLCHAIN_FILE names. APPBOX_X86_TOOLCHAIN_FILE is the file the
# toolchain of a 64 bit build names for its 32 bit build and stays an input of
# the caller, see cmake/toolchain/mingw-w64-x86_64.cmake.
###############################################################################
include_guard(DIRECTORY)

if (MSVC)
    set(APPBOX_FOREIGN_X86_ARGS -A Win32)
    set(APPBOX_FOREIGN_X64_ARGS)
else ()
    set(APPBOX_FOREIGN_X86_ARGS)
    set(APPBOX_FOREIGN_X64_ARGS)
endif ()

if (CMAKE_BUILD_TYPE)
    set(APPBOX_FOREIGN_BUILD_TYPE -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE})
else ()
    set(APPBOX_FOREIGN_BUILD_TYPE)
endif ()

if (CMAKE_TOOLCHAIN_FILE)
    set(APPBOX_FOREIGN_X86_TOOLCHAIN_FILE -DCMAKE_TOOLCHAIN_FILE=${APPBOX_X86_TOOLCHAIN_FILE})
    set(APPBOX_FOREIGN_X64_TOOLCHAIN_FILE -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
else ()
    set(APPBOX_FOREIGN_X86_TOOLCHAIN_FILE)
    set(APPBOX_FOREIGN_X64_TOOLCHAIN_FILE)
endif ()
