/* USBJoystick.h */
/* USB device example: Joystick*/
/* Copyright (c) 2011 ARM Limited. All rights reserved. */
/* Modified Mouse code for Joystick - WH 2012 */
 
#ifndef USBJOYSTICK_H
#define USBJOYSTICK_H
 
#include "USBHID.h"
#include "usb_phy_api.h"
#include "circbuf.h"

// Bufferd incoming LedWiz message structure
struct LedWizMsg
{
    uint8_t data[8];
};

// interface IDs
const uint8_t IFC_ID_JS = 0;        // joystick + LedWiz interface
const uint8_t IFC_ID_KB = 1;        // keyboard interface

// keyboard interface report IDs 
const uint8_t REPORT_ID_KB = 1;
const uint8_t REPORT_ID_MEDIA = 2;

/* Common usage */
enum JOY_BUTTON {
     JOY_B0 = 0x0001,
     JOY_B1 = 0x0002,
     JOY_B2 = 0x0004,
     JOY_B3 = 0x0008,
     JOY_B4 = 0x0010,
     JOY_B5 = 0x0020,
     JOY_B6 = 0x0040,
     JOY_B7 = 0x0080,
     JOY_B8 = 0x0100,
     JOY_B9 = 0x0200,
     JOY_B10 = 0x0400,
     JOY_B11 = 0x0800,
     JOY_B12 = 0x1000,
     JOY_B13 = 0x2000,
     JOY_B14 = 0x4000,
     JOY_B15 = 0x8000
};
 
/**
 *
 * USBJoystick example
 * @code
 * #include "mbed.h"
 * #include "USBJoystick.h"
 *
 * USBJoystick joystick;
 *
 * int main(void)
 * {
 *   while (1)
 *   {
 *      joystick.move(20, 0);
 *      wait(0.5);
 *   }
 * }
 *
 * @endcode
 *
 *
 * @code
 * #include "mbed.h"
 * #include "USBJoystick.h"
 * #include <math.h>
 *
 * USBJoystick joystick;
 *
 * int main(void)
 * {   
 *   while (1) {
 *       // Basic Joystick
 *       joystick.update(tx, y, z, buttonBits);
 *       wait(0.001);
 *   }
 * }
 * @endcode
 */
 
 
// Maximum report sizes. Mbed OS 6 hands read_start() a buffer of the
// caller's own, so the class needs these where its members are declared.
const int MAX_REPORT_JS_RX = 8;
const int MAX_REPORT_KB_TX = 8;
const int MAX_REPORT_KB_RX = 4;

class USBJoystick: public USBHID 
{
public:
    // Length of our joystick reports.  Important: This must be kept in sync 
    // with the actual joystick report format sent in update().
    static const int reportLen = 22;
    
    // Joystick axis report format
    static const int AXIS_FORMAT_XYZ        = 0;    // nudge on X/Y, plunger on Z
    static const int AXIS_FORMAT_RXRYRZ     = 1;    // nudge on Rx/Ry, plunger on Rz
    static const int AXIS_FORMAT_XYZ_RXRYRZ = 2;    // accelerations on X/Y, plunger pos on Z, velocities on Rx/Ry, plunger speed on Rz, plunger unprocessed on Slider1

    /**
     *   Constructor
     *
     * @param vendor_id Your vendor_id (default: 0x1234)
     * @param product_id Your product_id (default: 0x0002)
     * @param product_release Your product_release (default: 0x0001)
     * @param waitforConnect don't return until the connection is established
     * @param enableJoystick enable the joystick interface (if false, uses the OUT-only LedWiz-style interface)
     * @param axisFormat an AXIS_FORMAT_xxx value specifying the joystick axis report format
     * @param useKB enable the USB keyboard reporting interface
     */
    USBJoystick(uint16_t vendor_id, uint16_t product_id, uint16_t product_release,
        int waitForConnect, bool enableJoystick, int axisFormat, bool useKB)
        // Mbed OS 6 reordered these: the first parameter of the simple
        // constructor is connect_blocking, not the report length, so the mbed 2
        // argument list silently mapped 16 onto a bool, the vendor id onto an
        // 8-bit report length, and shifted the ids along by one.
        //
        // A derived class must use the USBPhy form regardless.  The other one
        // connects during the base constructor, before this class's vtable
        // exists, so the device would answer descriptor requests with USBHID's
        // own -- not the joystick/LedWiz ones.  We call connect() below, once
        // the object is whole.
        : USBHID(get_usb_phy(), 16, 64, vendor_id, product_id, product_release)
    { 
        _init();
        this->useKB = useKB;
        this->enableJoystick = enableJoystick;
        this->axisFormat = axisFormat;
        // Mbed OS 6 dropped the blocking flag; state is reported by callback.
        (void)waitForConnect;
        connect();
    };

