cmake_minimum_required(VERSION 3.15)
project(loader)

###############################################################################
# Support functions
###############################################################################
function(appbox_setup_target_wall name)
    if (CMAKE_C_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(${name} PRIVATE /W4 /WX)
    else ()
        target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)
    endif ()
endfunction()

###############################################################################
# Setup executable
###############################################################################

set(loader_sources
    "main.cpp"
    "resource.rc"
)

add_executable(${PROJECT_NAME} WIN32 ${loader_sources})
appbox_setup_target_wall(${PROJECT_NAME})

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../utils
)

###############################################################################
# Dependency
###############################################################################

set(HAVE_DETOURS ON)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party third_party)
target_link_libraries(${PROJECT_NAME}
    PRIVATE
        Detours
)

option(JSON_SEARCH_PATH "Search path for nlohmann_json" OFF)
if (JSON_SEARCH_PATH)
    find_package(nlohmann_json REQUIRED PATHS ${JSON_SEARCH_PATH} NO_DEFAULT_PATH)
else ()
    find_package(nlohmann_json REQUIRED)
endif ()
target_link_libraries(${PROJECT_NAME} PRIVATE nlohmann_json::nlohmann_json)

option(WXWIDGETS_SEARCH_PATH "Search path for wxWidgets" OFF)
if (WXWIDGETS_SEARCH_PATH)
    find_package(wxWidgets REQUIRED COMPONENTS core base PATHS ${WXWIDGETS_SEARCH_PATH} NO_DEFAULT_PATH)
else()
    find_package(wxWidgets REQUIRED COMPONENTS core base)
endif()
target_link_libraries(${PROJECT_NAME} PRIVATE wx::core wx::base)

option(ZLIB_SEARCH_PATH "Search path for zlib" OFF)
if (ZLIB_SEARCH_PATH)
    find_package(zlib REQUIRED PATHS ${ZLIB_SEARCH_PATH} NO_DEFAULT_PATH)
else()
    find_package(zlib REQUIRED)
endif()
target_link_libraries(${PROJECT_NAME} PRIVATE ZLIB::ZLIBSTATIC)

###############################################################################
# Install target
###############################################################################

install(TARGETS ${PROJECT_NAME})
