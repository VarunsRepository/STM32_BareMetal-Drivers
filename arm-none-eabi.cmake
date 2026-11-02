# =============================================================================
#  arm-none-eabi.cmake - toolchain file for bare-metal ARM Cortex-M
# =============================================================================
#
#  A "toolchain file" tells CMake it is CROSS-compiling: building for a
#  machine that is not this one. CMake reads it before anything else, so it
#  must be passed on the very first configure:
#
#      cmake -B build -DCMAKE_TOOLCHAIN_FILE=arm-none-eabi.cmake
#
#  After that it is cached, and plain `cmake --build build` remembers.
#
#  This file knows nothing about the project - only about the compiler and
#  the fact that there is no operating system. That separation is the point:
#  the same file works for any Cortex-M project.
# =============================================================================


# ---- Tell CMake there is no OS ----
#
# "Generic" is CMake's word for a bare-metal target. This is what stops it
# assuming it can run what it builds, look for system libraries, or use
# anything from the host.
set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)


# ---- THE ONE THAT TRIPS EVERYONE UP ----
#
# By default CMake validates a compiler by compiling AND LINKING a small
# test program. On bare metal that link always fails: there is no startup
# code and no linker script at that point, so there is no _start, no vector
# table, and no idea where flash is. CMake then reports "compiler is broken"
# when it is perfectly fine.
#
# Telling it to build a static library instead means it compiles the test
# but never links it, so the check passes.
#
# This single line is the most common reason a first bare-metal CMake
# attempt fails, and knowing it is the tell that someone has actually done
# this rather than copied a working file.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)


# ---- Which compiler ----
#
# arm-none-eabi-gcc specifically. The "none" means no operating system.
# A toolchain called arm-linux-gnueabihf is a DIFFERENT target - it builds
# for ARM chips running Linux, with an MMU and a kernel underneath, and
# cannot produce code for a Cortex-M at all.
set(CMAKE_C_COMPILER   arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)

set(CMAKE_OBJCOPY arm-none-eabi-objcopy CACHE FILEPATH "")
set(CMAKE_SIZE    arm-none-eabi-size    CACHE FILEPATH "")


# ---- Do not look on the host machine ----
#
# Without this, find_library() and friends would happily hand back x86
# libraries from the host system, which would then fail to link in
# confusing ways.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM BEFORE)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)


# ---- What the chip is ----
#
# These must be identical for every compile AND for the link. Mismatch them
# and the linker complains that one object "uses VFP register arguments"
# while another does not - it has noticed half the binary disagrees about
# how floats are passed.
#
#   cortex-m4       the core, so GCC emits the right instructions
#   thumb           Thumb-2 instruction set; Cortex-M runs nothing else
#   fpv4-sp-d16     the hardware FPU: SINGLE PRECISION ONLY. 'float' runs
#                   in hardware at about one cycle; 'double' silently falls
#                   back to software emulation, roughly 10x slower. Worth
#                   remembering once filtering starts.
#   float-abi=hard  pass floats in FPU registers rather than integer ones
set(CPU_FLAGS "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard")

set(CMAKE_C_FLAGS_INIT   "${CPU_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${CPU_FLAGS}")
set(CMAKE_ASM_FLAGS_INIT "${CPU_FLAGS}")