    /* read a report from the LedWiz buffer */
    bool readLedWizMsg(LedWizMsg &msg)
    {
        return lwbuf.read(msg);
    }
     
    /* get the idle time settings, in milliseconds */
    uint32_t getKbIdle() const { return kbIdleTime * 4UL; }
    uint32_t getMediaIdle() const { return mediaIdleTime * 4UL; }
     

    /**
     * Send a keyboard report.  The argument gives the key state, in the standard
     * 6KRO USB keyboard report format: byte 0 is the modifier key bit mask, byte 1
     * is reserved (must be 0), and bytes 2-6 are the currently pressed keys, as
     * USB key codes.
     */
    bool kbUpdate(uint8_t data[8]);
     
    /**
     * Send a media key update.  The argument gives the bit mask of media keys
     * currently pressed.  See the HID report descriptor for the order of bits.
     */
    bool mediaUpdate(uint8_t data);

    // HID idle periods repeat the current state; this is not an event queue.
    bool kbIdleExpired() const;
    bool mediaIdleExpired() const;
     
    /**
     * Update the joystick status
     *
     * @param x x-axis position
     * @param y y-axis position
     * @param z z-axis position
     * @param buttons buttons state, as a bit mask (combination with '|' of JOY_Bn values)
     * @returns true if there is no error, false otherwise
     */
    bool update(int16_t x, int16_t y, int16_t z, int16_t z0, int16_t vx, int16_t vy, int16_t vz, uint32_t buttons, uint16_t status);
     
    /**
     * Update just the status
     */
    bool updateStatus(uint32_t stat);
     
    /**
     * Write the plunger status report header.
     *
     * Note that we automatically add the "calibration mode" bit to the flags,
     * so the caller doesn't have to include this.  The caller only has to
     * include the sensor-specific flags.
     *
     * @param npix number of pixels in the sensor (0 for non-imaging sensors)
     * @param pos the decoded plunger position, or -1 if none detected
     * @param flags (see USBProtocol.h, message type 2A, "byte 7" bit flags)
     * @param avgScanTime average sensor scan time in microseconds
     * @param processingTime time in microseconds to process the current frame
     */
    bool sendPlungerStatus(int npix, int pos, int flags,
        uint32_t avgScanTime, uint32_t processingTime, int16_t speed);
        
    /**
      * Send a secondary plunger status report header.
      *
      * @param nativeScale upper bound of the sensor's native reading scale
      * @param jitterLo low end of jitter filter window (in sensor native scale units)
      * @param jitterHi high end of jitter filter window
      * @param rawPos raw position reading, before applying jitter filter
      * @param axcTime auto-exposure time in microseconds
      */
    bool sendPlungerStatus2(
        int nativeScale, int jitterLo, int jitterHi, int rawPos, int axcTime);
        
    /**
     * Send a barcode plunger status report header.
     *
     * @param nbits number of bits in bar code
     * @param codetype bar code type (1=Gray code/Manchester bit coding)
     * @param pixofs pixel offset of first bit
     * @param raw raw bar code bits
     * @param mask mask of successfully read bar code bits
     */
    bool sendPlungerStatusBarcode(
        int nbits, int codetype, int startOfs, int pixPerBit, int raw, int mask);
        
    /**
     * Send a quadrature sensor status report header.
     *
     * @param chA channel "A" reading
     * @param chB channel "B" reading
     */
    bool sendPlungerStatusQuadrature(int chA, int chB);

    /**
     * Send an Open Pinball Device report.
     *
     * OPD is a vendor-neutral interface for pinball cabinet controls: nudge,
     * plunger and buttons, in a layout fixed by its specification rather than
     * by us. Pinball software reading it needs to know nothing about this
     * board -- the spec deliberately refuses even VID/PID matching.
     *
     * @param genericButtons  32 general-purpose buttons, one bit each
     * @param pinballButtons  32 pre-assigned pinball buttons (OPD_BTN_xxx)
     * @param ax,ay           nudge acceleration
     * @param vx,vy           nudge velocity
     * @param plungerPos      plunger position, +32767 = fully retracted
     * @param plungerSpeed    plunger speed
     */
    bool sendOPD(uint32_t genericButtons, uint32_t pinballButtons,
                 int16_t ax, int16_t ay, int16_t vx, int16_t vy,
                 int16_t plungerPos, int16_t plungerSpeed);

    /** Is the Open Pinball Device interface present in this configuration? */
    bool opdEnabled() const { return useKB; }
    
    /**
     * Send a VCNL4010 sensor extra status report
     *
     * @param proxCount raw proximity count reading from the sensor
     */
     bool sendPlungerStatusVCNL4010(int filteredProxCount, int rawProxCount);
    
