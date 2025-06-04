cmake_minimum_required(VERSION 3.15)
project(honeypot)

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
# Setup library
###############################################################################

add_executable(${PROJECT_NAME}
    "honeypot/__init__.cpp"
    "honeypot/CreateProcessA.cpp"
    "main.cpp"
)
target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)
appbox_setup_target_wall(${PROJECT_NAME})
target_link_libraries(${PROJECT_NAME} PRIVATE spdlog::spdlog Shlwapi.dll)

install(TARGETS ${PROJECT_NAME})
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/honeypot.pack.json
    TYPE DATA)
