#
# Pinscape Controller, built against Mbed OS Community Edition.
#
# The original project was built by the mbed online compiler, which shut down
# with the rest of mbed in July 2026. All it left behind was `mbed.bld`, a
# one-line pointer to a Mercurial revision on a server that no longer answers —
# so the firmware could not be rebuilt at all. This file replaces that pointer
# with a local build: GCC ARM and CMake, against the community fork of Mbed OS,
# which still supports the KL25Z.
#
cmake_minimum_required(VERSION 3.19)
cmake_policy(VERSION 3.19...3.22)

# Mbed CE defaults to the Develop profile (-Og -g3), which does not fit: it
# overflows the flash region by 3732 bytes. There is no room on this part for
# anything but -Os, so choose it rather than letting the build fail on a linker
# error that says nothing about why.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build profile" FORCE)
endif()

# This firmware runs on one board and one board only, so there is no reason to
# make every build spell out -DMBED_TARGET. The Pinscape target derives from
# KL25Z; see custom_targets/custom_targets.json5.
if(NOT MBED_TARGET)
    set(MBED_TARGET PINSCAPE_KL25Z CACHE STRING "Mbed target")
endif()
if(NOT MBED_TOOLCHAIN)
    set(MBED_TOOLCHAIN GCC_ARM CACHE STRING "Mbed toolchain")
endif()

# Configure the console before Mbed resolves its library settings. Release
# builds must not initialize the UART on pins a saved cabinet config can use.
option(PINSCAPE_CRASH_DIAGNOSTICS "Print fatal error details on the OpenSDA serial port" OFF)
option(PINSCAPE_NUDGE_DIAGNOSTICS "Log sensor data and completed USB transfers once per second" OFF)
if(PINSCAPE_NUDGE_DIAGNOSTICS AND NOT PINSCAPE_CRASH_DIAGNOSTICS)
    message(FATAL_ERROR "Nudge diagnostics require PINSCAPE_CRASH_DIAGNOSTICS=ON for the guarded UART")
endif()
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/mbed_app.json5" pinscape_app_config)
if(PINSCAPE_CRASH_DIAGNOSTICS)
    string(REPLACE "\"target.console-uart\": false" "\"target.console-uart\": true"
        pinscape_app_config "${pinscape_app_config}")
endif()
set(MBED_APP_JSON_PATH "${CMAKE_CURRENT_BINARY_DIR}/pinscape_mbed_app.json5")
file(CONFIGURE OUTPUT "${MBED_APP_JSON_PATH}" CONTENT "${pinscape_app_config}" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/mbed_app.json5")

# The stock KL25Z target does not enable USB device support; see the comment in
# custom_targets.json5. Build against a derived target that does.
set(CUSTOM_TARGETS_JSON_PATH custom_targets/custom_targets.json5)

include(mbed-os/tools/cmake/mbed_toolchain_setup.cmake)

project(Pinscape_Controller
        LANGUAGES C CXX ASM)

include(mbed_project_setup)

# Keep compiler stack reports alongside the objects, including LTO output.
# Static RAM totals alone cannot detect a stack/heap collision.
add_compile_options(-fstack-usage)
add_link_options(-fstack-usage)

add_subdirectory(custom_targets)
add_subdirectory(mbed-os)

# The Kinetis USB driver does not compile as shipped, and reserves 2 KB of RAM
# for an endpoint this device never uses. Both are fixed on a generated copy;
# see cmake/patch_usb_phy.cmake for why it cannot be done any other way.
set(MBED_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/mbed-os)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_usb_phy.cmake)
pinscape_patch_usb_phy()

# The firmware, as one executable. The original had no notion of libraries:
# every driver lives in its own folder and is compiled straight into the image,
# so the layout here follows the source rather than imposing structure on it.
add_executable(${PROJECT_NAME}
    main.cpp

    # Sensors and I/O drivers
    AltAnalogIn/AltAnalogIn_KL25Z.cpp
    BitBangI2C/BitBangI2C.cpp
    CRC32/crc32.cpp
    FastInterruptIn/FastInterruptIn.cpp
    FreescaleIAP/FreescaleIAP.cpp
    MMA8451Q/MMA8451Q.cpp
    NewMalloc/NewMalloc.cpp
    NewPwm/NewPwm.cpp
    SimpleDMA/SimpleDMA_common.cpp
    SimpleDMA/SimpleDMA_KL25_46.cpp
    TinyDigitalIn/TinyDigitalIn.cpp
    VCNL4010/VCNL4010.cpp
    VL6180X/VL6180X.cpp

    # Plunger sensors
    Plunger/potSensor.cpp

    # Infrared remote control
    IRRemote/IRCommand.cpp
    IRRemote/IRProtocols.cpp
    IRRemote/IRPwmOut.cpp
    IRRemote/IRReceiver.cpp

    # USB
    USBJoystick/USBJoystick.cpp

    # Assembler, translated from ARM Compiler syntax to GNU as. The .s files
    # next to these are the originals, kept for comparison.
    FreescaleIAP/IAP.S
    Plunger/edgeSensorScan2.S

    # Stubs for what mbed 2 had and Mbed CE does not.
    mbed_ce_compat.cpp
)

