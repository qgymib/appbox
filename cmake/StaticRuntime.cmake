###############################################################################
# Form of the runtime library
#
# Every product of this repository links the runtime library of the compiler
# statically (the /MT option of MSVC, /MTd in a debug build), so no Visual C++
# Redistributable has to be installed on the machine which runs them: the
# packer, the launcher and the two sandbox injection modules an archive carries
# are self contained, and the machine of a sandboxed application never needs the
# runtime the tool was built with.
#
# The variable below initializes the MSVC_RUNTIME_LIBRARY property of every
# target which is created after this file was included, the targets of the
# dependencies included, so it has to be set before the first target is created.
# Policy CMP0091 has to be NEW when the first language is enabled for the
# property to have an effect at all, which the cmake_minimum_required() of both
# projects below sets it to. The debug runtime is a static one as well, so a
# debug build needs no redistributable either.
#
# The file is included by the top level project and by the sandbox project,
# which is configured on its own below the build tree of the packer: the
# variable is not inherited across the two configurations, and both have to
# name the same runtime library, because the injection modules are linked into
# the products of the top level project as resources.
###############################################################################
include_guard(GLOBAL)

if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif ()
