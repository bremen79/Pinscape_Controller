// Host fixtures for the implementations extracted by run_regressions.py.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cmath>
#include <type_traits>
#include "platform/Callback.h"
#include "mbed-os/drivers/usb/include/usb/internal/USBHID_Types.h"
#include "OpenPinballDevice.h"
#include "circbuf.h"
#include "mbed-os/drivers/usb/include/usb/internal/USBDevice_Types.h"
#include "mbed-os/drivers/usb/include/usb/internal/USBDescriptor.h"

extern "C" void mbed_assert_internal(const char *expr, const char *file, int line) {
    std::fprintf(stderr, "Mbed assertion: %s (%s:%d)\n", expr, file, line);
    std::abort();
}

// Simulate DMA registers, while using Mbed's actual Callback implementation.
struct { volatile uint8_t CHCFG[4]{}; } dmaMux;
struct { struct { volatile uint32_t DSR_BCR = 0; } DMA[4]; } dmaRegisters;
auto *DMAMUX0 = &dmaMux;
auto *DMA0 = &dmaRegisters;
constexpr uint32_t DMA_DSR_BCR_DONE_MASK = 1u << 24;
class SimpleDMA {
public:
    explicit SimpleDMA(uint8_t channel) : _channel(channel) {}
    void irq_handler(void);
    uint8_t _channel;
    mbed::Callback<void()> _callback;
};

// The allocator must stop at the linker heap limit, leaving the stack intact.
struct {
    alignas(8) char pool[256];
    char limit;
    char stack[64];
} heapFixture;
#define __HeapLimit heapFixture.limit
char *xmalloc_nxt = nullptr;
size_t xmalloc_rem = 0;
void *systemMallocResult = heapFixture.pool;
void *__real_malloc(size_t) { return systemMallocResult; }
struct HeapExhausted {};
[[noreturn]] void HaltOutOfMem() { throw HeapExhausted{}; }

#define IF_DIAG(...)
constexpr uint8_t REPORT_ID_KB = 1, REPORT_ID_MEDIA = 2, OPD_REPORT_ID = 1;
static uint64_t now_us = 0;
static void (*waitHook)() = nullptr;
uint32_t us_ticker_read() { return static_cast<uint32_t>(now_us); }
struct ticker_data_t {};
const ticker_data_t *get_us_ticker_data() { static ticker_data_t ticker; return &ticker; }
uint64_t ticker_read_us(const ticker_data_t *) { return now_us; }

struct Timer {
    uint64_t start_us = 0;
    void start() { start_us = now_us; }
    int read_us() const { return static_cast<int32_t>(now_us - start_us); }
};
struct CriticalSectionLock {};

// Only configuration fields used by the UART conflict check are needed here.
constexpr int CONSOLE_TX=1, CONSOLE_RX=2, MBED_CONF_PLATFORM_STDIO_BAUD_RATE=115200;
constexpr int PortTypeDisabled=0, PortTypeGPIOPWM=1, PortTypeGPIODig=2, PlungerType_None=0;
#define PINNAME_TO_WIRE(pin) uint8_t(pin)
struct Config {
    struct { uint8_t typ=0, pin=255; } outPort[4];
    struct { uint8_t pin=255; } button[4];
    struct {
        uint8_t sensorType=0, sensorPin[4]={255,255,255,255};
        struct { uint8_t btn=255, led=255; } cal;
    } plunger;
    struct { uint8_t statusPin=255, latchPin=255, relayPin=255; } TVON;
    struct { uint8_t sensor=255, emitter=255; } IR;
    struct { uint8_t nchips=0, sin=255, sclk=255, xlat=255, blank=255, gsclk=255; } tlc5940;
    struct { uint8_t nchips=0, sin=255, sclk=255, latch=255, ena=255; } hc595;
    struct { uint16_t chipMask=0; uint8_t sda=255, scl=255, reset=255; } tlc59116;
};
static bool crashConsoleEnabled=false;
struct {} stdio_uart;
int stdio_uart_inited=0;
unsigned serialInits=0, serialWrites=0, serialBaud=0;
void serial_init(decltype(stdio_uart)*, int tx, int rx) {
    assert(tx==CONSOLE_TX && rx==CONSOLE_RX);
    ++serialInits; stdio_uart_inited=1;
}
void serial_baud(decltype(stdio_uart)*, int baud) { serialBaud=baud; }
void serial_putc(decltype(stdio_uart)*, int) { ++serialWrites; }
namespace mbed { int minimal_console_putc(int); }

void __disable_irq() {}
void __enable_irq() {}
void diagLED(int, int, int) {}
void wait_us(unsigned us) { now_us += us; if (waitHook) waitHook(); }
struct { uint8_t ADDR = 0; } usb0;
auto *USB0 = &usb0;

