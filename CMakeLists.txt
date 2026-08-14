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

set(MBED_APP_JSON_PATH mbed_app.json5)

# The stock KL25Z target does not enable USB device support; see the comment in
# custom_targets.json5. Build against a derived target that does.
set(CUSTOM_TARGETS_JSON_PATH custom_targets/custom_targets.json5)

include(mbed-os/tools/cmake/mbed_toolchain_setup.cmake)

project(Pinscape_Controller
        LANGUAGES C CXX ASM)

include(mbed_project_setup)

add_subdirectory(custom_targets)
add_subdirectory(mbed-os)

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

mbed_set_post_build(${PROJECT_NAME})
