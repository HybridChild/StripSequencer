# The module's whole job: compile the core's own source list and translate
# Kconfig into the four macros config.h already reads. config.h guards each
# macro with #ifndef, so nothing in include/ or src/ knows it is inside Zephyr.
#
# Zephyr owns the compiler, the standard and the flags here; the C++20
# requirement is stated in Kconfig as a dependency rather than set as a flag.
if(CONFIG_STRIPSEQ)
  include(${CMAKE_CURRENT_LIST_DIR}/../cmake/sources.cmake)
  zephyr_library()
  zephyr_library_sources(${STRIPSEQ_SOURCES})
  zephyr_include_directories(${STRIPSEQ_INCLUDE_DIR})

  # zephyr_compile_definitions, not zephyr_library_compile_definitions: the
  # application includes the same headers, and an application that disagreed
  # with the library about STRIPSEQ_WHITE_CHANNEL would link cleanly against a
  # Color of the wrong size -- see cmake/stripseq_variant.cmake.
  zephyr_compile_definitions(
    STRIPSEQ_MAX_STEPS=${CONFIG_STRIPSEQ_MAX_STEPS}
    STRIPSEQ_MAX_SEGMENTS=${CONFIG_STRIPSEQ_MAX_SEGMENTS}
    STRIPSEQ_WHITE_CHANNEL=$<BOOL:${CONFIG_STRIPSEQ_WHITE_CHANNEL}>
    STRIPSEQ_GAMMA=$<BOOL:${CONFIG_STRIPSEQ_GAMMA}>)
endif()