struct LedWizMsg { uint8_t data[8]; };
struct Accel {};
struct TinyDigitalIn { uint8_t value = 1; int read() const { return value; } } __attribute__((packed));
struct { void attach_us(void (*)(), unsigned interval) { assert(interval == 1000); } } scanButtonsTimeout;
struct { bool changed; uint8_t data[8]; } kbState;
struct { bool changed; uint8_t data; } mediaState;

constexpr uint8_t IFC_ID_JS = 0, IFC_ID_KB = 1;
class USBHID {
public:
    enum DeviceState { Powered, Address, Configured };
    enum RequestResult { Receive, Send, Success, Failure, PassThrough };
    struct setup_packet_t {
        struct {uint8_t dataTransferDirection, Type, Recipient;} bmRequestType;
        uint8_t bRequest; uint16_t wValue, wIndex, wLength;
    };
    virtual void callback_state_change(DeviceState state) {
        ++stateCallbacks;
        if (state != Configured) std::memset(pending, 0, sizeof(pending));
    }
    virtual void callback_request(const setup_packet_t*) {}
    virtual void callback_request_xfer_done(const setup_packet_t*, bool) {}
    virtual void callback_set_interface(uint16_t, uint8_t) {}
    void complete_request(RequestResult r, uint8_t *data, uint32_t size) {
        result=r; controlData=data; controlSize=size; ++requestCompletions;
    }
    void complete_request_xfer_done(bool ok) { xferOK=ok; ++xferCompletions; }
    void complete_set_interface(bool ok) { interfaceOK=ok; ++interfaceCompletions; }
    RequestResult result=PassThrough;
    uint8_t* controlData=nullptr; uint32_t controlSize=0;
    unsigned requestCompletions=0, xferCompletions=0, interfaceCompletions=0;
    bool xferOK=false, interfaceOK=false;
    unsigned stateCallbacks=0;
    bool pending[3]{};
};

class USBJoystick: public USBHID {
public:
    enum { AXIS_FORMAT_XYZ, AXIS_FORMAT_RXRYRZ, AXIS_FORMAT_XYZ_RXRYRZ, reportLen = 22 };
    USBJoystick(uint16_t = 0, uint16_t = 0, uint16_t = 0,
                bool = false, bool = true, int format = 0, bool kb = true)
        : axisFormat(format), useKB(kb) {}
    bool configured() const { return state == Configured; }
    void connect() { ++connects; }
    void disconnect() { ++disconnects; USB0->ADDR = 0; setState(Powered); }
    void setState(DeviceState value) { state = value; callback_state_change(value); }
    void emitSuspend(bool value) { suspend(value); }

    // Mbed allows one outstanding packet on each interrupt endpoint. Model
    // that backpressure, and the Kinetis driver's immediate buffer copy.
    bool write_start(int endpoint, uint8_t *data, uint32_t length) {
        if (!configured() || pending[endpoint]) return false;
        pending[endpoint] = true;
        packets.emplace_back(data, data + length);
        return true;
    }
    bool send_nb(const HID_REPORT *report) {
        ++sendAttempts;
        if (!configured() || pending[0]) return false;
        joystick = *report;
        pending[0] = true;
        return true;
    }
    bool sendReply(const HID_REPORT *);
    bool updateStatus(uint32_t);
    bool reportRawBytes(const uint8_t *, size_t);
    bool kbIdleExpired() const;
    bool mediaIdleExpired() const;
    const uint8_t *reportDesc(int, uint16_t &);
    void callback_request(const setup_packet_t*) override;
    void callback_request_xfer_done(const setup_packet_t*, bool) override;
    void callback_set_interface(uint16_t, uint8_t) override;
    void callback_state_change(DeviceState) override;
    void reportButtonStatus(int count, const uint8_t *bits) {
        buttons.assign(bits, bits + (count + 7) / 8);
    }
    bool readLedWizMsg(LedWizMsg &message) {
        if (readIndex == commands.size()) return false;
        message = commands[readIndex++];
        return true;
    }
    bool update(int16_t, int16_t, int16_t, int16_t, int16_t, int16_t, int16_t, uint32_t, uint16_t);
    bool update();
    bool kbUpdate(uint8_t[8]);
    bool mediaUpdate(uint8_t);
    bool sendOPD(uint32_t, uint32_t, int16_t, int16_t, int16_t, int16_t, int16_t, int16_t);
    bool opdEnabled() const { return useKB; }

