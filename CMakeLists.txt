cmake_minimum_required(VERSION 3.15)
project(appbox)

include(ExternalProject)

ExternalProject_Add(appbox32
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/appbox
    CMAKE_ARGS
        -DAPPBOX_DLL_SUFFIX:STRING=32
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_BINARY_DIR}/appbox
        -A Win32
    BUILD_ALWAYS TRUE
)

ExternalProject_Add(appbox64
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/appbox
    CMAKE_ARGS
        -DAPPBOX_DLL_SUFFIX:STRING=64
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_BINARY_DIR}/appbox
        -A x64
    BUILD_ALWAYS TRUE
)

ExternalProject_Add(loader32
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/loader
    CMAKE_ARGS
		-DAPPBOX_EXE_SUFFIX:STRING=32
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_BINARY_DIR}/appbox
        -A Win32
    BUILD_ALWAYS TRUE
)