    /**
     * Write an exposure report.  We'll fill out a report with as many pixels as
     * will fit in the packet, send the report, and update the index to the next
     * pixel to send.  The caller should call this repeatedly to send reports for
     * all pixels.
     *
     * @param idx current index in pixel array, updated to point to next pixel to send
     * @param npix number of pixels in the overall array
     * @param pix pixel array
     */
    bool sendPlungerPix(int &idx, int npix, const uint8_t *pix);
     
    /**
     * Write a configuration report.
     *
     * @param numOutputs the number of configured output channels
     * @param unitNo the device unit number
     * @param plungerZero plunger zero calibration point
     * @param plungerMax plunger max calibration point
     * @param plungerRlsTime measured plunger release time, in milliseconds
     * @param configured true if a configuration has been saved to flash from the host
     * @param sbxpbx true if this firmware version supports SBX/PBX protocol extensions
     * @param newAccelFeatures true if this firmware version supports the new accelerometer
     *        features (adjustable dynamic range, adjustable auto-centering mode time,
     *        auto-centering mode on/off)
     * @param flashStatusFeature true if this firmware version supports the flash write
     *        success flags in the status bits
     * @param reportTimingFeatures true if this firmware version supports configurable
     *        joystick report timing
     * @param chimeLogicFeature true if this firmware version supports Chime Logic
     * @param velocityFeatures true if this firmware version supports accelerometer velocity
     *        integration, plunger speed reporting, and X/Y/Z+RX/RY/RZ joystick reporting
     * @param freeHeapBytes number of free bytes in the malloc heap
     */
    bool reportConfig(int numOutputs, int unitNo, 
        int plungerZero, int plungerMax, int plunterRlsTime, 
        bool configured, bool sbxpbx, bool newAccelFeatures, 
        bool flashStatusFeature, bool reportTimingFeatures,
        bool chimeLogicFeature, bool velocityFeatures,
         size_t freeHeapBytes);
        
    /**
     * Write a configuration variable query report.
     *
     * @param data the 7-byte data variable buffer, starting with the variable ID byte
     */
    bool reportConfigVar(const uint8_t *data);
     
    /**
     * Write a device ID report.
     */
    bool reportID(int index);
     
    /**
     * Write a build data report
     *
     * @param date build date plus time, in __DATE__ " " __TIME__ macro format ("Mon dd, yyyy hh:mm:ss")
     */
    bool reportBuildInfo(const char *date);

    /**
     * Send a raw byte buffer as a report
     */
    bool reportRawBytes(const uint8_t *data, size_t len);

     
    /**
      * Write a physical button status report.
      *
      * @param numButtons the number of buttons
      * @param state the button states, 1 bit per button, 8 buttons per byte,
      *        starting with button 0 in the low-order bit (0x01) of the 
      *        first byte
      */
    bool reportButtonStatus(int numButtons, const uint8_t *state);
    
    /**
     * Write an IR raw sensor input report.  This reports a set of raw
     * timing reports for input read from the IR sensor, for learning
     * remote purposes.
     *
     * @param n number of items to report, up to maxRawIR
     * @param data items to report; each is a timing reading, in 2us
     *        increments, with the low bit in each report set to 0 for
     *        a "space" (IR off) or 1 for a "mark" (IR on)
     */
    bool reportRawIR(int n, const uint16_t *data);
    
    /**
     * Maximum number of raw IR readings that can be sent in one report
     * via reportRawIR().
     */
    static const int maxRawIR = (reportLen - 3)/2;
    
    /**
     * Write an IR input report.  This reports a decoded command read in
     * learning mode to the host.
     *
     * @param pro protocol ID (see IRProtocolID.h)
     * @param flags bit flags: 0x02 = protocol uses dittos
     * @param code decoded command code
     */
    bool reportIRCode(uint8_t pro, uint8_t flags, uint64_t code);

    /**
     * Send a joystick report to the host
     *
     * @returns true if there is no error, false otherwise
     */
    bool update();
     
    /**
     * Move the cursor to (x, y)
     *
     * @param x x-axis position
     * @param y y-axis position
     * @returns true if there is no error, false otherwise
     */
    bool move(int16_t x, int16_t y);
     
    /**
     * Set the z position
     *
     * @param z z-axis osition
     */
    bool setZ(int16_t z);
     
    /**
     * Press one or several buttons
     *
     * @param buttons button state, as a bitwise combination of JOY_Bn values
     * @returns true if there is no error, false otherwise
     */
    bool buttons(uint32_t buttons);

#ifdef PINSCAPE_NUDGE_DIAGNOSTICS
    void reportNudgeDiagnostics() const;
#endif