    int axisFormat;
    bool useKB;
    bool enableJoystick = true;
    volatile uint8_t kbIdleTime=0, mediaIdleTime=0, _kbProtocol=1;
    volatile bool _kbResend=false, _replyTimedOut=false;
    uint32_t _kbLastSend=0, _mediaLastSend=0;
    uint8_t _controlBuf[HID_DESCRIPTOR_LENGTH]{};
    bool _controlOutput=false;
    CircBuf<LedWizMsg, 16> lwbuf;
    unsigned sendAttempts=0;
    int _kb_in = 1, _opd_in = 2;
    int16_t _x, _y, _z, _z0, _vx, _vy, _vz;
    uint16_t _buttonsLo, _buttonsHi, _status;
    HID_REPORT joystick{};
    DeviceState state = Powered;
    unsigned connects = 0, disconnects = 0;
    std::vector<std::vector<uint8_t>> packets;
    std::vector<uint8_t> buttons;
    std::vector<LedWizMsg> commands;
    size_t readIndex = 0;

private:
    // Match Mbed CE: suspend is a private virtual function, not the old
    // sleepStateChanged notification. Dispatch through the base interface.
    virtual void suspend(bool) {}
};

std::vector<uint8_t> handled;
unsigned command_cost_us = 0;
void handleInputMsg(LedWizMsg &message, USBJoystick &, Accel &) {
    handled.push_back(message.data[0]);
    now_us += command_cost_us;
}

#include "firmware_under_test.inc"

void testDisconnect() {
    MyUSBJoystick js(0, 0, 0, false, true, 0, true);
    js.setState(USBJoystick::Configured);
    USB0->ADDR = 7;
    js.pending[0] = true;
    now_us = 0xfffffff0ULL;
    js.emitSuspend(true);
    assert(js.isSleeping() && !js.isConnected());
    assert(js.disconnects == 1 && !js.pending[0]);
    const auto suspendTime = now_us;
    js.recoverConnection();
    assert(now_us == suspendTime && js.connects == 0); // must not busy-wait
    now_us += 4999;
    js.recoverConnection();
    assert(js.connects == 0);
    ++now_us;
    js.recoverConnection();
    assert(js.connects == 1);
    js.recoverConnection();
    assert(js.connects == 1);
    js.setState(USBJoystick::Configured); // enumeration without RESUME
    assert(js.isConnected() && !js.isSleeping());
    USB0->ADDR = 8;
    js.emitSuspend(false);
    assert(js.disconnects == 1 && js.isConnected());
    assert(js.stateCallbacks == 3); // base HID callback still runs

    // Also recover when enumeration failed after SET_ADDRESS, before configure.
    js.setState(USBJoystick::Address);
    js.emitSuspend(true);
    now_us += 5000;
    js.recoverConnection();
    assert(js.connects == 2);
    js.setState(USBJoystick::Configured);
    assert(!js.isSleeping());
}

void testKeyboard() {
    USBJoystick js;
    js.setState(USBJoystick::Configured);
    kbState.changed = true;
    kbState.data[2] = 4;
    mediaState.changed = true;
    mediaState.data = 2;
    serviceKeys(js);
    assert(!kbState.changed && mediaState.changed);
    assert(js.packets.size() == 1 && js.packets.back()[3] == 4);
    serviceKeys(js); // busy: retain media update
    assert(mediaState.changed && js.packets.size() == 1);
    js.pending[1] = false;
    serviceKeys(js);
    assert(!mediaState.changed && js.packets.back()[0] == REPORT_ID_MEDIA);

    kbState.data[2] = 0;
    kbState.changed = true;
    serviceKeys(js); // release arrives while media report is in flight
    assert(kbState.changed);
    js.pending[1] = false;
    serviceKeys(js);
    assert(!kbState.changed && js.packets.back()[0] == REPORT_ID_KB);
    assert(js.packets.back()[3] == 0);

    js.setState(USBJoystick::Powered);
    kbState.changed = mediaState.changed = true;
    mediaState.data = 0;
    serviceKeys(js);
    assert(kbState.changed && mediaState.changed);
    js.setState(USBJoystick::Configured);
    serviceKeys(js);
    js.pending[1] = false;
    serviceKeys(js);
    assert(!kbState.changed && !mediaState.changed);
    assert(js.packets.back()[1] == 0);
}

void testButtons() {
    USBJoystick js;
    ButtonState inputs[5];
    const uint8_t indices[] = {0, 9, 12, 47, 48};
    for (int i = 0; i < 5; ++i) {
        inputs[i].physState = i != 2;
        inputs[i].cfgIndex = indices[i];
    }
    buttonState = inputs;
    nButtons = sizeof(inputs) / sizeof(inputs[0]);
    reportButtonStatus(js);
    assert((js.buttons == std::vector<uint8_t>{1, 2, 0, 0, 0, 128}));
    nButtons = 0;
    reportButtonStatus(js);
    assert(js.buttons == std::vector<uint8_t>(6, 0));
}

