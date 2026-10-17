# That every source file someone might copy out of the repository names its
# copyright holder and its licence in its first lines, as an SPDX identifier.
# LICENSE covers the repository, but a file copied out of it leaves LICENSE
# behind, and the header is then the only thing that says what the copy may be
# used for.
#
# The files are the ones that leave: include/ and src/, which are what an
# application compiles into its image and the headers it may vendor one at a
# time, and samples/, whose adapter and main.cpp are the code most likely to be
# lifted wholesale into someone's own board. The rest -- tests, build files,
# scripts, CI -- only ever travels with the repository, and LICENSE beside it.
#
# The file has two halves, as stripseq_cookbook_check.cmake does. Included from
# CMakeLists.txt, it defines stripseq_add_spdx_check(), which registers the
# CTest entries. Run in script mode, it is the check itself:
#
#   cmake -DROOT=. -P cmake/stripseq_spdx_check.cmake

if(CMAKE_SCRIPT_MODE_FILE)
    # As in the cookbook check: a script run with -P inherits no policies.
    cmake_minimum_required(VERSION 3.21)

    if(NOT DEFINED ROOT)
        message(FATAL_ERROR "spdx check: -DROOT=<path> is required")
    endif()
    # GLOB_RECURSE ... RELATIVE needs both sides spelled the same way, and a
    # ROOT of "." is not how the glob spells the paths it finds.
    file(REAL_PATH "${ROOT}" ROOT)

    set(_globs)
    foreach(_dir include src samples)
        foreach(_ext h cpp overlay)
            list(APPEND _globs ${ROOT}/${_dir}/*.${_ext})
        endforeach()
    endforeach()
    file(GLOB_RECURSE _files RELATIVE ${ROOT} ${_globs})

    # Enough for the comment block; the header is the first thing in every
    # file, so a match further down would be a header nobody reads.
    set(_limit 256)

    list(LENGTH _files _checked)
    set(_failed 0)
    foreach(_file IN LISTS _files)
        file(READ "${ROOT}/${_file}" _head LIMIT ${_limit})
        if(NOT _head MATCHES "Copyright \\(c\\) [0-9][0-9][0-9][0-9] [^\n]+\n"
           OR NOT _head MATCHES "SPDX-License-Identifier: Apache-2\\.0\n")
            math(EXPR _failed "${_failed} + 1")
            message("No copyright and SPDX header in the first ${_limit} bytes of ${_file}")
        endif()
    endforeach()

    # A tree reorganised so that nothing matches would otherwise pass.
    if(_checked EQUAL 0)
        message(FATAL_ERROR "spdx check: found no files to check under ${ROOT}")
    endif()
    if(_failed GREATER 0)
        message(FATAL_ERROR "spdx check: ${_failed} of ${_checked} files fail")
    endif()
    message(STATUS "spdx check: all ${_checked} files carry the header")
    return()
endif()

# Captured here: inside the function, CMAKE_CURRENT_LIST_FILE is the caller's.
set(_stripseq_spdx_check_file ${CMAKE_CURRENT_LIST_FILE})

#   stripseq_add_spdx_check()     # once, from the tests block
#
# Two entries. The first is the check. The second proves it can fail: it runs
# against a tree holding one file written without the header, and passes only
# if the check reports it.
function(stripseq_add_spdx_check)
    add_test(NAME "spdx:every library and sample source names its copyright and licence"
        COMMAND ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR}
                -P ${_stripseq_spdx_check_file})

    set(_root ${CMAKE_CURRENT_BINARY_DIR}/spdx_check)
    file(WRITE ${_root}/src/bare.cpp "/* A file with no header. */\n")

    set(_name "spdx:a file without the header fails the check")
    add_test(NAME "${_name}"
        COMMAND ${CMAKE_COMMAND} -DROOT=${_root}
                -P ${_stripseq_spdx_check_file})
    set_tests_properties("${_name}" PROPERTIES
        PASS_REGULAR_EXPRESSION "1 of 1 files fail")
endfunction()
