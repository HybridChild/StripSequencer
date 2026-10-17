# The footprint build: the library, configured from the command line, linked
# into tools/footprint/app.cpp for a bare-metal Cortex-M. footprint.sh drives
# it once per core and configuration; see that script for what it reads back.
#
#   cmake -S tools/footprint -B build-footprint/m4 -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake \
#         [-DSTRIPSEQ_CROSS_CPU=cortex-m0] \
#         [-DSTRIPSEQ_WHITE_CHANNEL=1] [-DSTRIPSEQ_GAMMA=0]
#
# A project of its own rather than an option in the top-level CMakeLists.txt,
# because it is a *consumer* of the library: it takes the library in through
# add_subdirectory(), exactly as an application would, and chooses its
# configuration and flags from outside.
cmake_minimum_required(VERSION 3.21)

project(stripseq_footprint LANGUAGES CXX)

set(STRIPSEQ_WHITE_CHANNEL 0 CACHE STRING "STRIPSEQ_WHITE_CHANNEL for this build")
set(STRIPSEQ_GAMMA         1 CACHE STRING "STRIPSEQ_GAMMA for this build")

# Each function and object in a section of its own, so --gc-sections can drop
# whatever the application never reaches; every embedded build does this,
# Zephyr included, and without it the figures would be whole object files.
# Directory scope, set before add_subdirectory(), so the library's targets
# inherit it: these are the consumer's flags, and the library sets none.
add_compile_options(-ffunction-sections -fdata-sections)

# The source tree is not below this one, so the binary directory has to be
# named. The library's tests stay off because it is not the top-level project.
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. stripseq)

# PUBLIC, as the README asks of every consumer: the application and the library
# must agree on the size of a Color.
target_compile_definitions(stripseq PUBLIC
    STRIPSEQ_WHITE_CHANNEL=${STRIPSEQ_WHITE_CHANNEL}
    STRIPSEQ_GAMMA=${STRIPSEQ_GAMMA})

add_executable(footprint app.cpp)
target_link_libraries(footprint PRIVATE StripSequencer::stripseq)

# -nostartfiles: there is no startup code, and app.cpp's _start is the root
# the linker keeps everything from. nano.specs links newlib-nano, which is
# where memcpy and memset come from; nothing else in libc is reached. The map
# is not read by footprint.sh, but it is the answer to "why is this symbol
# here", which the symbol table alone cannot give.
target_link_options(footprint PRIVATE
    -nostartfiles
    --specs=nano.specs
    -Wl,--gc-sections
    -Wl,-Map=$<TARGET_FILE:footprint>.map)
