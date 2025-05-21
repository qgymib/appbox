cmake_minimum_required(VERSION 3.15)
project(appbox)

include(ExternalProject)

ExternalProject_Add(json32
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/json
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_BINARY_DIR}/install/Win32
        -DJSON_BuildTests=OFF
        -A Win32
)

ExternalProject_Add(spdlog32
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/spdlog
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_BINARY_DIR}/install/Win32
        -DSPDLOG_BUILD_EXAMPLE=OFF
        -DSPDLOG_WCHAR_SUPPORT=ON
        -DSPDLOG_WCHAR_FILENAMES=ON
        -DSPDLOG_WCHAR_CONSOLE=ON
        -A Win32
)

ExternalProject_Add(wxWidgets32
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/wxWidgets
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_BINARY_DIR}/install/Win32
        -DwxBUILD_SHARED=OFF
        -DwxBUILD_TESTS=OFF
        -DwxBUILD_SAMPLES=OFF
        -DwxBUILD_DEMOS=OFF
        -DwxBUILD_PRECOMP=OFF
        -A Win32
)

ExternalProject_Add(zlib32
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_BINARY_DIR}/install/Win32
        -DZLIB_BUILD_TESTING=OFF
        -DZLIB_BUILD_SHARED=OFF
        -DZLIB_INSTALL_COMPAT_DLL=OFF
        -A Win32
)

ExternalProject_Add(sandbox32
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/sandbox
    CMAKE_ARGS
        -DAPPBOX_SANDBOX_SUFFIX:STRING=32
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_BINARY_DIR}/install/dist
        -A Win32
)

ExternalProject_Add(sandbox64
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/sandbox
    CMAKE_ARGS
        -DAPPBOX_SANDBOX_SUFFIX:STRING=64
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_BINARY_DIR}/install/dist
        -A x64
)

ExternalProject_Add(loader32
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/loader
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_BINARY_DIR}/install/dist
        -DJSON_SEARCH_PATH=${CMAKE_BINARY_DIR}/install/Win32
        -DSPDLOG_SEARCH_PATH=${CMAKE_BINARY_DIR}/install/Win32
        -DWXWIDGETS_SEARCH_PATH=${CMAKE_BINARY_DIR}/install/Win32
        -DZLIB_SEARCH_PATH=${CMAKE_BINARY_DIR}/install/Win32
        -A Win32
    DEPENDS
        json32
        spdlog32
        wxWidgets32
        zlib32
)

ExternalProject_Add(packer32
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/packer
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_BINARY_DIR}/install/dist
        -DJSON_SEARCH_PATH=${CMAKE_BINARY_DIR}/install/Win32
        -DSPDLOG_SEARCH_PATH=${CMAKE_BINARY_DIR}/install/Win32
        -DWXWIDGETS_SEARCH_PATH=${CMAKE_BINARY_DIR}/install/Win32
        -DZLIB_SEARCH_PATH=${CMAKE_BINARY_DIR}/install/Win32
        -A Win32
    DEPENDS
        json32
        spdlog32
        wxWidgets32
        zlib32
)
