# Toolchain file for the freestanding cross-compile check.
#
# The cheapest possible proxy for "this actually works on the MCU": compile the
# library only -- no tests, no link -- and see whether it needs a hosted libc, a
# heap, or a 64-bit word. It runs on every change, so a violation is caught
# while fixing it is one line rather than an archaeology exercise.
#
#   cmake -B build-cross -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake \
#         -DSTRIPSEQ_BUILD_TESTS=OFF
#
# Note where the flags are: in the toolchain file, which is the *consumer* side
# of the boundary. The library itself still sets none, per the ground rule.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Without this, CMake's compiler check tries to link a full executable, which
# needs a startup file and a linker script we do not have. Telling it to stop at
# a static library is the standard bare-metal answer.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_COMPILER   arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_AR           arm-none-eabi-gcc-ar)
set(CMAKE_RANLIB       arm-none-eabi-gcc-ranlib)

# cortex-m4, not the bench board's cortex-m7: with no -mfpu it is soft-float,
# which makes an accidental floating-point operation expensive and visible
# rather than free. The stricter proxy is the more useful one here.
#
# _INIT rather than a plain set(): these are defaults the cache picks up, so a
# caller can still override them from the command line.
set(CMAKE_CXX_FLAGS_INIT "-mcpu=cortex-m4 -mthumb -ffreestanding -fno-exceptions -fno-rtti -Os")
set(CMAKE_C_FLAGS_INIT   "-mcpu=cortex-m4 -mthumb -ffreestanding -Os")

# Look for programs on the host, but never search the host's headers/libraries.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM BEFORE)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
