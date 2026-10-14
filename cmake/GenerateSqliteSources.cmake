# GenerateSqliteSources.cmake
#
# Build time generator of the source files the canonical SQLite tree does not
# carry. The tree below third_party/sqlite is the check-out of the SQLite
# developers, which holds the inputs of the code generators but none of their
# results, so sqlite3.h, parse.c, parse.h, opcodes.c, opcodes.h, keywordhash.h,
# pragma.h and ctime.c have to be produced before the library can be compiled.
#
# The script is executed by the custom command of cmake/Findsqlite.cmake with
# `cmake -P`, so it runs at build time, when the host tools of the tree exist.
# Every step mirrors the rule of main.mk / Makefile.msc of the tree, and every
# step fails the build when the tool reports an error: a generator which is not
# run leaves a missing or stale source file behind, which would surface as a
# confusing compile error much later.
#
# Required input variables:
#   SQLITE_SOURCE_DIR    Root of the canonical SQLite source tree.
#   SQLITE_OUTPUT_DIR    Directory the generated files are written to; it is
#                        also the working directory of every tool, because the
#                        generators read and write relative paths.
#   SQLITE_JIMSH         Interpreter of the tree (autosetup/jimsh0.c), which
#                        runs the TCL generators.
#   SQLITE_LEMON         Parser generator of the tree (tool/lemon.c).
#   SQLITE_MKKEYWORDHASH Keyword hash generator of the tree
#                        (tool/mkkeywordhash.c).
#   SQLITE_MKSOURCEID    Source id generator of the tree (tool/mksourceid.c);
#                        mksqlite3h.tcl runs the program of that name which it
#                        finds in its working directory.
#   SQLITE_LEMON_FLAGS   Options of the library as -D options of Lemon,
#                        separated by a vertical bar; may be empty.
#
# Usage:
#   cmake -DSQLITE_SOURCE_DIR=<> -DSQLITE_OUTPUT_DIR=<> -DSQLITE_JIMSH=<>
#         -DSQLITE_LEMON=<> -DSQLITE_MKKEYWORDHASH=<> -DSQLITE_MKSOURCEID=<>
#         [-DSQLITE_LEMON_FLAGS=<>] -P GenerateSqliteSources.cmake

cmake_minimum_required(VERSION 3.15)

foreach(_required
        SQLITE_SOURCE_DIR
        SQLITE_OUTPUT_DIR
        SQLITE_JIMSH
        SQLITE_LEMON
        SQLITE_MKKEYWORDHASH
        SQLITE_MKSOURCEID)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR
                "Usage: cmake -DSQLITE_SOURCE_DIR=<> -DSQLITE_OUTPUT_DIR=<> "
                "-DSQLITE_JIMSH=<> -DSQLITE_LEMON=<> -DSQLITE_MKKEYWORDHASH=<> "
                "-DSQLITE_MKSOURCEID=<> [-DSQLITE_LEMON_FLAGS=<>] "
                "-P GenerateSqliteSources.cmake")
    endif()
endforeach()

if(NOT DEFINED SQLITE_LEMON_FLAGS)
    set(SQLITE_LEMON_FLAGS "")
endif()

# The options arrive as a single argument separated by vertical bars: a
# semicolon inside an argument of a custom command does not survive every build
# tool, a vertical bar does.
string(REPLACE "|" ";" _lemon_flags "${SQLITE_LEMON_FLAGS}")

file(MAKE_DIRECTORY "${SQLITE_OUTPUT_DIR}")

# Run one step of the pipeline and fail the build when it reports an error.
#
# Every step runs in the output directory, which is what the generators expect:
# Lemon looks for the parser template and for the grammar in the working
# directory, and mksqlite3h.tcl runs the source id program it finds there.
#
# Arguments:
#   description  Name of the step, used by the log and the error message.
#   input_file   File connected to the standard input of the command, empty
#                when the command reads no input.
#   output_file  File the standard output of the command is written to, empty
#                when the output is only reported on failure.
#   ...          The command line.
function(sqlite_step description input_file output_file)
    set(_redirection "")
    if(NOT "${input_file}" STREQUAL "")
        list(APPEND _redirection INPUT_FILE "${input_file}")
    endif()

    if(NOT "${output_file}" STREQUAL "")
        list(APPEND _redirection OUTPUT_FILE "${output_file}")
    else()
        list(APPEND _redirection OUTPUT_VARIABLE _stdout)
    endif()

    message(STATUS "GenerateSqliteSources: ${description}")

    execute_process(
        COMMAND ${ARGN}
        WORKING_DIRECTORY "${SQLITE_OUTPUT_DIR}"
        ${_redirection}
        RESULT_VARIABLE _result
        ERROR_VARIABLE _stderr
    )

    if(NOT _result STREQUAL "0")
        message(FATAL_ERROR
                "GenerateSqliteSources: ${description} failed with exit code ${_result}.\n"
                "  command: ${ARGN}\n"
                "  error: ${_stderr}")
    endif()