struct Axis {
    unsigned usage, offset, unit, exponent;
    int minimum, maximum;
    int logicalMinimum, logicalMaximum;
};

// Parse HID short items independently to check what a host actually sees.
std::vector<Axis> parseAxes() {
    unsigned page = 0, bits = 0, count = 0, offset = 0, unit = 0, exponent = 0;
    int minimum = 0, maximum = 0;
    int logicalMinimum = 0, logicalMaximum = 0;
    std::vector<unsigned> usages;
    std::vector<Axis> axes;
    for (size_t i = 0; i < sizeof(reportDescriptorJS);) {
        const uint8_t tag = reportDescriptorJS[i++];
        unsigned size = tag & 3;
        if (size == 3) size = 4;
        unsigned value = 0;
        for (unsigned j = 0; j < size; ++j) value |= unsigned(reportDescriptorJS[i++]) << (8*j);
        const unsigned kind = (tag >> 2) & 3, code = tag >> 4;
        const int signedValue = size == 2 ? int(int16_t(value)) : int(int8_t(value));
        if (kind == 1) {
            switch (code) {
            case 0: page = value; break;
            case 1: logicalMinimum = signedValue; break;
            case 2: logicalMaximum = logicalMinimum < 0 ? signedValue : int(value); break;
            case 3: minimum = signedValue; break;
            case 4: maximum = signedValue; break;
            case 5: exponent = value; break;
            case 6: unit = value; break;
            case 7: bits = value; break;
            case 9: count = value; break;
            }
        }
        if (kind == 2 && code == 0) usages.push_back(value);
        if (kind == 0 && code == 8) {
            if (page == 1 && bits == 16)
                for (unsigned k = 0; k < count; ++k)
                    axes.push_back({usages.at(k), (offset + k*bits)/8, unit, exponent,
                                    minimum, maximum, logicalMinimum, logicalMaximum});
            offset += count * bits;
        }
        if (kind == 0) usages.clear();
    }
    assert(offset == 22*8);
    return axes;
}

void testAxes() {
    assert(sizeof(reportDescriptorJS) == 77);
    for (int format : {0, 1, 2}) {
        USBJoystick js;
        js.axisFormat = format;
        js.setState(USBJoystick::Configured);
        js.update(100, 200, 300, 400, 500, 600, 700, 0, 0);
        const int expected[3][7] = {
            {100, 200, 300, 0, 0, 0, 0},
            {0, 0, 0, 100, 200, 300, 0},
            {100, 200, 400, 500, 600, 700, 300},
        };
        const auto axes = parseAxes();
        assert(axes.size() == 7);
        for (unsigned i = 0; i < axes.size(); ++i) {
            const auto &axis = axes[i];
            assert(axis.usage == 0x30 + i && axis.offset == 8 + 2*i);
            assert(axis.logicalMinimum == -4096 && axis.logicalMaximum == 4096);
            const auto *data = js.joystick.data + axis.offset;
            const int value = int16_t(data[0] | (unsigned(data[1]) << 8));
            assert(value == expected[format][i]);
            assert(axis.unit == 0 && axis.exponent == 0);
            assert(axis.minimum == 0 && axis.maximum == 0);
        }
        // The descriptor must interpret zero as center and preserve the
        // signs and limits of every advertised axis in every wire format.
        for (int sample : {-4096, 0, 4096}) {
            js.pending[0] = false;
            assert(js.update(sample, sample, sample, sample, sample, sample, sample, 0, 0));
            for (unsigned i = 0; i < axes.size(); ++i) {
                const auto &axis = axes[i];
                const auto *data = js.joystick.data + axis.offset;
                int value = int16_t(data[0] | (unsigned(data[1]) << 8));
                const bool used = format == 2 || (format == 0 ? i < 3 : i >= 3 && i < 6);
                assert(value == (used ? sample : 0));
                double normalized = 2.0 * (value - axis.logicalMinimum)
                    / (axis.logicalMaximum - axis.logicalMinimum) - 1.0;
                assert(normalized == (used ? sample/4096.0 : 0.0));
            }
        }
    }
}

