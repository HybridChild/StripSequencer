# StripSequencer -- a standalone, OS-agnostic C++20 library for animating
# WS2812-style LED strips. See docs/DESIGN.md for the mechanism.
#
# 3.21 is the version that introduced PROJECT_IS_TOP_LEVEL, which everything
# below uses to keep our own build settings out of a consumer's build.
cmake_minimum_required(VERSION 3.21)

project(StripSequencer VERSION 0.1.0 LANGUAGES CXX)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/sources.cmake)

# These seed each target's properties at the moment the target is created, so
# they must come before the first add_library -- set any later, they silently
# miss every target defined above them.
if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_CXX_EXTENSIONS OFF)              # -std=c++20, not -std=gnu++20
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)      # for clangd
endif()

# ---------------------------------------------------------------------------
# The canonical library
#
# Built with no definitions at all, so a consumer gets config.h's declared
# defaults and can override them from its own build. The five variants below
# are test scaffolding and are deliberately not what gets exported: aliasing one
# of them would quietly push that variant's PUBLIC definitions onto every
# consumer, pinning their configuration from inside the library.
# ---------------------------------------------------------------------------

add_library(stripseq STATIC ${STRIPSEQ_SOURCES})
add_library(StripSequencer::stripseq ALIAS stripseq)

target_include_directories(stripseq PUBLIC ${STRIPSEQ_INCLUDE_DIR})

# A requirement, not a flag: CMake reconciles it with whatever the consumer
# already asked for. This is the only thing the library says about the
# toolchain -- Zephyr owns the compiler, the standard library, the optimisation
# level and the warning set for anything built inside it.
#
# C++20 rather than 17 for two things. consteval is what makes an invalid
# sequence known at compile time a compile error, through build_const(); and
# constinit is what lets a Strip sit at file scope with no constructor running
# before main. Under 17 the first falls back to a runtime check and the second
# to a dynamic initialiser.
target_compile_features(stripseq PUBLIC cxx_std_20)

# ---------------------------------------------------------------------------
# Everything below shapes *our* build only
# ---------------------------------------------------------------------------

if(PROJECT_IS_TOP_LEVEL)
    # -Wconversion because the arithmetic is deliberately narrow -- uint8_t
    # channels, uint16_t pixel indices, int32_t delays -- and a narrowing the
    # author did not mean is exactly the bug the golden hashes catch only if a
    # test happens to reach it. Every intended narrowing is a visible cast.
    #
    # -Wsign-conversion spelled out because gcc's -Wconversion leaves signedness
    # out in C++ while clang's includes it; without it the two compilers in the
    # matrix would enforce different rules.
    #
    # -Wundef because the configuration is read with #if: a misspelled
    # `#if STRIPSEQ_GAMMMA` is 0, and quietly compiles the other variant.
    set(STRIPSEQ_WARNINGS -Wall -Wextra -Wpedantic -Wshadow
        -Wconversion -Wsign-conversion -Wundef)

    target_compile_options(stripseq PRIVATE ${STRIPSEQ_WARNINGS})
endif()

option(STRIPSEQ_BUILD_TESTS "Build the host test suite" ${PROJECT_IS_TOP_LEVEL})

if(STRIPSEQ_BUILD_TESTS)
    # doctest: installed copy first, fetched only if there is not one. The
    # installed path needs no network and no build; the fetched path pins an
    # exact tag. CI has no doctest installed, so both stay exercised.
    #
    # doctestConfig.cmake appends its own directory to CMAKE_MODULE_PATH, which
    # is why a single include(doctest) serves both paths.
    find_package(doctest 2.5 QUIET)
    if(NOT doctest_FOUND)
        include(FetchContent)
        FetchContent_Declare(doctest
            GIT_REPOSITORY https://github.com/doctest/doctest.git
            GIT_TAG        v2.5.3
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(doctest)
        list(APPEND CMAKE_MODULE_PATH ${doctest_SOURCE_DIR}/scripts/cmake)
    endif()
    include(doctest)   # doctest_discover_tests

    enable_testing()

    # Not in cmake/sources.cmake: that file is shared with the Zephyr build,
    # which must never see the tests.
    set(STRIPSEQ_TEST_SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/tests/doctest_main.cpp
        ${CMAKE_CURRENT_LIST_DIR}/tests/layout.cpp
        ${CMAKE_CURRENT_LIST_DIR}/tests/color.cpp
        ${CMAKE_CURRENT_LIST_DIR}/tests/convert.cpp
        ${CMAKE_CURRENT_LIST_DIR}/tests/sequence.cpp
        ${CMAKE_CURRENT_LIST_DIR}/tests/fuzz.cpp
        ${CMAKE_CURRENT_LIST_DIR}/tests/sequencer.cpp
        ${CMAKE_CURRENT_LIST_DIR}/tests/segment.cpp
        ${CMAKE_CURRENT_LIST_DIR}/tests/strip.cpp
        ${CMAKE_CURRENT_LIST_DIR}/tests/cookbook.cpp)

    include(${CMAKE_CURRENT_LIST_DIR}/cmake/stripseq_variant.cmake)
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/stripseq_compile_fail.cmake)
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/stripseq_cookbook_check.cmake)
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/stripseq_spdx_check.cmake)

    # The tests that must fail to compile, run here at configure time; see the
    # file for why they cannot be test cases.
    stripseq_check_build_const()
    stripseq_check_config_range()

    # That the cookbook in docs/COOKBOOK.md and the one the tests compile are
    # the same text; see the file for how it compares them.
    stripseq_add_cookbook_check()

    # That every library and sample source carries its copyright and licence
    # header; see the file for why those and no others.
    stripseq_add_spdx_check()

    # Five, not four. The cross product of the two behavioural macros is four,
    # and `default` is one of them -- called with no definitions at all, so that
    # config.h's #ifndef values are exercised *as* the defaults. Each of the
    # others passes only its deviations, for the same reason. The fifth is
    # `tight`: the capacities are plain constants and do not need their own
    # cross product, but one cheap variant catches anything that quietly assumes
    # 8 -- an off-by-one in the builder's capacity check, or a test with a
    # hardcoded step count. Its values are the most the cookbook needs, Rainbow's
    # six steps and the overlay's two segments, so the recipes run there exactly
    # at capacity rather than not at all.
    stripseq_add_variant(default)
    stripseq_add_variant(rgbw          STRIPSEQ_WHITE_CHANNEL=1)
    stripseq_add_variant(nogamma       STRIPSEQ_GAMMA=0)
    stripseq_add_variant(rgbw_nogamma  STRIPSEQ_WHITE_CHANNEL=1 STRIPSEQ_GAMMA=0)
    stripseq_add_variant(tight         STRIPSEQ_MAX_STEPS=6 STRIPSEQ_MAX_SEGMENTS=2)
endif()
