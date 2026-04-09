###############################################################################
# Export:
# - Detours
###############################################################################

if(TARGET Detours)
    set(Detours_FOUND TRUE)
    return()
endif()

set(_DETOURS_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../third_party/Detours")
get_filename_component(_DETOURS_SOURCE_DIR "${_DETOURS_SOURCE_DIR}" ABSOLUTE)

add_library(Detours STATIC
    "${_DETOURS_SOURCE_DIR}/src/creatwth.cpp"
    "${_DETOURS_SOURCE_DIR}/src/detours.cpp"
    "${_DETOURS_SOURCE_DIR}/src/disasm.cpp"
    "${_DETOURS_SOURCE_DIR}/src/disolarm.cpp"
    "${_DETOURS_SOURCE_DIR}/src/disolarm64.cpp"
    "${_DETOURS_SOURCE_DIR}/src/disolia64.cpp"
    "${_DETOURS_SOURCE_DIR}/src/disolx64.cpp"
    "${_DETOURS_SOURCE_DIR}/src/disolx86.cpp"
    "${_DETOURS_SOURCE_DIR}/src/image.cpp"
    "${_DETOURS_SOURCE_DIR}/src/modules.cpp"
)
target_include_directories(Detours
    PUBLIC
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${_DETOURS_SOURCE_DIR}/src>
)

set(Detours_FOUND TRUE)