void testCommands() {
    USBJoystick js;
    Accel accel;
    for (uint8_t id : {1, 2, 3}) {
        LedWizMsg message{};
        message.data[0] = id;
        js.commands.push_back(message);
    }
    command_cost_us = 5000;
    serviceCommands(js, accel);
    assert(handled == std::vector<uint8_t>{1} && js.readIndex == 1);
    serviceCommands(js, accel);
    assert((handled == std::vector<uint8_t>{1, 2}) && js.readIndex == 2);
    command_cost_us = 0;
    serviceCommands(js, accel);
    assert((handled == std::vector<uint8_t>{1, 2, 3}) && js.readIndex == 3);
}

void testTimestamps() {
    USBJoystick js;
    js.setState(USBJoystick::Configured);
    // Include a reporting pause spanning more than one hardware wrap.
    for (uint64_t time : {0xfffffff0ULL, 0x100000010ULL, 0x300000020ULL}) {
        now_us = time;
        js.pending[2] = false;
        assert(js.sendOPD(0, 0, 0, 0, 0, 0, 0, 0));
        uint64_t decoded = 0;
        for (int i = 0; i < 8; ++i) decoded |= uint64_t(js.packets.back()[1+i]) << (8*i);
        assert(decoded == time);
    }
}

void testOPDPlunger() {
    USBJoystick js;
    js.setState(USBJoystick::Configured);
    // Calibrated full pull, rest, forward travel, and values near rest.
    const int positions[][2] = {
        {4096, 32767}, {2048, 16383}, {512, 4095}, {1, 7}, {0, 0},
        {-1, -7}, {-512, -4095}, {-2048, -16383}, {-4096, -32767},
    };
    for (const auto &position : positions) {
        js.pending[2] = false;
        serviceOPD(js, position[0]);
        const auto &packet = js.packets.back();
        assert(packet.size() == OPD_REPORT_SIZE + 1 && packet[0] == OPD_REPORT_ID);
        const auto readAxis = [&](unsigned offset) {
            return int16_t(packet[1 + offset] | (unsigned(packet[2 + offset]) << 8));
        };
        assert(readAxis(OPD_OFS_PLUNGER_POS) == position[1]);
        assert(readAxis(OPD_OFS_AX_NUDGE) == 100 && readAxis(OPD_OFS_AY_NUDGE) == -200);
        assert(readAxis(OPD_OFS_VX_NUDGE) == 300 && readAxis(OPD_OFS_VY_NUDGE) == -400);
        assert(readAxis(OPD_OFS_PLUNGER_SPEED) == -500);
    }
}

void testDMA() {
    // The TSL14xx chain uses channels 1 and 2 with no callback; only the
    // final channel (3) notifies the sensor driver. All must acknowledge IRQs.
    SimpleDMA clockUp(1), adc(2), clockDown(3);
    unsigned completed = 0;
    clockDown._callback = [&completed] {
        // Hardware cleanup must finish before the callback can rearm DMA.
        assert(DMAMUX0->CHCFG[3] == 0);
        assert(DMA0->DMA[3].DSR_BCR & DMA_DSR_BCR_DONE_MASK);
        ++completed;
        DMAMUX0->CHCFG[3] = 0x80;
    };
    for (unsigned cycle = 0; cycle < 3; ++cycle) {
        for (auto *channel : {&clockUp, &adc, &clockDown}) {
            const auto n = channel->_channel;
            DMAMUX0->CHCFG[n] = 0x80;
            DMA0->DMA[n].DSR_BCR = 0;
            channel->irq_handler();
            assert(DMA0->DMA[n].DSR_BCR & DMA_DSR_BCR_DONE_MASK);
            assert(DMAMUX0->CHCFG[n] == (n == 3 ? 0x80 : 0));
        }
        assert(completed == cycle + 1);
    }
    // A detached callback must also be safe on a later completion.
    clockDown._callback = nullptr;
    DMA0->DMA[3].DSR_BCR = 0;
    clockDown.irq_handler();
    assert(completed == 3 && DMAMUX0->CHCFG[3] == 0);
    assert(DMA0->DMA[3].DSR_BCR & DMA_DSR_BCR_DONE_MASK);
}

void testHeap() {
    heapFixture.limit = 0x5a;
    std::memset(heapFixture.stack, 0x5a, sizeof(heapFixture.stack));
    const auto exhausted = [](size_t size) {
        bool halted = false;
        try { xmalloc(size); }
        catch (const HeapExhausted &) { halted = true; }
        assert(halted);
    };
    assert(xmalloc(1) == heapFixture.pool);
    assert(mallocBytesFree() == 252);
    exhausted(SIZE_MAX); // alignment must not wrap to a zero-byte request
    assert(mallocBytesFree() == 252);
    auto *rest = xmalloc(252);
    assert(rest == heapFixture.pool + 4 && mallocBytesFree() == 0);
    std::memset(rest, 0x33, 252);
    exhausted(1);
    assert(xmalloc_nxt == heapFixture.pool + 256 && mallocBytesFree() == 0);
    assert(heapFixture.limit == 0x5a);
    for (char byte : heapFixture.stack) assert(byte == 0x5a);

    // Failed or out-of-range system probes must not underflow the pool size.
    for (void *probe : {static_cast<void *>(nullptr), static_cast<void *>(&heapFixture.limit),
                       static_cast<void *>(heapFixture.stack)}) {
        xmalloc_nxt = nullptr;
        xmalloc_rem = 0;
        systemMallocResult = probe;
        exhausted(4);
        assert(xmalloc_rem == 0);
    }
}