endfunction()

###############################################################################
# Inputs of the generators
#
# Lemon reads the grammar and the parser template from its working directory,
# and mksqlite3h.tcl runs the source id program of its working directory, so the
# three files are copied next to the generated sources first.
###############################################################################
file(COPY
    "${SQLITE_SOURCE_DIR}/src/parse.y"
    "${SQLITE_SOURCE_DIR}/tool/lempar.c"
    "${SQLITE_MKSOURCEID}"
    DESTINATION "${SQLITE_OUTPUT_DIR}"
)

###############################################################################
# parse.c and parse.h: the SQL parser, generated from the grammar by Lemon.
#
# The options of the library are handed to Lemon, because they select parts of
# the grammar; -S adds the report of the parser tables, which the build of the
# tree asks for as well.
###############################################################################
sqlite_step("the parser generator (parse.c, parse.h)"
            "" ""
            "${SQLITE_LEMON}" ${_lemon_flags} -S parse.y)

###############################################################################
# keywordhash.h: the table which maps the SQL keywords onto their token codes.
#
# The generator takes no argument and writes the header to its standard output;
# the options of the library are compiled into it (see Findsqlite.cmake).
###############################################################################
sqlite_step("the keyword hash generator (keywordhash.h)"
            "" "${SQLITE_OUTPUT_DIR}/keywordhash.h"
            "${SQLITE_MKKEYWORDHASH}")

###############################################################################
# opcodes.h: the numbers of the virtual machine opcodes.
#
# The generator reads the token codes of parse.h followed by the opcode
# descriptions of src/vdbe.c from its standard input, so the two files are
# concatenated first. `cmake -E cat` copies the bytes of the inputs without
# touching them, which a text mode concatenation of a build script would not
# guarantee.
###############################################################################
sqlite_step("the concatenation of parse.h and vdbe.c (opcodes input)"
            "" "${SQLITE_OUTPUT_DIR}/opcodes.in"
            "${CMAKE_COMMAND}" -E cat
            "${SQLITE_OUTPUT_DIR}/parse.h" "${SQLITE_SOURCE_DIR}/src/vdbe.c")

sqlite_step("the opcode number generator (opcodes.h)"
            "${SQLITE_OUTPUT_DIR}/opcodes.in" "${SQLITE_OUTPUT_DIR}/opcodes.h"
            "${SQLITE_JIMSH}" "${SQLITE_SOURCE_DIR}/tool/mkopcodeh.tcl")

###############################################################################
# opcodes.c: the reverse mapping of an opcode number onto its name, which the
# EXPLAIN output uses. It is generated from the header above.
###############################################################################
sqlite_step("the opcode name generator (opcodes.c)"
            "" "${SQLITE_OUTPUT_DIR}/opcodes.c"
            "${SQLITE_JIMSH}" "${SQLITE_SOURCE_DIR}/tool/mkopcodec.tcl"
            "${SQLITE_OUTPUT_DIR}/opcodes.h")

###############################################################################
# pragma.h: the table of the built-in PRAGMA statements.
###############################################################################
sqlite_step("the pragma table generator (pragma.h)"
            "" ""
            "${SQLITE_JIMSH}" "${SQLITE_SOURCE_DIR}/tool/mkpragmatab.tcl"
            "${SQLITE_OUTPUT_DIR}/pragma.h")

###############################################################################
# ctime.c: the list of the compile time options the library reports through
# sqlite3_compileoption_get() and sqlite3_compileoption_used().
###############################################################################
sqlite_step("the compile option generator (ctime.c)"
            "" ""
            "${SQLITE_JIMSH}" "${SQLITE_SOURCE_DIR}/tool/mkctimec.tcl"
            "${SQLITE_OUTPUT_DIR}/ctime.c")

###############################################################################
# sqlite3.h: the public header, which is assembled from src/sqlite.h.in and the
# headers of the extensions plus the version of VERSION and the source id of the
# check-in.
#
# The tree is handed to the generator in the spelling the build of the tree uses
# (forward slashes), because the script resolves the paths of the tree with the
# [file normalize] command of its interpreter.
###############################################################################
string(REPLACE "\\" "/" _source_dir "${SQLITE_SOURCE_DIR}")

sqlite_step("the public header generator (sqlite3.h)"
            "" ""
            "${SQLITE_JIMSH}" "${SQLITE_SOURCE_DIR}/tool/mksqlite3h.tcl"
            "${_source_dir}" -o "${SQLITE_OUTPUT_DIR}/sqlite3.h")

message(STATUS "GenerateSqliteSources: the sources were generated in ${SQLITE_OUTPUT_DIR}")
