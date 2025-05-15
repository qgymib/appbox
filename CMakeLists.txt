cmake_minimum_required(VERSION 3.15)
project(appbox)

include(ExternalProject)

ExternalProject_Add(appbox32
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/appbox
    CMAKE_ARGS
        -DAPPBOX_DLL_SUFFIX:STRING=32
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_BINARY_DIR}/install
        -A Win32
)

ExternalProject_Add(appbox64
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/appbox
    CMAKE_ARGS
        -DAPPBOX_DLL_SUFFIX:STRING=64
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_BINARY_DIR}/install
        -A x64
)

ExternalProject_Add(loader
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/loader
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_BINARY_DIR}/install
        -A Win32
)

ExternalProject_Add(packer
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/packer
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_BINARY_DIR}/install
        -A Win32
)

ExternalProject_Add(gui
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/gui
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_BINARY_DIR}/install
        -A Win32
)
