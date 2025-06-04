cmake_minimum_required(VERSION 3.15)
project(appbox)

include(ExternalProject)

option(APPBOX_INSTALL_PREFIX "" OFF)
if (APPBOX_INSTALL_PREFIX)
	set(CMAKE_INSTALL_PREFIX ${APPBOX_INSTALL_PREFIX})
else()
	set(CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR}/install/dist)
endif()

set(JSON_BuildTests OFF)
set(JSON_Install OFF)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/third_party/json)

set(SPDLOG_BUILD_EXAMPLE OFF)
set(SPDLOG_WCHAR_SUPPORT ON)
set(SPDLOG_WCHAR_FILENAMES ON)
set(SPDLOG_WCHAR_CONSOLE ON)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/third_party/spdlog)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(wxBUILD_SHARED OFF)
    set(wxBUILD_TESTS OFF)
    set(wxBUILD_SAMPLES OFF)
    set(wxBUILD_DEMOS OFF)
    set(wxBUILD_PRECOMP OFF)
    set(wxBUILD_INSTALL OFF)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/third_party/wxWidgets)

    set(ZLIB_BUILD_TESTING OFF)
    set(ZLIB_BUILD_SHARED OFF)
    set(ZLIB_INSTALL OFF)
    set(ZLIB_INSTALL_COMPAT_DLL OFF)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib)
endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/third_party/Detours)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_subdirectory(loader)
    add_subdirectory(packer)
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(APPBOX_SANDBOX_SUFFIX 64)
else ()
    set(APPBOX_SANDBOX_SUFFIX 32)
endif ()
add_subdirectory(sandbox)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    ExternalProject_Add(sandbox_build_32bit
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}
        CMAKE_ARGS
			-DAPPBOX_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
            -A Win32
    )
endif()

add_subdirectory(test)