void testUSBBackpressure() {
    USBJoystick js;
    js.setState(USBJoystick::Configured);
    js.pending[0] = true;
    const auto start = now_us = 0xfffffff0ULL;
    assert(!js.update());
    assert(!js.updateStatus(0));
    uint8_t raw[22]{};
    assert(!js.reportRawBytes(raw, sizeof(raw)));
    assert(now_us == start); // normal input reports never wait
    HID_REPORT reply{};
    reply.length = 22;
    assert(!js.sendReply(&reply));
    assert(now_us - start == 100000); // deadline works across timer wrap
    assert(!js.sendReply(&reply));
    assert(now_us - start == 100000); // remaining stream packets don't wait again
    js.pending[0] = false;
    assert(js.sendReply(&reply));
    assert(!js._replyTimedOut);
    js.setState(USBJoystick::Powered);
    const auto disconnected = now_us;
    assert(!js.sendReply(&reply));
    assert(now_us == disconnected);

    // A host that polls after a short delay must still get a configuration reply.
    js.setState(USBJoystick::Configured);
    js.pending[0] = true;
    static USBJoystick *waiting;
    waiting = &js;
    waitHook = [] { waiting->pending[0] = false; };
    assert(js.sendReply(&reply));
    waitHook = nullptr;
    assert(now_us == disconnected + 50);
}

USBHID::setup_packet_t hidRequest(uint8_t request, uint16_t value,
                                 uint16_t iface, uint16_t length, bool input = false) {
    return {{uint8_t(input ? DEVICE_TO_HOST : HOST_TO_DEVICE), CLASS_TYPE, INTERFACE_RECIPIENT},
            request, value, iface, length};
}

