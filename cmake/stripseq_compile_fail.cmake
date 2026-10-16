# The half of SequenceBuilder::build_const() that a test case cannot reach: an
# invalid sequence must fail to *compile*, and a file that fails to compile
# cannot also be the file that runs the check.
#
# So the check runs at configure time instead. try_compile builds
# tests/compile_fail/build_const.cpp once per case and reports whether it
# compiled; a case that compiles when it should not stops the configure.
#
# Failing is not enough on its own. A probe broken for some unrelated reason --
# a missing include, a typo -- fails too, and would pass silently. So each case
# also names the function build_const() calls for its Error, and the compiler's
# output must contain it. Case 0 is the control: the same probe, valid, which
# must compile, so a probe that cannot compile at all is caught as well.
#
#   stripseq_check_build_const()      # once, from the tests block

# Captured here: inside the function, CMAKE_CURRENT_LIST_DIR is the caller's.
set(_stripseq_compile_fail_dir ${CMAKE_CURRENT_LIST_DIR})

function(stripseq_check_build_const)
    set(_probe ${_stripseq_compile_fail_dir}/../tests/compile_fail/build_const.cpp)

    # A configure-time check runs only when CMake configures, and an edit to a
    # header is not something CMake configures for: the build recompiles the
    # tests and runs no probe. Listing what the probe compiles as configure
    # dependencies makes the next build after any edit to them reconfigure,
    # and so re-run every probe. The list is the probe and every header it
    # reaches; a new include in sequence.h belongs here too.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        ${_probe}
        ${STRIPSEQ_INCLUDE_DIR}/stripseq/sequence.h
        ${STRIPSEQ_INCLUDE_DIR}/stripseq/color.h
        ${STRIPSEQ_INCLUDE_DIR}/stripseq/config.h)

    set(_cases
        "0:"
        "1:sequence_has_no_steps"
        "2:sequence_has_more_steps_than_STRIPSEQ_MAX_STEPS"
        "3:sequence_has_a_zero_duration_fade"
        "4:sequence_repeat_count_is_zero"
        "5:sequence_is_endless_but_has_a_landing_color"
        "6:sequence_is_endless_but_has_a_start_color"
        "7:sequence_is_longer_than_2_pow_29_ms"
        "8:sequence_is_endless_but_its_loop_is_0_ms")

    # Compile only: the probe has no main(), and a link would add nothing.
    # Local to this function, so it reaches no other try_compile in the build.
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

    foreach(_case IN LISTS _cases)
        string(REPLACE ":" ";" _case "${_case}")
        list(GET _case 0 _n)
        list(LENGTH _case _len)
        set(_expect "")
        if(_len GREATER 1)
            list(GET _case 1 _expect)
        endif()

        # A bare try_compile, never check_cxx_source_compiles: the check_*()
        # modules skip any check whose result is already in the cache, so this
        # one would run once per build tree and report that first answer
        # forever, whatever the headers became. try_compile itself always
        # compiles; it only records the result, as an INTERNAL cache entry
        # that each case overwrites.
        try_compile(_ok
            ${CMAKE_CURRENT_BINARY_DIR}/compile_fail/${_n}
            ${_probe}
            CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${STRIPSEQ_INCLUDE_DIR}"
            COMPILE_DEFINITIONS -DSTRIPSEQ_PROBE=${_n}
            CXX_STANDARD 20
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF
            OUTPUT_VARIABLE _output)

        if(_expect STREQUAL "")
            if(NOT _ok)
                message(FATAL_ERROR
                    "build_const() probe ${_n}, the valid control, failed to "
                    "compile:\n${_output}")
            endif()
        elseif(_ok)
            message(FATAL_ERROR
                "build_const() accepted an invalid sequence "
                "(probe ${_n}, expected ${_expect}).")
        elseif(NOT _output MATCHES "${_expect}")
            message(FATAL_ERROR
                "build_const() probe ${_n} failed to compile, but not by "
                "calling ${_expect}:\n${_output}")
        endif()
    endforeach()

    message(STATUS "build_const() refuses every invalid sequence it was shown")
endfunction()
