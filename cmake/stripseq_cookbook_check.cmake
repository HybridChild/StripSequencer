# That every cookbook recipe in docs/API-SKETCH.md appears, verbatim, in
# tests/cookbook.cpp. A hand-copied recipe drifts the first time the sketch is
# edited and the test is not, and the cookbook's whole value as an acceptance
# suite is that it is the documented API rather than a convenient variant of it.
#
# The file has two halves. Included from CMakeLists.txt, it defines
# stripseq_add_cookbook_check(), which registers the CTest entries. Run in
# script mode, it is the check itself:
#
#   cmake -DSKETCH=docs/API-SKETCH.md -DCOOKBOOK=tests/cookbook.cpp \
#         -P cmake/stripseq_cookbook_check.cmake
#
# It reads text rather than compiled code, so it is one CTest entry rather than
# one per variant.
#
# What it compares: every fenced cpp block under the sketch's "The cookbook as
# code", split at blank lines into chunks, each chunk with its runs of
# whitespace collapsed to one space, must be a substring of cookbook.cpp
# normalised the same way. Chunking is what lets the harness put a recipe's
# declarations at file scope and its statements in a function; normalising is
# what lets it indent them. A block whose first line says it is "not
# application code" -- Strip::drop_finished's loop -- is skipped.
#
# Everything is done with string(FIND) and string(SUBSTRING), never with lists.
# A CMake list is a string split at semicolons, and every line of C++ has one,
# so the source text must never pass through a list operation.

if(CMAKE_SCRIPT_MODE_FILE)
    foreach(_var SKETCH COOKBOOK)
        if(NOT DEFINED ${_var})
            message(FATAL_ERROR "cookbook check: -D${_var}=<path> is required")
        endif()
    endforeach()

    file(READ "${SKETCH}" _sketch)
    file(READ "${COOKBOOK}" _cookbook)

    string(REGEX REPLACE "[ \t\r\n]+" " " _haystack "${_cookbook}")

    # The section: from its heading to the next level-two heading.
    string(FIND "${_sketch}" "\n## The cookbook as code\n" _start)
    if(_start EQUAL -1)
        message(FATAL_ERROR "cookbook check: no \"## The cookbook as code\" in ${SKETCH}")
    endif()
    math(EXPR _start "${_start} + 1")
    string(SUBSTRING "${_sketch}" ${_start} -1 _section)
    string(SUBSTRING "${_section}" 1 -1 _rest)
    string(FIND "${_rest}" "\n## " _end)
    if(NOT _end EQUAL -1)
        math(EXPR _end "${_end} + 1")
        string(SUBSTRING "${_section}" 0 ${_end} _section)
    endif()

    set(_open "```cpp\n")
    set(_close "\n```")
    string(LENGTH "${_open}" _open_len)

    set(_blocks 0)
    set(_chunks 0)
    set(_missing 0)
    while(TRUE)
        string(FIND "${_section}" "${_open}" _at)
        if(_at EQUAL -1)
            break()
        endif()
        math(EXPR _at "${_at} + ${_open_len}")
        string(SUBSTRING "${_section}" ${_at} -1 _section)
        string(FIND "${_section}" "${_close}" _stop)
        if(_stop EQUAL -1)
            message(FATAL_ERROR "cookbook check: an unterminated cpp block in ${SKETCH}")
        endif()
        string(SUBSTRING "${_section}" 0 ${_stop} _block)
        string(SUBSTRING "${_section}" ${_stop} -1 _section)

        string(FIND "${_block}" "\n" _eol)
        string(SUBSTRING "${_block}" 0 ${_eol} _first_line)
        if(_first_line MATCHES "not application code")
            continue()
        endif()
        math(EXPR _blocks "${_blocks} + 1")

        # A blank line is one holding only whitespace. The separator is a
        # marker that appears in neither file.
        set(_sep "@@chunk@@")
        string(LENGTH "${_sep}" _sep_len)
        string(REGEX REPLACE "\n[ \t]*\n" "\n${_sep}" _block "${_block}")
        while(NOT _block STREQUAL "")
            string(FIND "${_block}" "${_sep}" _cut)
            if(_cut EQUAL -1)
                set(_chunk "${_block}")
                set(_block "")
            else()
                string(SUBSTRING "${_block}" 0 ${_cut} _chunk)
                math(EXPR _cut "${_cut} + ${_sep_len}")
                string(SUBSTRING "${_block}" ${_cut} -1 _block)
            endif()

            string(REGEX REPLACE "[ \t\r\n]+" " " _needle "${_chunk}")
            string(STRIP "${_needle}" _needle)
            if(_needle STREQUAL "")
                continue()
            endif()
            math(EXPR _chunks "${_chunks} + 1")

            string(FIND "${_haystack}" "${_needle}" _found)
            if(_found EQUAL -1)
                math(EXPR _missing "${_missing} + 1")
                message("Not in ${COOKBOOK}, from cookbook block ${_blocks}:\n${_chunk}\n")
            endif()
        endwhile()
    endwhile()

    # A sketch reorganised so that nothing matches would otherwise pass.
    if(_chunks EQUAL 0)
        message(FATAL_ERROR "cookbook check: found no cookbook blocks in ${SKETCH}")
    endif()
    if(_missing GREATER 0)
        message(FATAL_ERROR
            "cookbook check: ${_missing} of ${_chunks} chunks from the sketch "
            "are not in ${COOKBOOK}")
    endif()
    message(STATUS "cookbook check: all ${_chunks} chunks of ${_blocks} blocks match")
    return()