    /* USB descriptor overrides
     *
     * Mbed OS 6 renamed every one of these, so the mbed 2 spellings below
     * silently stopped overriding anything: the device answered with USBHID's
     * own descriptors and enumerated as "mbed.org HID DEVICE", a generic HID
     * rather than a joystick.
     */
    virtual const uint8_t *configuration_desc(uint8_t index);
    virtual const uint8_t *reportDesc(int idx, uint16_t &len);

    /* USB descriptor string overrides */
    virtual const uint8_t *string_imanufacturer_desc();
    virtual const uint8_t *string_iserial_desc();
    virtual const uint8_t *string_iproduct_desc();

    // The Open Pinball Device report descriptor labels its data block with a
    // string index, and the host identifies the structure -- and its version --
    // by that string rather than by VID/PID. Mbed only dispatches the six
    // standard string indices, and Pinscape leaves the configuration string
    // unused, so the label lives there. (Proposed upstream as
    // mbed-ce/mbed-os#622, which would allow an index of our own.)
    virtual const uint8_t *string_iconfiguration_desc();

    // Handle all HID interfaces here: Mbed's base handler assumes one interface.
    void callback_request(const setup_packet_t *setup) override;
    void callback_request_xfer_done(const setup_packet_t *setup, bool aborted) override;

    /* callback overrides */

    // Endpoints are added here. The base class handles the joystick/LedWiz
    // pair; the keyboard pair is ours, since USBHID manages only one.
    virtual void callback_set_configuration(uint8_t configuration) override;

    // Every advertised interface has only alternate setting zero.
    void callback_set_interface(uint16_t interface, uint8_t alternate) override;
    void callback_state_change(DeviceState state) override;

    // A completed IN packet means the host is reading again after a timeout.
    void report_tx() override
    {
        _replyTimedOut = false;
#ifdef PINSCAPE_NUDGE_DIAGNOSTICS
        ++_diagCompletedReports;
#endif
    }

    // A report arrived on the joystick/LedWiz endpoint. Was EP1_OUT_callback,
    // which had to do its own reading; the base class reads it now.
    virtual void report_rx() override;

    // Transfer completions on the keyboard endpoints. mbed 2 named callbacks
    // after endpoint numbers; they are registered per endpoint now.
    void _kb_send_isr();
    void _kb_read_isr();
    void _opd_send_isr();

     
private:
#ifdef PINSCAPE_NUDGE_DIAGNOSTICS
    volatile uint32_t _diagCompletedReports = 0;
#endif

    // Incoming LedWiz message buffer.  Each LedWiz message is exactly 8 bytes.
    CircBuf<LedWizMsg, 16> lwbuf;
     
    // enable the joystick interface
    bool enableJoystick;
    
    // joystick axis reporting format
    int axisFormat;

    // enable the keyboard interface for button inputs
    bool useKB;
    
    // keyboard maximum idle time between reports
    volatile uint8_t kbIdleTime = 0;
    volatile uint8_t _kbProtocol = 1; // report protocol; zero is boot protocol
    volatile bool _kbResend = false;
    uint32_t _kbLastSend = 0;
    uint32_t _mediaLastSend = 0;

    // EP0 needs its own persistent buffer, separate from interrupt OUT reads.
    uint8_t _controlBuf[HID_DESCRIPTOR_LENGTH] = { 0 };
    bool _controlOutput = false;
    volatile bool _replyTimedOut = false;
    bool sendReply(const HID_REPORT *report);

    // Endpoints for the keyboard interface. USBHID provides one pair of its
    // own for the joystick; the keyboard is a second interface on this same
    // device, so its pair is ours to allocate and to feed. Both stay unused
    // when useKB is false, which is the case on a board whose buttons are all
    // joystick buttons.
    usb_ep_t _kb_in = 0;
    usb_ep_t _kb_out = 0;

    // Endpoint for the Open Pinball Device interface. IN only: the host never
    // sends anything on it. Bundled with the keyboard rather than separately
    // switchable, so a cabinet with LedWiz compatibility trouble strips down to
    // the joystick interface alone in one step.
    usb_ep_t _opd_in = 0;

    // Reception buffer for the keyboard endpoint. read_start() needs somewhere
    // to put a packet before the callback runs.
    uint8_t _kb_out_buf[MAX_REPORT_KB_RX + 1] = { 0 };
    
    // media maximum idle time between reports
    volatile uint8_t mediaIdleTime = 0;
    
    // current X, Y, Z axis values
    int16_t _x;                       
    int16_t _y;     
    int16_t _z;

    // Z0 axis (Z reading before firing event detection)
    int16_t _z0;

    // current velocity axis values
    int16_t _vx;
    int16_t _vy;
    int16_t _vz;
    
    // joystick button status bits
    uint16_t _buttonsLo;
    uint16_t _buttonsHi;
    
    // special status flag bits
    uint16_t _status;

    void _init();                 
};
 
#endif
