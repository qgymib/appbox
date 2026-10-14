###############################################################################
# Export:
# - sqlite::sqlite3
#
# The submodule below third_party/sqlite carries the canonical SQLite source
# tree, which ships no CMakeLists.txt of its own, so the build of the library is
# described here.
#
# The canonical tree holds no pre-generated code: sqlite3.h, parse.c, parse.h,
# opcodes.c, opcodes.h, keywordhash.h, pragma.h and ctime.c are all produced by
# tools which travel with the tree. Two of those tools are C programs
# (tool/lemon.c, tool/mkkeywordhash.c), the remaining generators are TCL scripts
# (tool/mkopcodeh.tcl, tool/mkopcodec.tcl, tool/mkpragmatab.tcl,
# tool/mkctimec.tcl, tool/mksqlite3h.tcl). The upstream build runs the TCL
# scripts with the interpreter of the tree itself, autosetup/jimsh0.c, which is
# a single file C program; no TCL installation is involved, which is what
# "building SQLite itself does not require TCL" means in
# doc/compile-for-windows.md. This module takes the same route: the interpreter
# is compiled from the in-tree source as a host tool and drives the generators,
# so the dependency stays free of external components.
#
# Every generated file is written below the build tree; the submodule stays
# read-only.
#
# Only the core library is built: no source file of ext/ is compiled, so the
# extensions (FTS3, FTS4, FTS5, RTREE, GEOPOLY, session, ...) are not part of
# the library. JSON support and serialized threading mode are part of the core.
###############################################################################

# Prevent duplicate entries
if(TARGET sqlite::sqlite3)
    set(sqlite_FOUND TRUE)
    return()
endif()

