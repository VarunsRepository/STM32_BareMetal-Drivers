# =============================================================================
#  CMakeLists.txt - bare-metal STM32F401RE firmware
# =============================================================================
#
#  First time (picks up the toolchain file, only needed once):
#      cmake -B build -DCMAKE_TOOLCHAIN_FILE=arm-none-eabi.cmake
#
#  Every time after:
#      cmake --build build
#
#  Flash it:
#      cmake --build build --target flash
#
#  Other useful targets:
#      cmake --build build --target size     how much flash and RAM
#      cmake --build build --target bin      raw binary, for a bootloader
#      cmake --build build --target openocd  start a GDB server
#
#  Switch to Release:
#      cmake -B build -DCMAKE_BUILD_TYPE=Release
# =============================================================================

cmake_minimum_required(VERSION 3.22)

# ASM is listed alongside C because the startup file is assembly. Leave it
# out and CMake refuses to compile the .s file.
project(ecg_firmware
    LANGUAGES C ASM
    DESCRIPTION "Bare-metal STM32F401RE: clock tree, timers, ECG sampling")

# Default to a debug build. -Og rather than -O0 matters here: the driver
# headers are full of 'static inline' helpers, and at -O0 each one becomes
# a real function call instead of the single instruction it should be.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(CMAKE_C_FLAGS_DEBUG   "-Og -g3 -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_C_STANDARD 11)


# =============================================================================
#  THE PROJECT'S OWN FILES
# =============================================================================
#  Listed explicitly rather than globbed. That is deliberate: when the
#  hand-written startup code and linker script arrive alongside CubeIDE's
#  generated ones, there will be two of each, and a glob would pick one
#  arbitrarily. Naming them means always knowing which was linked - which
#  matters a great deal when the thing you are debugging IS the startup code.
#
#  Adjust these three paths to match where CubeIDE put yours.
# =============================================================================

set(STARTUP_FILE  "Startup/startup_stm32f401retx.s")
set(LINKER_SCRIPT "STM32F401RETX_FLASH.ld")

set(SOURCES
    Src/main.c
    Src/clock.c
)

foreach(f ${STARTUP_FILE} ${LINKER_SCRIPT} ${SOURCES})
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/${f}")
        message(FATAL_ERROR
            "Missing: ${f}\n"
            "Find yours with:  Get-ChildItem . -Recurse -Include *.s,*.ld\n"
            "then correct the path near the top of CMakeLists.txt.")
    endif()
endforeach()


# =============================================================================
#  BUILD
# =============================================================================
#  .elf because that is what the debugger needs: the machine code plus the
#  symbols that map it back to source lines.
# =============================================================================

add_executable(firmware.elf ${SOURCES} ${STARTUP_FILE})

target_include_directories(firmware.elf PRIVATE Inc Src)

target_compile_options(firmware.elf PRIVATE
    -Wall -Wextra
    # Put each function and variable in its own section, so the linker's
    # --gc-sections below can discard whatever nothing references.
    -ffunction-sections -fdata-sections
)

target_link_options(firmware.elf PRIVATE
    # The linker script: where flash starts (0x08000000) and how much there
    # is, where RAM starts (0x20000000) and how much, where the stack goes.
    # Mandatory on bare metal - nothing knows the memory map without it.
    -T${CMAKE_SOURCE_DIR}/${LINKER_SCRIPT}

    # Stub out printf, file I/O, exit. There is no OS to provide them, and
    # without this you get undefined references to _write, _sbrk and friends.
    --specs=nosys.specs
    # The cut-down C library, much smaller than full newlib.
    --specs=nano.specs

    -Wl,--gc-sections
    # A report of what ended up where and how big it is. Worth opening when
    # flash starts filling up, or to see what is eating RAM.
    -Wl,-Map=${CMAKE_BINARY_DIR}/firmware.map
    # Print the section sizes at the end of every link.
    -Wl,--print-memory-usage
)