void testUSBControl() {
    USBJoystick js;
    js.setState(USBJoystick::Configured);
    for (uint16_t iface : {0, 1, 2, 3, 256}) {
        const auto count = js.interfaceCompletions;
        js.callback_set_interface(iface, 0);
        assert(js.interfaceCompletions == count + 1);
        assert(js.interfaceOK == (iface < 3));
        js.callback_set_interface(iface, 1);
        assert(!js.interfaceOK);
    }
    js.useKB = false;
    js.callback_set_interface(1, 0);
    assert(!js.interfaceOK);
    js.useKB = true;

    // Check each interface's HID length against its actual report descriptor.
    for (uint16_t iface : {0, 1, 2}) {
        auto p = hidRequest(GET_DESCRIPTOR, HID_DESCRIPTOR << 8, iface, 9, true);
        p.bmRequestType.Type = STANDARD_TYPE;
        js.callback_request(&p);
        uint16_t len = 0;
        assert(js.reportDesc(iface, len) != nullptr);
        assert(js.result == USBHID::Send && js.controlSize == 9);
        assert((js.controlData[7] | unsigned(js.controlData[8]) << 8) == len);
        p.wValue = REPORT_DESCRIPTOR << 8;
        js.callback_request(&p);
        assert(js.result == USBHID::Send && js.controlSize == len);
    }

    auto p = hidRequest(SET_REPORT, 0x200, 0, 8);
    js.callback_request(&p);
    assert(js.result == USBHID::Receive && js.controlSize == p.wLength);
    const uint8_t command[] = {65, 2, 3, 4, 5, 6, 7, 8};
    std::memcpy(js.controlData, command, sizeof(command));
    js.callback_request_xfer_done(&p, false);
    LedWizMsg received;
    assert(js.xferOK && js.lwbuf.read(received));
    assert(std::memcmp(received.data, command, 8) == 0);
    js.callback_request(&p);
    js.callback_request_xfer_done(&p, true);
    assert(!js.lwbuf.read(received)); // cancelled transfers must not execute commands
    for (int i = 0; i < 15; ++i) assert(js.lwbuf.write(received));
    js.callback_request(&p);
    js.callback_request_xfer_done(&p, false);
    assert(!js.xferOK); // reject instead of acknowledging a discarded command

    for (uint16_t length : {0, 7, 9, 64, 65535}) {
        p.wLength = length;
        js.callback_request(&p);
        assert(js.result == USBHID::Failure);
    }
    p = hidRequest(SET_REPORT, 0x200, 0, 8, true);
    js.callback_request(&p);
    assert(js.result == USBHID::Failure);
    p.bmRequestType.dataTransferDirection = HOST_TO_DEVICE;
    p.bmRequestType.Recipient = DEVICE_RECIPIENT;
    js.callback_request(&p);
    assert(js.result == USBHID::Failure);
    p = hidRequest(SET_REPORT, 0x301, 1, 2); // no feature reports
    js.callback_request(&p);
    assert(js.result == USBHID::Failure);
    p = hidRequest(SET_REPORT, 0x201, 1, 2);
    js.callback_request(&p);
    assert(js.result == USBHID::Receive && js.controlSize == 2);
    js.controlData[0] = REPORT_ID_KB; js.controlData[1] = 3;
    js.callback_request_xfer_done(&p, false);
    assert(js.xferOK);

    // Boot protocol sends the unnumbered 8-byte keyboard packet and LED byte.
    p = hidRequest(0x0B, 0, 1, 0);
    js.callback_request(&p);
    assert(js.result == USBHID::Success);
    uint8_t keys[8] = {0, 0, 4};
    assert(js.kbUpdate(keys));
    assert(js.packets.back() == std::vector<uint8_t>(keys, keys + 8));
    const auto packets = js.packets.size();
    assert(js.mediaUpdate(1) && js.packets.size() == packets);
    p = hidRequest(0x03, 0, 1, 1, true);
    js.callback_request(&p);
    assert(js.result == USBHID::Send && js.controlData[0] == 0);
    p = hidRequest(SET_REPORT, 0x200, 1, 1);
    js.callback_request(&p);
    assert(js.result == USBHID::Receive && js.controlSize == 1);
    js.callback_request_xfer_done(&p, false);
    assert(js.xferOK);
    p = hidRequest(0x0B, 2, 1, 0);
    js.callback_request(&p);
    assert(js.result == USBHID::Failure);

    js.setState(USBJoystick::Powered);
    p = hidRequest(SET_REPORT, 0x200, 0, 8);
    js.callback_request(&p);
    assert(js.result == USBHID::Failure);
    assert(js._kbProtocol == 1 && js.kbIdleTime == 0 && js.mediaIdleTime == 0);
    js.callback_set_interface(0, 0);
    assert(!js.interfaceOK);
}

void testHIDIdle() {
    USBJoystick js;
    js.setState(USBJoystick::Configured);
    now_us = 0xfffffff0ULL;
    uint8_t keys[8]{};
    assert(js.kbUpdate(keys));
    js.pending[1] = false;
    assert(js.mediaUpdate(0));
    js.pending[1] = false;
    auto p = hidRequest(SET_IDLE, 0x0200, 1, 0); // ID zero changes both rates to 8 ms
    js.callback_request(&p);
    assert(js.result == USBHID::Success);
    now_us += 7999;
    assert(!js.kbIdleExpired() && !js.mediaIdleExpired());
    ++now_us;
    assert(js.kbIdleExpired() && js.mediaIdleExpired());
    serviceKeys(js);
    assert(!js.kbIdleExpired() && js.mediaIdleExpired());
    js.pending[1] = false;
    serviceKeys(js);
    assert(!js.mediaIdleExpired());
    p = hidRequest(SET_IDLE, 0x0302, 1, 0); // media only
    js.callback_request(&p);
    assert(js.result == USBHID::Success);
    for (uint8_t id : {1, 2}) {
        p = hidRequest(GET_IDLE, id, 1, 1, true);
        js.callback_request(&p);
        assert(js.result == USBHID::Send && js.controlSize == 1);
        assert(js.controlData[0] == (id == 1 ? 2 : 3));
    }
    p = hidRequest(SET_IDLE, 0, 1, 0);
    js.callback_request(&p);
    now_us += 1000000;
    assert(!js.kbIdleExpired() && !js.mediaIdleExpired());
    js.useKB = false;
    js.callback_request(&p);
    assert(js.result == USBHID::Failure);

    // With a host slower than both idle rates, both report types still run.
    js.useKB = true;
    p = hidRequest(SET_IDLE, 0x0100, 1, 0);
    js.callback_request(&p);
    const auto first = js.packets.size();
    for (int i = 0; i < 4; ++i) {
        now_us += 10000;
        js.pending[1] = false;
        serviceKeys(js);
    }
    assert(js.packets.size() == first + 4);
    for (size_t i = first + 1; i < js.packets.size(); ++i)
        assert(js.packets[i][0] != js.packets[i-1][0]);
}

