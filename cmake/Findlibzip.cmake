###############################################################################
# Export:
# - libzip::zip
###############################################################################

# Prevent duplicate entries
if(TARGET libzip::zip)
    set(libzip_FOUND TRUE)
    return()
endif()

# Calculate the path to the third-party source code (relative to the directory containing this file)
set(_libzip_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../third_party/libzip")
get_filename_component(_libzip_SOURCE_DIR "${_libzip_SOURCE_DIR}" ABSOLUTE)

# libzip declares cmake_minimum_required(VERSION 3.10), so policies
# introduced later (CMP0077, "option() honors normal variables") behave as
# OLD inside the subdirectory: option() prefers stale cache entries from a
# previous configure over the normal variables set by this script. Push the
# options through the cache with FORCE instead, so a reconfigured build tree
# keeps the minimal static libzip configuration.
#
# BUILD_SHARED_LIBS is pinned for the same reason: libzip declares it with a
# default of ON, and the value has to stay OFF because the variable is global
# and every later third-party project (spdlog, googletest, libwebp) picks its
# library type from it.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "libzip: static library" FORCE)
foreach(_libzip_option
    ENABLE_COMMONCRYPTO
    ENABLE_GNUTLS
    ENABLE_MBEDTLS
    ENABLE_OPENSSL
    ENABLE_WINDOWS_CRYPTO
    ENABLE_BZIP2
    ENABLE_LZMA
    ENABLE_ZSTD
    ENABLE_FDOPEN
    BUILD_TOOLS
    BUILD_REGRESS
    BUILD_OSSFUZZ
    BUILD_EXAMPLES
    BUILD_DOC
    LIBZIP_DO_INSTALL)
    set(${_libzip_option} OFF CACHE BOOL "libzip: disabled by appbox" FORCE)
endforeach()

# The wxWidgets build tree probes snprintf with check_function_exists(),
# which links a bare "snprintf" symbol. The UCRT does not export that symbol
# (its snprintf is an inline wrapper), so the check fails and caches
# HAVE_SNPRINTF=0. libzip reuses the cached value for its own
# check_symbol_exists(), and compat.h would then alias snprintf to
# _snprintf, which is a hard error (C1189) against the UCRT headers. The
# UCRT does provide snprintf, so force the correct result.
if(WIN32 AND MSVC)
    set(HAVE_SNPRINTF 1 CACHE INTERNAL "snprintf is available in the UCRT headers" FORCE)
endif()

# Build the third-party library (specify the binary directory to avoid conflicts)
add_subdirectory("${_libzip_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/third_party/libzip-build")

# BUILD_SHARED_LIBS is deliberately left at OFF here: the top level
# CMakeLists.txt requires every dependency to be static, and restoring a
# previously cached ON value would turn the later third-party projects into
# DLLs again.

set(libzip_FOUND TRUE)
