cmake_minimum_required(VERSION 3.20.0)

# As in strip_demo: this repository is the module, named here so the sample
# builds against any workspace without its west.yml knowing about this repo.
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../..)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(showcase)

target_sources(app PRIVATE src/main.cpp)

# The adapter between the library's buffer and the led_strip driver, shared
# with the other samples that drive the strip.
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)