void testButtonScan() {
    static_assert(std::is_volatile<decltype(ButtonState::physState)>::value,
                  "interrupt-shared physical state must be volatile");
    ButtonState b;
    buttonState = &b; nButtons = 1;
    b.logState = 1; b.prevLogState = 1; b.pulseState = 4;
    b.di.value = 0;
    scanButtons();
    assert(b.physState == 1 && b.logState == 1 && b.prevLogState == 1 && b.pulseState == 4);
    b.di.value = 1;
    for (int i = 0; i < 4; ++i) { scanButtons(); assert(b.physState == 1); }
    scanButtons();
    assert(b.physState == 0 && b.logState == 1 && b.prevLogState == 1 && b.pulseState == 4);

    // Simulate main's stale read/modify/write interrupted by a physical press.
    b.logState = 0;
    uint8_t before[sizeof(b)], after[sizeof(b)];
    std::memcpy(before, &b, sizeof(b));
    b.logState = 1;
    std::memcpy(after, &b, sizeof(b));
    b.logState = 0;
    b.di.value = 0;
    scanButtons();
    for (size_t i = 0; i < sizeof(b); ++i)
        if (before[i] != after[i]) reinterpret_cast<uint8_t *>(&b)[i] = after[i];
    assert(b.physState == 1 && b.logState == 1);
}

void testConsole() {
    // Even a fatal error before configuration load must not initialize UART.
    mbed::minimal_console_putc('x');
    assert(serialInits == 0 && serialWrites == 0);
    Config cfg;
    assert(consolePinsFree(cfg));
    cfg.outPort[0].typ=PortTypeGPIOPWM; cfg.outPort[0].pin=CONSOLE_TX;
    assert(!consolePinsFree(cfg));
    initCrashConsole(cfg);
    mbed::minimal_console_putc('x');
    assert(serialInits == 0 && serialWrites == 0);

    cfg=Config{}; cfg.button[1].pin=CONSOLE_RX;
    assert(!consolePinsFree(cfg));
    cfg=Config{}; cfg.plunger.sensorType=1; cfg.plunger.sensorPin[0]=CONSOLE_TX;
    assert(!consolePinsFree(cfg));
    cfg=Config{}; cfg.plunger.cal.led=CONSOLE_TX;
    assert(!consolePinsFree(cfg));
    cfg=Config{}; cfg.TVON.latchPin=CONSOLE_RX;
    assert(!consolePinsFree(cfg));
    cfg=Config{}; cfg.IR.sensor=CONSOLE_TX;
    assert(!consolePinsFree(cfg));
    cfg=Config{}; cfg.tlc5940.nchips=1; cfg.tlc5940.gsclk=CONSOLE_RX;
    assert(!consolePinsFree(cfg));
    cfg=Config{}; cfg.hc595.nchips=1; cfg.hc595.latch=CONSOLE_TX;
    assert(!consolePinsFree(cfg));
    cfg=Config{}; cfg.tlc59116.chipMask=1; cfg.tlc59116.reset=CONSOLE_RX;
    assert(!consolePinsFree(cfg));

    cfg=Config{}; cfg.outPort[0].typ=3; cfg.outPort[0].pin=CONSOLE_TX;
    assert(consolePinsFree(cfg)); // expander channel numbers aren't GPIO pins
    initCrashConsole(cfg);
    mbed::minimal_console_putc('a');
    mbed::minimal_console_putc('b');
    assert(serialInits == 1 && serialWrites == 2 && serialBaud == 115200);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    if (!std::strcmp(argv[1], "disconnect")) testDisconnect();
    else if (!std::strcmp(argv[1], "keyboard")) testKeyboard();
    else if (!std::strcmp(argv[1], "buttons")) testButtons();
    else if (!std::strcmp(argv[1], "axes")) testAxes();
    else if (!std::strcmp(argv[1], "commands")) testCommands();
    else if (!std::strcmp(argv[1], "timestamps")) testTimestamps();
    else if (!std::strcmp(argv[1], "opd_plunger")) testOPDPlunger();
    else if (!std::strcmp(argv[1], "dma")) testDMA();
    else if (!std::strcmp(argv[1], "heap")) testHeap();
    else if (!std::strcmp(argv[1], "usb_backpressure")) testUSBBackpressure();
    else if (!std::strcmp(argv[1], "usb_control")) testUSBControl();
    else if (!std::strcmp(argv[1], "hid_idle")) testHIDIdle();
    else if (!std::strcmp(argv[1], "button_scan")) testButtonScan();
    else if (!std::strcmp(argv[1], "console")) testConsole();
    else return 2;
}
