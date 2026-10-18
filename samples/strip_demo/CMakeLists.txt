cmake_minimum_required(VERSION 3.20.0)

# This repository is the module: its zephyr/module.yml is what makes
# CONFIG_STRIPSEQ exist. Naming it here lets the sample build against any
# workspace without the workspace's west.yml knowing about this repo.
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../..)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(strip_demo)

target_sources(app PRIVATE src/main.cpp)
