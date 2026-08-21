# RgbSequencer -- a standalone, OS-agnostic C++20 library for animating
# WS2812-style LED strips. See docs/DESIGN.md for the mechanism and
# docs/IMPLEMENTATION-PLAN.md for how it gets built.
#
# 3.21 is the version that introduced PROJECT_IS_TOP_LEVEL, which everything
# below uses to keep our own build settings out of a consumer's build.
cmake_minimum_required(VERSION 3.21)

project(RgbSequencer VERSION 0.1.0 LANGUAGES CXX)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/sources.cmake)

# ---------------------------------------------------------------------------
# The canonical library
#
# Built with no definitions at all, so a consumer gets config.h's declared
# defaults and can override them from its own build. The five variants below
# are test scaffolding and are deliberately not what gets exported: aliasing one
# of them would quietly push that variant's PUBLIC definitions onto every
# consumer, pinning their configuration from inside the library.
# ---------------------------------------------------------------------------

add_library(rgbseq STATIC ${RGBSEQ_SOURCES})
add_library(RgbSequencer::rgbseq ALIAS rgbseq)

target_include_directories(rgbseq PUBLIC ${RGBSEQ_INCLUDE_DIR})

# A requirement, not a flag: CMake reconciles it with whatever the consumer
# already asked for. This is the only thing the library says about the
# toolchain -- Zephyr owns the compiler, the standard library, the optimisation
# level and the warning set for anything built inside it.
target_compile_features(rgbseq PUBLIC cxx_std_20)

# ---------------------------------------------------------------------------
# Everything below shapes *our* build only
# ---------------------------------------------------------------------------

if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_CXX_EXTENSIONS OFF)              # -std=c++20, not -std=gnu++20
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)      # for clangd

    # -Wconversion is deliberately absent. This codebase mixes uint8_t,
    # uint16_t, int32_t and uint32_t in nearly every expression, and enabling it
    # while the arithmetic is still in flux would slow every phase with casts.
    # It goes on in Phase 7, where reaching zero is the job.
    set(RGBSEQ_WARNINGS -Wall -Wextra -Wpedantic -Wshadow)

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # Phase 0 only, and it should be deleted rather than inherited. Every
        # class is declared in full while its implementation is a stub, so clang
        # correctly reports members that nothing reads yet -- Sequence::steps_,
        # Segment::first_, Strip::buffer_ and the rest. Phase 4 is the last
        # phase that leaves any of them unread; remove this line then and see
        # what is left.
        list(APPEND RGBSEQ_WARNINGS -Wno-unused-private-field)
    endif()

    target_compile_options(rgbseq PRIVATE ${RGBSEQ_WARNINGS})
endif()

option(RGBSEQ_BUILD_TESTS "Build the host test suite" ${PROJECT_IS_TOP_LEVEL})

if(RGBSEQ_BUILD_TESTS)
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
    set(RGBSEQ_TEST_SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/tests/doctest_main.cpp
        ${CMAKE_CURRENT_LIST_DIR}/tests/umbrella_only.cpp
        ${CMAKE_CURRENT_LIST_DIR}/tests/layout.cpp)

    include(${CMAKE_CURRENT_LIST_DIR}/cmake/rgbseq_variant.cmake)

    # Five, not four. The cross product of the two behavioural macros is four,
    # and `default` is one of them -- called with no definitions at all, so that
    # config.h's #ifndef values are exercised *as* the defaults. Each of the
    # others passes only its deviations, for the same reason. The fifth is
    # `tight`: the capacities are plain constants and do not need their own
    # cross product, but one cheap variant catches anything that quietly assumes
    # 8 -- an off-by-one in the builder's capacity check, or a test with a
    # hardcoded step count.
    rgbseq_add_variant(default)
    rgbseq_add_variant(rgbw          RGBSEQ_WHITE_CHANNEL=1)
    rgbseq_add_variant(nogamma       RGBSEQ_GAMMA=0)
    rgbseq_add_variant(rgbw_nogamma  RGBSEQ_WHITE_CHANNEL=1 RGBSEQ_GAMMA=0)
    rgbseq_add_variant(tight         RGBSEQ_MAX_STEPS=3 RGBSEQ_MAX_SEGMENTS=1)
endif()