# The maths library. Not needed yet; will be once the filter chain uses
# sqrtf and friends.
target_link_libraries(firmware.elf PRIVATE m)


# =============================================================================
#  SIZE
# =============================================================================
#    text = code + constants, lives in flash             (512 KB available)
#    data = initialised globals: counted TWICE - the values sit in flash and
#           are copied into RAM at boot
#    bss  = zeroed globals, RAM only, costs no flash
#
#    flash used = text + data          RAM used = data + bss
#
#  RAM is the one to watch: 96 KB, and the stack grows down into whatever is
#  left. Nothing detects a collision at runtime.
# =============================================================================

add_custom_command(TARGET firmware.elf POST_BUILD
    COMMAND ${CMAKE_SIZE} firmware.elf)

add_custom_target(size
    COMMAND ${CMAKE_SIZE} -A -x firmware.elf
    DEPENDS firmware.elf
    COMMENT "Per-section breakdown")


# =============================================================================
#  RAW BINARY
# =============================================================================
#  An .elf is a container: code plus symbols plus section metadata. A .bin is
#  just the bytes that go into flash, nothing else. The debugger wants the
#  elf; a bootloader or a raw flash write wants the bin.
# =============================================================================

add_custom_target(bin
    COMMAND ${CMAKE_OBJCOPY} -O binary firmware.elf firmware.bin
    DEPENDS firmware.elf
    COMMENT "Writing firmware.bin")


# =============================================================================
#  FLASH
# =============================================================================
#  SWD is ARM's two-wire debug protocol - clock plus bidirectional data. It
#  reaches straight into the chip's bus, so it can write flash, read RAM and
#  halt the core.
#
#  On a Nucleo the ST-Link chip on the top third of the board speaks USB to
#  the PC and SWD to the F401, with the traces already routed. One cable.
#
#  -rst resets the chip afterwards so the new firmware starts immediately.
# =============================================================================

find_program(STM32_PROGRAMMER
    NAMES STM32_Programmer_CLI
    PATHS
        "C:/ST"
        "C:/Program Files/STMicroelectronics"
        "C:/Program Files (x86)/STMicroelectronics"
        "$ENV{HOME}/st"
    PATH_SUFFIXES
        "STM32CubeProgrammer/bin"
        "STM32Cube/STM32CubeProgrammer/bin"
    DOC "ST's command-line flashing tool")

if(STM32_PROGRAMMER)
    add_custom_target(flash
        COMMAND ${STM32_PROGRAMMER} -c port=SWD -w firmware.elf -rst
        DEPENDS firmware.elf
        COMMENT "Flashing over SWD")
else()
    add_custom_target(flash
        COMMAND ${CMAKE_COMMAND} -E echo
            "STM32_Programmer_CLI not found. Pass -DSTM32_PROGRAMMER=<path>, or flash from CubeIDE."
        COMMENT "No flashing tool")
endif()


# =============================================================================
#  OPENOCD
# =============================================================================
#  The open-source alternative to ST's tool, and the usual pairing with
#  command-line GDB. Starting it leaves a server listening on :3333; then,
#  from another terminal:
#
#      arm-none-eabi-gdb build/firmware.elf
#      (gdb) target remote :3333
#      (gdb) load
#      (gdb) break main
#      (gdb) continue
#
#  Which is the same debugging CubeIDE does, with the GUI removed.
# =============================================================================

find_program(OPENOCD NAMES openocd)

if(OPENOCD)
    add_custom_target(openocd
        COMMAND ${OPENOCD} -f interface/stlink.cfg -f target/stm32f4x.cfg
        COMMENT "GDB server on :3333 - Ctrl-C to stop")
endif()


message(STATUS "")
message(STATUS "  ${PROJECT_NAME} - ${CMAKE_BUILD_TYPE}")
message(STATUS "  startup: ${STARTUP_FILE}")
message(STATUS "  linker:  ${LINKER_SCRIPT}")
message(STATUS "")
