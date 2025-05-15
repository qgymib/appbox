
# microsoft/Detours
option(HAVE_DETOURS "Enable microsoft/Detours" OFF)
if (HAVE_DETOURS)
    set(DETOURS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/Detours)
    add_library(Detours STATIC
        ${DETOURS_ROOT}/src/creatwth.cpp
        ${DETOURS_ROOT}/src/detours.cpp
        ${DETOURS_ROOT}/src/disasm.cpp
        ${DETOURS_ROOT}/src/disolarm.cpp
        ${DETOURS_ROOT}/src/disolarm64.cpp
        ${DETOURS_ROOT}/src/disolia64.cpp
        ${DETOURS_ROOT}/src/disolx64.cpp
        ${DETOURS_ROOT}/src/disolx86.cpp
        ${DETOURS_ROOT}/src/image.cpp
        ${DETOURS_ROOT}/src/modules.cpp
    )
    target_include_directories(Detours
        PUBLIC
            $<INSTALL_INTERFACE:include>
            $<BUILD_INTERFACE:${DETOURS_ROOT}/src>
    )
endif()

# nlohmann/json
option(HAVE_JSON "Enable nlohmann/json" OFF)
if (HAVE_JSON)
    add_subdirectory(json)
endif()

# wxWidgets
option(HAVE_WXWIDGETS "Enable wxWidgets" OFF)
if (HAVE_WXWIDGETS)
    set(wxBUILD_SHARED OFF)
	set(wxBUILD_TESTS OFF)
	set(wxBUILD_SAMPLES OFF)
	set(wxBUILD_PRECOMP OFF)
	set(wxBUILD_INSTALL OFF)
    add_subdirectory(wxWidgets)
endif()