# Calculate the path to the third-party source code (relative to the directory containing this file)
set(_sqlite_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../third_party/sqlite")
get_filename_component(_sqlite_SOURCE_DIR "${_sqlite_SOURCE_DIR}" ABSOLUTE)

# Directory of the host tools and of the generated sources. The generated
# sources do not depend on the configuration (neither the sources of the tree
# nor the options below change between Debug and Release), so a single directory
# serves both configurations.
set(_sqlite_BUILD_DIR "${CMAKE_BINARY_DIR}/third_party/sqlite-build")
set(_sqlite_GENERATED_DIR "${_sqlite_BUILD_DIR}/generated")

###############################################################################
# Compile time options
#
# The list holds the options of the default Windows build of SQLite which need
# no source file from ext/; see the OPT_FEATURE_FLAGS block of Makefile.msc.
# Options which only exist to enable an extension (SQLITE_ENABLE_FTS5,
# SQLITE_ENABLE_RTREE, ...) are deliberately left out together with the
# extension sources they would need.
#
# SQLITE_ENABLE_MATH_FUNCTIONS needs the math library on a Unix system; the
# products of this repository are Windows only, where the functions are part of
# the C runtime.
###############################################################################
set(_sqlite_FEATURE_FLAGS
    SQLITE_THREADSAFE=1
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_ENABLE_COLUMN_METADATA=1
    SQLITE_ENABLE_MATH_FUNCTIONS=1
    SQLITE_ENABLE_PERCENTILE=1
)

###############################################################################
# Host tools
#
# The generators are built from the sources of the tree, so a build needs no
# program beside the compiler. They are excluded from the default build and are
# built on demand, because they are only needed while the sources are generated.
###############################################################################
add_executable(sqlite_jimsh EXCLUDE_FROM_ALL
    "${_sqlite_SOURCE_DIR}/autosetup/jimsh0.c"
)

# The interpreter of the tree needs to know how to resolve a path: the [file
# normalize] command which the generators use returns an empty string without
# one of the two macros, which is what the check of the jimsh rule of main.mk
# reports. JIM_COMPAT, which the [expr] command needs for more than one
# argument, is already defined by jimsh0.c itself.
if(MSVC)
    target_compile_definitions(sqlite_jimsh PRIVATE HAVE__FULLPATH=1)
else()
    target_compile_definitions(sqlite_jimsh PRIVATE HAVE_REALPATH=1)
endif()

add_executable(sqlite_lemon EXCLUDE_FROM_ALL
    "${_sqlite_SOURCE_DIR}/tool/lemon.c"
)

# The keyword table is cut at compile time, so this tool carries the same
# options as the library (see the OPT_FEATURE_FLAGS comment of main.mk).
add_executable(sqlite_mkkeywordhash EXCLUDE_FROM_ALL
    "${_sqlite_SOURCE_DIR}/tool/mkkeywordhash.c"
)
target_compile_definitions(sqlite_mkkeywordhash PRIVATE ${_sqlite_FEATURE_FLAGS})

# Prints the source id of the check-in, which mksqlite3h.tcl records in
# sqlite3.h. The name of the program is part of the interface of that script:
# it runs the program `mksourceid` of its working directory, which is why the
# target is written under the name the tree uses.
add_executable(sqlite_mksourceid EXCLUDE_FROM_ALL
    "${_sqlite_SOURCE_DIR}/tool/mksourceid.c"
)
set_target_properties(sqlite_mksourceid PROPERTIES OUTPUT_NAME "mksourceid")

###############################################################################
# Generated sources
#
# One command runs the whole pipeline: cmake -P executes
# GenerateSqliteSources.cmake, which copies the inputs of the generators, runs
# the tools and checks the exit code of every step. Keeping the pipeline in a
# single script keeps the failure of one generator a single message of the build
# log instead of a broken source file of a later step.
#
# The options of the library are handed to the script as one argument, separated
# by a vertical bar: a semicolon inside an argument of a custom command survives
# a build tool which treats the semicolon as a list separator, a vertical bar
# does not.
###############################################################################
set(_sqlite_LEMON_FLAGS "")
foreach(_sqlite_flag IN LISTS _sqlite_FEATURE_FLAGS)
    list(APPEND _sqlite_LEMON_FLAGS "-D${_sqlite_flag}")
endforeach()
string(REPLACE ";" "|" _sqlite_LEMON_FLAGS "${_sqlite_LEMON_FLAGS}")

add_custom_command(
    OUTPUT
        "${_sqlite_GENERATED_DIR}/sqlite3.h"
        "${_sqlite_GENERATED_DIR}/parse.c"
        "${_sqlite_GENERATED_DIR}/parse.h"
        "${_sqlite_GENERATED_DIR}/opcodes.c"
        "${_sqlite_GENERATED_DIR}/opcodes.h"
        "${_sqlite_GENERATED_DIR}/keywordhash.h"
        "${_sqlite_GENERATED_DIR}/pragma.h"
        "${_sqlite_GENERATED_DIR}/ctime.c"
    COMMAND ${CMAKE_COMMAND}
        "-DSQLITE_SOURCE_DIR=${_sqlite_SOURCE_DIR}"
        "-DSQLITE_OUTPUT_DIR=${_sqlite_GENERATED_DIR}"
        "-DSQLITE_JIMSH=$<TARGET_FILE:sqlite_jimsh>"
        "-DSQLITE_LEMON=$<TARGET_FILE:sqlite_lemon>"
        "-DSQLITE_MKKEYWORDHASH=$<TARGET_FILE:sqlite_mkkeywordhash>"
        "-DSQLITE_MKSOURCEID=$<TARGET_FILE:sqlite_mksourceid>"
        "-DSQLITE_LEMON_FLAGS=${_sqlite_LEMON_FLAGS}"
        -P "${CMAKE_CURRENT_LIST_DIR}/GenerateSqliteSources.cmake"
    DEPENDS
        sqlite_jimsh
        sqlite_lemon
        sqlite_mkkeywordhash
        sqlite_mksourceid
        "${_sqlite_SOURCE_DIR}/VERSION"
        "${_sqlite_SOURCE_DIR}/manifest"
        "${_sqlite_SOURCE_DIR}/manifest.tags"
        "${_sqlite_SOURCE_DIR}/src/parse.y"
        "${_sqlite_SOURCE_DIR}/src/sqlite.h.in"
        "${_sqlite_SOURCE_DIR}/src/vdbe.c"
        "${_sqlite_SOURCE_DIR}/tool/lempar.c"
        "${_sqlite_SOURCE_DIR}/tool/mkctimec.tcl"
        "${_sqlite_SOURCE_DIR}/tool/mkkeywordhash.c"
        "${_sqlite_SOURCE_DIR}/tool/mkopcodec.tcl"
        "${_sqlite_SOURCE_DIR}/tool/mkopcodeh.tcl"
        "${_sqlite_SOURCE_DIR}/tool/mkpragmatab.tcl"
        "${_sqlite_SOURCE_DIR}/tool/mksqlite3h.tcl"
    COMMENT "Generating the sources of sqlite"
    VERBATIM
)

# A generated source file has to be marked as such: the file does not exist
# while the build tree is configured, so the generator would report it as a
# missing source file.
set_source_files_properties(
    "${_sqlite_GENERATED_DIR}/parse.c"
    "${_sqlite_GENERATED_DIR}/opcodes.c"
    "${_sqlite_GENERATED_DIR}/ctime.c"
    PROPERTIES GENERATED TRUE
)

###############################################################################
# The library
#
# The source list is the object list of the non-amalgamation build of main.mk
# (LIBOBJS0) without the sources of the extensions and without the sources of
# the test programs, which all begin with `test`. The files which implement a
# choice of the build (mem0/mem1/mem2/mem3/mem5, mutex_noop/mutex_unix/
# mutex_w32, os_unix/os_win) compile to an empty translation unit unless their
# option selects them, which is why the official amalgamation carries all of
# them as well.
###############################################################################
set(_sqlite_CORE_SOURCES
    alter.c
    analyze.c
    attach.c
    auth.c
    backup.c
    bitvec.c
    btmutex.c
    btree.c
    build.c
    callback.c
    carray.c
    complete.c
    date.c
    dbpage.c
    dbstat.c
    delete.c
    expr.c
    fault.c
    fkey.c
    func.c
    global.c
    hash.c
    insert.c
    json.c
    legacy.c
    loadext.c
    main.c
    malloc.c
    mem0.c
    mem1.c
    mem2.c
    mem3.c
    mem5.c
    memdb.c
    memjournal.c
    mutex.c
    mutex_noop.c
    mutex_unix.c
    mutex_w32.c
    notify.c
    os.c
    os_kv.c
    os_unix.c
    os_win.c
    pager.c
    pcache.c
    pcache1.c
    pragma.c
    prepare.c
    printf.c
    random.c
    resolve.c
    rowset.c
    select.c
    status.c
    table.c
    threads.c
    tokenize.c
    treeview.c
    trigger.c
    update.c
    upsert.c
    utf.c
    util.c
    vacuum.c
    vdbe.c
    vdbeapi.c
    vdbeaux.c
    vdbeblob.c
    vdbemem.c
    vdbesort.c
    vdbetrace.c
    vdbevtab.c
    vtab.c
    wal.c
    walker.c
    where.c
    wherecode.c
    whereexpr.c
    window.c
)

set(_sqlite_SOURCES "")
foreach(_sqlite_source IN LISTS _sqlite_CORE_SOURCES)
    list(APPEND _sqlite_SOURCES "${_sqlite_SOURCE_DIR}/src/${_sqlite_source}")
endforeach()

list(APPEND _sqlite_SOURCES
    "${_sqlite_GENERATED_DIR}/parse.c"
    "${_sqlite_GENERATED_DIR}/opcodes.c"
    "${_sqlite_GENERATED_DIR}/ctime.c"
)

add_library(sqlite3 STATIC ${_sqlite_SOURCES})

# The public header of the library is one of the generated files, so the
# directory which holds it is part of the interface of the target.
target_include_directories(sqlite3
    PUBLIC
        "$<BUILD_INTERFACE:${_sqlite_GENERATED_DIR}>"
    PRIVATE
        "${_sqlite_SOURCE_DIR}/src"
)

target_compile_definitions(sqlite3 PRIVATE ${_sqlite_FEATURE_FLAGS})

# The name every consumer of the repository uses; the option list above is
# deliberately not part of the interface, because it decides how the library was
# built and not how it is used.
add_library(sqlite::sqlite3 ALIAS sqlite3)

set(sqlite_FOUND TRUE)
