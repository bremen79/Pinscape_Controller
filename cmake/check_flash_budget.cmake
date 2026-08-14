# Fail the build if the image would collide with the saved configuration.
#
# The firmware keeps its settings in the last sectors of flash, at
# flashSize() - ceil(sizeof(NVM)/SECTOR_SIZE) * SECTOR_SIZE (see
# configFlashAddr() in main.cpp). The linker knows nothing about that: its
# FLASH region runs to the end of the device, so an image that grows into the
# config area links without a word of complaint and then corrupts itself the
# first time the board saves its settings.
#
# That is not hypothetical. The image reached 130492 bytes during the port,
# 1468 past the reserved area, and nothing flagged it.

if(NOT DEFINED IMAGE OR NOT DEFINED FLASH_SIZE OR NOT DEFINED RESERVED)
    message(FATAL_ERROR "check_flash_budget.cmake: IMAGE, FLASH_SIZE and RESERVED are required")
endif()

file(SIZE "${IMAGE}" image_size)
math(EXPR limit "${FLASH_SIZE} - ${RESERVED}")

if(image_size GREATER limit)
    math(EXPR over "${image_size} - ${limit}")
    message(FATAL_ERROR
        "Image is ${image_size} bytes, ${over} past the ${limit}-byte limit.\n"
        "The last ${RESERVED} bytes of flash hold the saved configuration; an "
        "image that reaches into them will be overwritten when the board saves "
        "its settings. Free up space rather than raising this limit.")
endif()

math(EXPR headroom "${limit} - ${image_size}")
math(EXPR percent "${image_size} * 100 / ${FLASH_SIZE}")
message(STATUS "flash: ${image_size} bytes, ${percent}% of ${FLASH_SIZE}, ${headroom} free below the config area")