endif()

# Captured here: inside the function, CMAKE_CURRENT_LIST_FILE is the caller's.
set(_stripseq_cookbook_check_file ${CMAKE_CURRENT_LIST_FILE})

#   stripseq_add_cookbook_check()     # once, from the tests block
#
# Two entries. The first is the check. The second proves it can fail: it runs
# the check against a copy of the sketch with one token of Blink changed, and
# passes only if the check reports that chunk missing. Without it, a check that
# matched everything -- an empty section, a chunk split that produced nothing
# -- would pass forever unnoticed.
function(stripseq_add_cookbook_check)
    set(_sketch ${PROJECT_SOURCE_DIR}/docs/API-SKETCH.md)
    set(_cookbook ${PROJECT_SOURCE_DIR}/tests/cookbook.cpp)

    add_test(NAME "verbatim:every cookbook recipe in the sketch is in tests/cookbook.cpp"
        COMMAND ${CMAKE_COMMAND} -DSKETCH=${_sketch} -DCOOKBOOK=${_cookbook}
                -P ${_stripseq_cookbook_check_file})

    # The mutated copy is written at configure time, so the sketch is a
    # configure dependency: an edit to it rewrites the copy.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_sketch})
    file(READ ${_sketch} _text)
    set(_from "hold(colors::red, 500).hold(colors::black, 500)")
    set(_to   "hold(colors::red, 501).hold(colors::black, 500)")
    string(FIND "${_text}" "${_from}" _at)
    if(_at EQUAL -1)
        message(FATAL_ERROR
            "cookbook check: the self-test mutates Blink's \"${_from}\", which "
            "is no longer in the sketch; pick another token in "
            "${CMAKE_CURRENT_FUNCTION_LIST_FILE}")
    endif()
    string(REPLACE "${_from}" "${_to}" _text "${_text}")
    set(_mutated ${CMAKE_CURRENT_BINARY_DIR}/cookbook_check/API-SKETCH-mutated.md)
    file(WRITE ${_mutated} "${_text}")

    set(_name "verbatim:a sketch recipe changed by one token fails the check")
    add_test(NAME "${_name}"
        COMMAND ${CMAKE_COMMAND} -DSKETCH=${_mutated} -DCOOKBOOK=${_cookbook}
                -P ${_stripseq_cookbook_check_file})
    set_tests_properties("${_name}" PROPERTIES
        PASS_REGULAR_EXPRESSION "1 of [0-9]+ chunks from the sketch are not in")
endfunction()
