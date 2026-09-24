# GenerateAboutInfo.cmake
#
# Build time generator of the header which carries the information shown by the
# About dialog of the packer.
#
# The script is executed by the `appbox_build_info` custom target of the top
# level CMakeLists.txt on every build (`cmake -P`), so the values it records
# describe the build which is running right now: the build date is the current
# local time and the git revision is the commit which is being built. The
# application itself never reads a version file, calls git or looks at the file
# system at run time; every value is compiled into the binary.
#
# The version of every third-party library is parsed from the sources of the
# submodule below third_party, so a bumped submodule is picked up automatically
# and a missing version file fails the build instead of producing an empty
# version.
#
# Required input variables:
#   APPBOX_SOURCE_DIR     Root of the source tree; used to locate git and the
#                         version files of the third-party libraries.
#   APPBOX_OUTPUT_FILE    Header file to write.
#   APPBOX_VERSION        Version of the CMake project (appbox_VERSION).
#   APPBOX_GIT_EXECUTABLE Path of git, empty when git is not available; the git
#                         fields degrade to `unknown` in that case.
#
# Usage:
#   cmake -DAPPBOX_SOURCE_DIR=<> -DAPPBOX_OUTPUT_FILE=<> -DAPPBOX_VERSION=<>
#         -DAPPBOX_GIT_EXECUTABLE=<> -P GenerateAboutInfo.cmake

cmake_minimum_required(VERSION 3.15)

foreach(_required APPBOX_SOURCE_DIR APPBOX_OUTPUT_FILE APPBOX_VERSION)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR
                "Usage: cmake -DAPPBOX_SOURCE_DIR=<> -DAPPBOX_OUTPUT_FILE=<> "
                "-DAPPBOX_VERSION=<> [-DAPPBOX_GIT_EXECUTABLE=<>] -P GenerateAboutInfo.cmake")
    endif()
endforeach()

