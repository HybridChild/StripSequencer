# One (library, test binary, CTest entries) triple per build configuration.
#
#   stripseq_add_variant(default)                       # config.h's own defaults
#   stripseq_add_variant(rgbw   STRIPSEQ_WHITE_CHANNEL=1)
#   stripseq_add_variant(tight  STRIPSEQ_MAX_STEPS=6 STRIPSEQ_MAX_SEGMENTS=2)
#
# STRIPSEQ_WHITE_CHANNEL and STRIPSEQ_GAMMA change compiled behaviour rather
# than just a constant -- the first changes sizeof(Color), sizeof(Step) and
# Strip::bytes_per_pixel, the second changes the output conversion and whether a
# table is linked -- so no single compiled artifact represents both settings and
# the whole library is built once per combination.

# One translation unit per public header, holding that header's #include and
# nothing else, compiled in every variant by stripseq_add_variant() below. The
# umbrella header cannot show that each header stands on its own: it includes
# them in one order, so a header leaning on a sibling that happens to come
# before it still compiles there. Here nothing comes before it.
#
# Globbed rather than listed, unlike the sources in cmake/sources.cmake: the
# point is that a new header is covered without anyone remembering to add it,
# and CONFIGURE_DEPENDS re-runs the glob when one appears.
file(GLOB STRIPSEQ_PUBLIC_HEADERS CONFIGURE_DEPENDS ${STRIPSEQ_INCLUDE_DIR}/stripseq/*.h)
set(STRIPSEQ_HEADER_TUS "")
foreach(header IN LISTS STRIPSEQ_PUBLIC_HEADERS)
    get_filename_component(stem ${header} NAME_WE)
    set(tu ${PROJECT_BINARY_DIR}/header_alone/${stem}.cpp)
    file(CONFIGURE OUTPUT ${tu} CONTENT "#include <stripseq/${stem}.h>\n")
    list(APPEND STRIPSEQ_HEADER_TUS ${tu})
endforeach()

function(stripseq_add_variant name)
    set(lib stripseq_${name})
    set(exe tests_${name})

    add_library(${lib} STATIC ${STRIPSEQ_SOURCES})
    target_include_directories(${lib} PUBLIC ${STRIPSEQ_INCLUDE_DIR})
    target_compile_features(${lib} PUBLIC cxx_std_20)

    # PUBLIC is load-bearing: it propagates to ${exe}, so the tests cannot
    # disagree with the library about the layout of Color. They would link
    # anyway if they did -- the mangled names do not change -- so nothing
    # downstream would catch it. tests/layout.cpp is what catches it here.
    target_compile_definitions(${lib} PUBLIC ${ARGN})

    # PRIVATE: our warnings are not a consumer's problem.
    target_compile_options(${lib} PRIVATE ${STRIPSEQ_WARNINGS})

    add_executable(${exe} ${STRIPSEQ_TEST_SOURCES})
    target_link_libraries(${exe} PRIVATE ${lib} doctest::doctest)

    # src/ for the internal headers -- color_internal.h's to_bytes -- which the
    # tests exercise directly and a consumer never sees. PRIVATE, and on the
    # test target only: the library finds them beside its own sources.
    target_include_directories(${exe} PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_compile_options(${exe} PRIVATE ${STRIPSEQ_WARNINGS})

    # The same definitions again, renamed, straight onto the test target -- a
    # second path that does not go through the PUBLIC propagation above.
    # tests/layout.cpp compares the two, which is the only way the PRIVATE
    # mistake can be caught: with it, the library sees STRIPSEQ_WHITE_CHANNEL=1
    # and the tests do not, and the mismatch is invisible to compiler and
    # linker alike.
    set(expect "")
    foreach(def IN LISTS ARGN)
        string(REPLACE "STRIPSEQ_" "STRIPSEQ_EXPECT_" renamed "${def}")
        list(APPEND expect ${renamed})
    endforeach()
    target_compile_definitions(${exe} PRIVATE ${expect})

    doctest_discover_tests(${exe} TEST_PREFIX "${name}:")

    # The headers one at a time, against this variant's definitions. An object
    # library rather than more sources in ${exe}, so that
    # `cmake --build build --target headers_${name}` asks this one question.
    # Nothing links it: compiling is the whole test.
    set(headers headers_${name})
    add_library(${headers} OBJECT ${STRIPSEQ_HEADER_TUS})
    target_include_directories(${headers} PRIVATE ${STRIPSEQ_INCLUDE_DIR})
    target_compile_features(${headers} PRIVATE cxx_std_20)
    target_compile_definitions(${headers} PRIVATE ${ARGN})
    target_compile_options(${headers} PRIVATE ${STRIPSEQ_WARNINGS})
endfunction()
