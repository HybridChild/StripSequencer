# One (library, test binary, CTest entries) triple per build configuration.
#
#   rgbseq_add_variant(default)                       # config.h's own defaults
#   rgbseq_add_variant(rgbw   RGBSEQ_WHITE_CHANNEL=1)
#   rgbseq_add_variant(tight  RGBSEQ_MAX_STEPS=3 RGBSEQ_MAX_SEGMENTS=1)
#
# RGBSEQ_WHITE_CHANNEL and RGBSEQ_GAMMA change compiled behaviour rather than
# just a constant -- the first changes sizeof(Color), sizeof(Step) and
# Strip::bytes_per_pixel, the second changes the output conversion and whether a
# table is linked -- so no single compiled artifact represents both settings and
# the whole library is built once per combination.
function(rgbseq_add_variant name)
    set(lib rgbseq_${name})
    set(exe tests_${name})

    add_library(${lib} STATIC ${RGBSEQ_SOURCES})
    target_include_directories(${lib} PUBLIC ${RGBSEQ_INCLUDE_DIR})
    target_compile_features(${lib} PUBLIC cxx_std_20)

    # PUBLIC is load-bearing: it propagates to ${exe}, so the tests cannot
    # disagree with the library about the layout of Color. They would link
    # anyway if they did -- the mangled names do not change -- so nothing
    # downstream would catch it. tests/layout.cpp is what catches it here.
    target_compile_definitions(${lib} PUBLIC ${ARGN})

    # PRIVATE: our warnings are not a consumer's problem.
    target_compile_options(${lib} PRIVATE ${RGBSEQ_WARNINGS})

    add_executable(${exe} ${RGBSEQ_TEST_SOURCES})
    target_link_libraries(${exe} PRIVATE ${lib} doctest::doctest)
    target_compile_options(${exe} PRIVATE ${RGBSEQ_WARNINGS})

    # The same definitions again, renamed, straight onto the test target -- a
    # second path that does not go through the PUBLIC propagation above.
    # tests/layout.cpp compares the two, which is the only way the PRIVATE
    # mistake can be caught: with it, the library sees RGBSEQ_WHITE_CHANNEL=1
    # and the tests do not, and the mismatch is invisible to compiler and
    # linker alike.
    set(expect "")
    foreach(def IN LISTS ARGN)
        string(REPLACE "RGBSEQ_" "RGBSEQ_EXPECT_" renamed "${def}")
        list(APPEND expect ${renamed})
    endforeach()
    target_compile_definitions(${exe} PRIVATE ${expect})

    doctest_discover_tests(${exe} TEST_PREFIX "${name}:")
endfunction()
