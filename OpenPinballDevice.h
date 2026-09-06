// Open Pinball Device report structure
//
// A vendor-neutral HID interface for virtual pinball cabinet controllers,
// defined by mjrgh and implemented by Pinscape Pico. It exists so that pinball
// software can read a cabinet's nudge, plunger and buttons without knowing
// which board it is talking to -- the interface is deliberately not identified
// by VID/PID, but by a string descriptor naming the report structure.
//
// Spec: PinscapePico/OpenPinballDevice/OpenPinballDeviceHID.htm
//
// The wire format is explicitly little-endian and explicitly unpadded, so this
// file describes it as byte offsets rather than as a C struct: the KL25Z is
// little-endian and would very likely lay a struct out correctly, but the spec
// says not to rely on that, and building the packet by hand costs nothing.

#ifndef PINSCAPE_OPENPINBALLDEVICE_H
#define PINSCAPE_OPENPINBALLDEVICE_H

#include <stdint.h>

// Byte offsets within the report, v1.0.
#define OPD_OFS_TIMESTAMP        0   // uint64  microseconds, arbitrary origin
#define OPD_OFS_GENERIC_BUTTONS  8   // uint32  32 general-purpose buttons
#define OPD_OFS_PINBALL_BUTTONS 12   // uint32  32 pre-assigned pinball buttons
#define OPD_OFS_AX_NUDGE        16   // int16   nudge acceleration, X
#define OPD_OFS_AY_NUDGE        18   // int16   nudge acceleration, Y
#define OPD_OFS_VX_NUDGE        20   // int16   nudge velocity, X
#define OPD_OFS_VY_NUDGE        22   // int16   nudge velocity, Y
#define OPD_OFS_PLUNGER_POS     24   // int16   plunger position
#define OPD_OFS_PLUNGER_SPEED   26   // int16   plunger speed

// Fields added in 1.1, appended at the end as the spec's extension rule
// requires, so that a reader built against 1.0 simply ignores them.
//
// nudgeFullScale is what the two acceleration axes mean: full scale on those
// int16 fields corresponds to this many g. Without it the values have no unit
// at all, and the host has to ask the user -- which is what pinball software
// does today, defaulting to 1 g and being wrong by a factor of 2, 4 or 8 on a
// board configured otherwise.
//
// Proposed as mjrgh/PinscapePico#124.
#define OPD_OFS_NUDGE_FULL_SCALE 28  // uint8   full scale in g, 0 = unknown
#define OPD_OFS_FIRMWARE_VERSION 29  // uint8[3] major, minor, patch

// Size of the report, and the string identifying it. A host matches the string
// only up to the '/', treating what follows as Major.Minor, so a reader built
// against 1.0 keeps working against this one.
#define OPD_REPORT_SIZE         32
#define OPD_STRUCT_STRDESC      "OpenPinballDeviceStruct/1.1"

// Pre-assigned pinball button indices, from the spec. The mask for a button is
// (1 << index). Only the ones Pinscape can currently source are listed.
#define OPD_BTN_START            0
#define OPD_BTN_EXIT             1
#define OPD_BTN_EXTRABALL        2
#define OPD_BTN_COIN1            3
#define OPD_BTN_COIN2            4
#define OPD_BTN_COIN3            5
#define OPD_BTN_COIN4            6
#define OPD_BTN_LAUNCH           7
#define OPD_BTN_FIRE             8
#define OPD_BTN_LEFTFLIPPER      9
#define OPD_BTN_RIGHTFLIPPER    10
#define OPD_BTN_LEFTFLIPPER2    11
#define OPD_BTN_RIGHTFLIPPER2   12
#define OPD_BTN_LEFTMAGNASAVE   13
#define OPD_BTN_RIGHTMAGNASAVE  14
#define OPD_BTN_TILT            15
#define OPD_BTN_COINDOOR        16
#define OPD_BTN_SERVICECANCEL   17
#define OPD_BTN_SERVICEDOWN     18
#define OPD_BTN_SERVICEUP       19
#define OPD_BTN_SERVICEENTER    20
#define OPD_BTN_LEFTNUDGE       21
#define OPD_BTN_FWDNUDGE        22
#define OPD_BTN_RIGHTNUDGE      23
#define OPD_BTN_VOLUMEUP        24
#define OPD_BTN_VOLUMEDOWN      25

// Little-endian writers. The report is assembled into a byte buffer, so these
// take a moving pointer and advance it.
inline void opdPutU16(uint8_t* &p, uint16_t v) {
    *p++ = static_cast<uint8_t>(v & 0xFF);
    *p++ = static_cast<uint8_t>((v >> 8) & 0xFF);
}
inline void opdPutI16(uint8_t* &p, int16_t v) { opdPutU16(p, static_cast<uint16_t>(v)); }
inline void opdPutU32(uint8_t* &p, uint32_t v) {
    opdPutU16(p, static_cast<uint16_t>(v & 0xFFFF));
    opdPutU16(p, static_cast<uint16_t>((v >> 16) & 0xFFFF));
}
inline void opdPutU64(uint8_t* &p, uint64_t v) {
    opdPutU32(p, static_cast<uint32_t>(v & 0xFFFFFFFFu));
    opdPutU32(p, static_cast<uint32_t>((v >> 32) & 0xFFFFFFFFu));
}

#endif
