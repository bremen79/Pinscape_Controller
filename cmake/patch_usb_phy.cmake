# Rebuild the Kinetis USB driver from a patched copy, generated at configure time.
#
# Two things are wrong with the stock driver, and neither can be fixed from
# outside the file:
#
#  1. USBPhy_Kinetis.cpp uses NULL and memset without including <stddef.h> or
#     <string.h>, so it does not compile at all. Nothing noticed because no
#     target builds it: the KL25Z is the only board whose USB support routes
#     here, and its device_has has never listed USBDEVICE (see
#     custom_targets.json5). KL43Z and K82F are MCUXpresso targets and use a
#     different driver entirely.
#
#  2. USBEndpoints_Kinetis.h sets MAX_PACKET_SIZE_EP3 to 1023 -- a full-size
#     isochronous packet -- and the driver declares ep3_buffer[2][that]
#     unconditionally. That is 2046 bytes of static RAM, an eighth of this
#     part's 16 KB, for an endpoint reserved to isochronous transfers that a
#     HID device never allocates. It goes unnoticed on the boards that do build
#     a USB driver because they have 32 KB and 256 KB.
#
# Neither is reachable through Mbed configuration: of the 355 options this
# build resolves, USB has only DEVICE_USBDEVICE, which is all or nothing. And
# the header cannot be shadowed with an include path, because #include "..."
# searches the including file's own directory first -- the driver will always
# find the copy sitting next to it.
#
# So copy both files somewhere else and patch them there. The copy's own
# neighbour is then the patched header, which is exactly what we want. The
# sources stay Mbed CE's: nothing is vendored into this repository, and a
# newer Mbed CE is picked up automatically.
#
# Both patches are conditional, so this quietly does nothing once upstream
# fixes either one.

function(pinscape_patch_usb_phy)
    set(src "${MBED_ROOT}/targets/TARGET_Freescale")
    set(out "${CMAKE_BINARY_DIR}/mbed_patched")
    file(MAKE_DIRECTORY "${out}")

    # -- the header: shrink the isochronous packet buffer -------------------
    file(READ "${src}/USBEndpoints_Kinetis.h" header)
    if(header MATCHES "#define[ \t]+MAX_PACKET_SIZE_EP3[ \t]+\\(1023\\)")
        string(REPLACE
            "#define MAX_PACKET_SIZE_EP3  (1023)"
            "#define MAX_PACKET_SIZE_EP3  (64)   /* patched: see cmake/patch_usb_phy.cmake */"
            header "${header}")
        message(STATUS "USB: isochronous endpoint buffer reduced, 1918 bytes of RAM recovered")
    endif()
    file(WRITE "${out}/USBEndpoints_Kinetis.h" "${header}")

    # -- the driver: add the includes it is missing -------------------------
    file(READ "${src}/USBPhy_Kinetis.cpp" driver)
    if(NOT driver MATCHES "#include <stddef.h>")
        string(REPLACE "#include \"USBPhyHw.h\""
                       "#include <stddef.h>\n#include <string.h>\n#include \"USBPhyHw.h\""
                       driver "${driver}")
        message(STATUS "USB: added the missing <stddef.h>/<string.h> to the Kinetis driver")
    endif()
    # -- the driver: let endpoint 3 carry interrupt transfers ---------------
    #
    # The table gives EP1 and EP2 to bulk/interrupt and reserves EP3 to
    # isochronous, so a device needing a third interrupt IN endpoint has
    # nowhere to put it. The hardware has no such restriction; the table is
    # how Mbed chose to divide it. Adding the bulk/interrupt attribute rather
    # than replacing the isochronous one leaves EP3 usable for its original
    # purpose as well.
    if(driver MATCHES "\\{USB_EP_ATTR_ALLOW_ISO \\| USB_EP_ATTR_DIR_IN_AND_OUT")
        string(REPLACE
            "{USB_EP_ATTR_ALLOW_ISO | USB_EP_ATTR_DIR_IN_AND_OUT"
            "{USB_EP_ATTR_ALLOW_ISO | ALLOW_BULK_OR_INT_ENDPOINTS | USB_EP_ATTR_DIR_IN_AND_OUT"
            driver "${driver}")
        message(STATUS "USB: endpoint 3 opened to interrupt transfers")
    endif()

    file(WRITE "${out}/USBPhy_Kinetis.cpp" "${driver}")

    # -- build the copy instead of the original -----------------------------
    #
    # HEADER_FILE_ONLY drops a source from a target without touching the
    # library's own CMakeLists. TARGET_DIRECTORY names the target whose
    # property to set, since the file belongs to another directory.
    set_source_files_properties("${src}/USBPhy_Kinetis.cpp"
        TARGET_DIRECTORY mbed-baremetal
        PROPERTIES HEADER_FILE_ONLY TRUE)

    target_sources(mbed-baremetal PRIVATE "${out}/USBPhy_Kinetis.cpp")

    # USBPhyHw.h was a neighbour of the original and is not of the copy.
    # USBEndpoints_Kinetis.h is deliberately not reached this way: the copy
    # finds the patched one beside it first.
    target_include_directories(mbed-baremetal PRIVATE "${src}")
endfunction()
