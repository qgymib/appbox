cmake_minimum_required(VERSION 3.15)
project(AppBoxTracer)

###############################################################################
# Tracer
#
# Console application (no WIN32 keyword) which runs a program below cdb.exe and
# reports the file, registry and network functions of ntdll, kernel32 and
# kernelbase which the program and its child processes actually used.
###############################################################################
add_executable(${PROJECT_NAME}
    "ArmPlan.cpp"
    "CdbLocator.cpp"
    "CdbOutputParser.cpp"
    "CdbSession.cpp"
    "Console.cpp"
    "Options.cpp"
    "PeImage.cpp"
    "ScopePatterns.cpp"
    "TargetProgram.cpp"
    "TraceReport.cpp"
    "TracedModules.cpp"
    "main.cpp"
)
appbox_setup_target_wall(${PROJECT_NAME})

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(${PROJECT_NAME}
    PRIVATE
        cxx_std_17
)

###############################################################################
# Dependency
###############################################################################
if (NOT TARGET AppBoxCommon)
    add_subdirectory(
        ${CMAKE_CURRENT_SOURCE_DIR}/../common
        ${CMAKE_BINARY_DIR}/common
    )
endif ()
target_link_libraries(${PROJECT_NAME} PRIVATE AppBoxCommon)

find_package(CLI11 REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE CLI11::CLI11)

###############################################################################
# Install products
###############################################################################
install(TARGETS ${PROJECT_NAME}
    DESTINATION bin
)