# Every driver expects to include its neighbours by bare filename, the way the
# online compiler flattened the whole project into one directory.
target_include_directories(${PROJECT_NAME} PRIVATE
    .
    74HC595
    AEAT6012
    AltAnalogIn
    BitBangI2C
    CRC32
    FastInterruptIn
    FreescaleIAP
    IRRemote
    MMA8451Q
    NewMalloc
    NewPwm
    Plunger
    SimpleDMA
    TCD1103
    TLC59116
    TLC5940
    TSL14xx
    TinyDigitalIn
    USBJoystick
    VCNL4010
    VL6180X
)

# USBJoystick derives from USBHID, which lives in the optional USB driver
# library rather than in the core. Its headers are reached as <usb/USBHID.h>.
target_link_libraries(${PROJECT_NAME} mbed-usb)

if("MBED_CONF_TARGET_APPLICATION_PROFILE=bare-metal" IN_LIST MBED_CONFIG_DEFINITIONS)
    target_link_libraries(${PROJECT_NAME} mbed-baremetal)
else()
    target_link_libraries(${PROJECT_NAME} mbed-os)
endif()

# Note: mbed_Fixes/ is deliberately left out. Its two files worked around a
# ticker bug that upstream fixed in 2019 — compiling them over a corrected
# library would put the old behaviour back.

# Route every allocation through the pool allocator, the way $Sub$$malloc did
# under ARM Compiler. new and delete come from Mbed OS now and go through
# malloc and free, so wrapping those two covers OpNew.cpp, which is no longer
# built: its operator new called xmalloc and its operator delete did nothing,
# which is exactly what these wrappers do.
target_link_options(${PROJECT_NAME} PRIVATE -Wl,--wrap=malloc -Wl,--wrap=free)

# Link-time optimisation, on trial.
target_compile_options(${PROJECT_NAME} PRIVATE -flto)
target_link_options(${PROJECT_NAME} PRIVATE -flto)

# Release builds suppress Mbed's normal error printout. Keep a separate,
# size-optimized diagnostic image available for failures reported by LED only.
if(PINSCAPE_CRASH_DIAGNOSTICS)
    target_sources(${PROJECT_NAME} PRIVATE CrashDiagnostics.cpp)
    target_compile_definitions(${PROJECT_NAME} PRIVATE PINSCAPE_CRASH_DIAGNOSTICS=1)
endif()

if(PINSCAPE_NUDGE_DIAGNOSTICS)
    target_compile_definitions(${PROJECT_NAME} PRIVATE PINSCAPE_NUDGE_DIAGNOSTICS=1)
endif()

mbed_set_post_build(${PROJECT_NAME})

# Rewrite the image without .heap.
#
# The KL25Z linker script declares `.heap :` where every other Mbed target
# declares `.heap (NOLOAD) :`, so the section is marked LOAD and given a load
# address past the end of the program. objcopy then dutifully writes it out:
# 13468 bytes of zeroes that pushed the .bin to 140288, past the 128 KB of
# flash on the part, for a firmware that actually ends at 126820. Flashing that
# image would run off the end of the device.
#
# Only the output is corrected here — the symbols the heap allocator uses
# (__end__, __HeapLimit) come from the linker script and are untouched.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary --remove-section=.heap
            $<TARGET_FILE:${PROJECT_NAME}> $<TARGET_FILE_DIR:${PROJECT_NAME}>/${PROJECT_NAME}.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex --remove-section=.heap
            $<TARGET_FILE:${PROJECT_NAME}> $<TARGET_FILE_DIR:${PROJECT_NAME}>/${PROJECT_NAME}.hex
    VERBATIM
    COMMENT "Removing the non-NOLOAD .heap section from the image")

# The saved configuration lives in the top sectors of flash, where the linker
# will happily place code instead. sizeof(NVM) is 1212, so it takes two 1 KB
# sectors; keep those free.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND}
            -DIMAGE=$<TARGET_FILE_DIR:${PROJECT_NAME}>/${PROJECT_NAME}.bin
            -DFLASH_SIZE=131072 -DRESERVED=2048
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_flash_budget.cmake
    VERBATIM)