# Read the first capture group of a regular expression from the text of a file.
#
# Fails the build when the file is missing or does not carry the expected
# pattern: a library whose version cannot be read is a build error and not a
# silently empty entry of the About dialog.
function(appbox_read_version file regex out_var)
    if(NOT EXISTS "${file}")
        message(FATAL_ERROR "GenerateAboutInfo: version file not found: ${file}")
    endif()

    file(READ "${file}" _text)
    if(NOT _text MATCHES "${regex}")
        message(FATAL_ERROR "GenerateAboutInfo: cannot read a version from ${file}")
    endif()

    set(${out_var} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

set(_third_party "${APPBOX_SOURCE_DIR}/third_party")

# asio: ASIO_VERSION encodes major * 100000 + minor * 1000 + patch.
appbox_read_version("${_third_party}/asio/include/asio/version.hpp"
                    "#define +ASIO_VERSION +([0-9]+)" _asio_encoded)
math(EXPR _asio_major "${_asio_encoded} / 100000")
math(EXPR _asio_minor "(${_asio_encoded} / 100) % 1000")
math(EXPR _asio_patch "${_asio_encoded} % 100")
set(_asio_version "${_asio_major}.${_asio_minor}.${_asio_patch}")

# Detours: the version only appears in the header comment of detver.h.
appbox_read_version("${_third_party}/Detours/src/detver.h"
                    "Version +([0-9]+\\.[0-9]+\\.[0-9]+)" _detours_version)

# expected and libzip declare their version in the project() command.
appbox_read_version("${_third_party}/expected/CMakeLists.txt"
                    "project\\([^)]*VERSION +([0-9]+\\.[0-9]+\\.[0-9]+)" _expected_version)
appbox_read_version("${_third_party}/libzip/CMakeLists.txt"
                    "project\\([^)]*VERSION +([0-9]+\\.[0-9]+\\.[0-9]+)" _libzip_version)

# nlohmann_json and spdlog declare one macro per version component.
appbox_read_version("${_third_party}/nlohmann_json/include/nlohmann/detail/abi_macros.hpp"
                    "#define +NLOHMANN_JSON_VERSION_MAJOR +([0-9]+)" _nlohmann_major)
appbox_read_version("${_third_party}/nlohmann_json/include/nlohmann/detail/abi_macros.hpp"
                    "#define +NLOHMANN_JSON_VERSION_MINOR +([0-9]+)" _nlohmann_minor)
appbox_read_version("${_third_party}/nlohmann_json/include/nlohmann/detail/abi_macros.hpp"
                    "#define +NLOHMANN_JSON_VERSION_PATCH +([0-9]+)" _nlohmann_patch)
set(_nlohmann_version "${_nlohmann_major}.${_nlohmann_minor}.${_nlohmann_patch}")

appbox_read_version("${_third_party}/spdlog/include/spdlog/version.h"
                    "#define +SPDLOG_VER_MAJOR +([0-9]+)" _spdlog_major)
appbox_read_version("${_third_party}/spdlog/include/spdlog/version.h"
                    "#define +SPDLOG_VER_MINOR +([0-9]+)" _spdlog_minor)
appbox_read_version("${_third_party}/spdlog/include/spdlog/version.h"
                    "#define +SPDLOG_VER_PATCH +([0-9]+)" _spdlog_patch)
set(_spdlog_version "${_spdlog_major}.${_spdlog_minor}.${_spdlog_patch}")

# wxWidgets: the file is documented as parseable by automatic tools.
appbox_read_version("${_third_party}/wxWidgets/include/wx/version.h"
                    "#define +wxMAJOR_VERSION +([0-9]+)" _wxwidgets_major)
appbox_read_version("${_third_party}/wxWidgets/include/wx/version.h"
                    "#define +wxMINOR_VERSION +([0-9]+)" _wxwidgets_minor)
appbox_read_version("${_third_party}/wxWidgets/include/wx/version.h"
                    "#define +wxRELEASE_NUMBER +([0-9]+)" _wxwidgets_release)
set(_wxwidgets_version "${_wxwidgets_major}.${_wxwidgets_minor}.${_wxwidgets_release}")

# zlib: the version is a string macro.
appbox_read_version("${_third_party}/zlib/zlib.h"
                    "#define +ZLIB_VERSION +\"([0-9]+\\.[0-9]+\\.[0-9]+)\"" _zlib_version)

# The libraries are listed in the order the About dialog shows them, which is
# alphabetical ignoring the case of the name.
set(_dependencies
    "asio=${_asio_version}"
    "Detours=${_detours_version}"
    "expected=${_expected_version}"
    "libzip=${_libzip_version}"
    "nlohmann_json=${_nlohmann_version}"
    "spdlog=${_spdlog_version}"
    "wxWidgets=${_wxwidgets_version}"
    "zlib=${_zlib_version}"
)

# Build date: the local time of this build.
string(TIMESTAMP _build_date "%Y-%m-%d %H:%M:%S")

# git: the revision which is being built. A missing git or a source tree which
# is not a repository only degrades the values, it does not fail the build.
set(_git_revision "unknown")
set(_git_branch "unknown")
set(_git_dirty "false")

if(APPBOX_GIT_EXECUTABLE)
    execute_process(
        COMMAND "${APPBOX_GIT_EXECUTABLE}" rev-parse --short HEAD
        WORKING_DIRECTORY "${APPBOX_SOURCE_DIR}"
        OUTPUT_VARIABLE _git_revision
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _git_result
    )

    if(_git_result EQUAL 0)
        execute_process(
            COMMAND "${APPBOX_GIT_EXECUTABLE}" rev-parse --abbrev-ref HEAD
            WORKING_DIRECTORY "${APPBOX_SOURCE_DIR}"
            OUTPUT_VARIABLE _git_branch
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE _git_branch_result
        )

        if(NOT _git_branch_result EQUAL 0)
            set(_git_branch "unknown")
        endif()

        # Untracked files are ignored: they are not part of the binary.
        execute_process(
            COMMAND "${APPBOX_GIT_EXECUTABLE}" status --porcelain --untracked-files=no
            WORKING_DIRECTORY "${APPBOX_SOURCE_DIR}"
            OUTPUT_VARIABLE _git_status
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE _git_status_result
        )

        if(_git_status_result EQUAL 0 AND NOT _git_status STREQUAL "")
            set(_git_dirty "true")
        endif()
    else()
        set(_git_revision "unknown")
    endif()
endif()

# Render the dependency entries of the generated array.
set(_dependency_entries "")
foreach(_dependency IN LISTS _dependencies)
    string(REPLACE "=" ";" _parts "${_dependency}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _version)
    string(APPEND _dependency_entries "    {\"${_name}\", \"${_version}\"},\n")
endforeach()

set(_content "// Build information shown by the About dialog.\n")
string(APPEND _content "//\n")
string(APPEND _content "// Generated by cmake/GenerateAboutInfo.cmake at build time. Do not edit:\n")
string(APPEND _content "// the file is not part of the source tree and is overwritten on every build.\n")
string(APPEND _content "#ifndef APPBOX_GENERATED_ABOUT_INFO_HPP\n")
string(APPEND _content "#define APPBOX_GENERATED_ABOUT_INFO_HPP\n")
string(APPEND _content "\n")
string(APPEND _content "namespace appbox::generated\n")
string(APPEND _content "{\n")
string(APPEND _content "\n")
string(APPEND _content "/** Version of the CMake project. */\n")
string(APPEND _content "inline constexpr const char* kApplicationVersion = \"${APPBOX_VERSION}\";\n")
string(APPEND _content "\n")
string(APPEND _content "/** Local time of the build, formatted as YYYY-MM-DD HH:MM:SS. */\n")
string(APPEND _content "inline constexpr const char* kBuildDate = \"${_build_date}\";\n")
string(APPEND _content "\n")
string(APPEND _content "/** Abbreviated revision the binary was built from, \"unknown\" without git. */\n")
string(APPEND _content "inline constexpr const char* kGitRevision = \"${_git_revision}\";\n")
string(APPEND _content "\n")
string(APPEND _content "/** Branch of the built revision, \"unknown\" without git. */\n")
string(APPEND _content "inline constexpr const char* kGitBranch = \"${_git_branch}\";\n")
string(APPEND _content "\n")
string(APPEND _content "/** Whether the working tree held uncommitted changes while building. */\n")
string(APPEND _content "inline constexpr bool kGitDirty = ${_git_dirty};\n")
string(APPEND _content "\n")
string(APPEND _content "/** One third-party library the binary was linked against. */\n")
string(APPEND _content "struct GeneratedDependency\n")
string(APPEND _content "{\n")
string(APPEND _content "    /** Name of the library. */\n")
string(APPEND _content "    const char* name;\n")
string(APPEND _content "\n")
string(APPEND _content "    /** Version of the library. */\n")
string(APPEND _content "    const char* version;\n")
string(APPEND _content "};\n")
string(APPEND _content "\n")
string(APPEND _content "/** Third-party libraries of the binary, ordered by name. */\n")
string(APPEND _content "inline constexpr GeneratedDependency kDependencies[] = {\n")
string(APPEND _content "${_dependency_entries}")
string(APPEND _content "};\n")
string(APPEND _content "\n")
string(APPEND _content "} // namespace appbox::generated\n")
string(APPEND _content "\n")
string(APPEND _content "#endif // APPBOX_GENERATED_ABOUT_INFO_HPP\n")

# The header is only touched when its content changes, so a build which happens
# to produce the same information does not recompile the translation unit which
# includes it.
set(_unchanged FALSE)
if(EXISTS "${APPBOX_OUTPUT_FILE}")
    file(READ "${APPBOX_OUTPUT_FILE}" _existing)
    if(_existing STREQUAL _content)
        set(_unchanged TRUE)
    endif()
endif()

if(NOT _unchanged)
    get_filename_component(_output_dir "${APPBOX_OUTPUT_FILE}" DIRECTORY)
    file(MAKE_DIRECTORY "${_output_dir}")
    file(WRITE "${APPBOX_OUTPUT_FILE}" "${_content}")
endif()

set(_git_description "${_git_revision} (${_git_branch}")
if(_git_dirty)
    string(APPEND _git_description ", modified")
endif()
string(APPEND _git_description ")")

message(STATUS "GenerateAboutInfo: ${APPBOX_VERSION}, built ${_build_date}, "
               "git ${_git_description} -> ${APPBOX_OUTPUT_FILE}")
