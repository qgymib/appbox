
if(TARGET Detours)
    set(Detours_FOUND TRUE)
    return()
endif()

set(DETOURS_ROOT ${CMAKE_SOURCE_DIR}/third_party/Detours)

add_library(Detours STATIC
    "${DETOURS_ROOT}/src/creatwth.cpp"
    "${DETOURS_ROOT}/src/detours.cpp"
    "${DETOURS_ROOT}/src/disasm.cpp"
    "${DETOURS_ROOT}/src/disolarm.cpp"
    "${DETOURS_ROOT}/src/disolarm64.cpp"
    "${DETOURS_ROOT}/src/disolia64.cpp"
    "${DETOURS_ROOT}/src/disolx64.cpp"
    "${DETOURS_ROOT}/src/disolx86.cpp"
    "${DETOURS_ROOT}/src/image.cpp"
    "${DETOURS_ROOT}/src/modules.cpp"
)
target_include_directories(Detours
    PUBLIC
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${DETOURS_ROOT}/src>
)

set(Detours_FOUND TRUE)
