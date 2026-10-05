# The library's source list and include directory, in one place.
#
# Shared by the standalone CMakeLists.txt and the Zephyr module's
# zephyr/CMakeLists.txt, so a new .cpp cannot be added to one build and
# forgotten in the other. That failure mode surfaces as a link error on
# hardware long after the host tests went green.
#
# Paths are absolute, built from CMAKE_CURRENT_LIST_DIR (the directory of *this*
# file, whatever includes it). Relative paths would resolve against the
# including file's directory, which is a different one in the Zephyr build.

set(RGBSEQ_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/../include)

set(RGBSEQ_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/../src/color.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/sequence.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/sequencer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/segment.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/strip.cpp)
