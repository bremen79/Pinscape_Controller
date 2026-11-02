#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <new>
#include <string>
#include "MMA8451Q.h"

uint64_t now_us = 0;
int running_timers = 0;
int i2c_instances = 0;

struct SensorBus {
    std::array<uint8_t, 256> regs{};
    std::deque<std::array<int, 3>> samples;
    std::array<int, 3> lastSample{};
    int selected = 0, transfers = 0, failAt = -1;
    int resets = 0, sampleReads = 0, statusReads = 0, stickyCount = -1;
    bool disconnected = false, stuckReset = false, stuckActive = false;
    uint64_t readyAt = 0;
    bool transferFailed() {
        now_us += 30;
        return ++transfers == failAt || disconnected || now_us < readyAt;
    }
} bus;

int I2C::write(int address, const char *data, int len, bool repeated)
{
    assert(address == (0x1d << 1));
    if (bus.transferFailed()) return 1;
    bus.selected = uint8_t(data[0]);
    assert((len == 1 && repeated) || (len == 2 && !repeated));
    if (len == 2) {
        uint8_t value = uint8_t(data[1]);
        if (bus.selected == 0x2b && (value & 0x40)) {
            ++bus.resets;
            bus.regs.fill(0);
            bus.samples.clear();
            bus.regs[0x2b] = bus.stuckReset ? 0x40 : 0;
            bus.readyAt = now_us + 1000;
        } else {
            bus.regs[bus.selected] = value;
        }
    }
    return 0;
}

int I2C::read(int address, char *data, int len, bool repeated)
{
    assert(address == (0x1d << 1) && !repeated);
    if (bus.transferFailed()) {
        // A failed HAL read can still overwrite part of its destination.
        data[0] = 0x7f;
        return 1;
    }
    if (bus.selected == 0) {
        ++bus.statusReads;
        data[0] = bus.stickyCount >= 0 ? bus.stickyCount : bus.samples.size();
    } else if (bus.selected == 1 && len == 6) {
        ++bus.sampleReads;
        now_us += 120;
        if (!bus.samples.empty()) {
            bus.lastSample = bus.samples.front();
            bus.samples.pop_front();
        }
        for (int i = 0; i < 3; ++i) {
            uint16_t raw = static_cast<uint16_t>(bus.lastSample[i] * 4);
            data[2*i] = raw >> 8;
            data[2*i + 1] = raw & 0xff;
        }
    } else {
        for (int i = 0; i < len; ++i) data[i] = bus.regs[bus.selected + i];
        if (bus.selected == 0x2a && bus.stuckActive) data[0] |= 1;
    }
    return 0;
}

constexpr PinName MMA8451_SDA_PIN = 25, MMA8451_SCL_PIN = 24;
constexpr int MMA8451_I2C_ADDRESS = 0x1d << 1;
template<typename T> T square(T value) { return value * value; }
struct Config {
    struct { uint8_t range = 0, autoCenterTime = 255, velocityScalingFactor = 20; } accel;
};
#include "accelerometer_under_test.inc"

void expectReport(Accel &accel, int expectedX, int expectedY)
{
    int x = 777, y = 777;
    accel.get(x, y);
    assert(x == expectedX && y == expectedY);
}

void driverErrors()
{
    MMA8451Q sensor(25, 24, 0x1d << 1, false);
    // Inject a failure at every I2C operation in each setup sequence. The
    // sequence must stop immediately and report the error to the caller.
    const std::function<bool()> operations[] = {
        [&] { return sensor.init(); }, [&] { return sensor.setRange(8); },
        [&] { return sensor.standby(); }, [&] { return sensor.active(); },
        [&] { return sensor.setInterruptMode(1); },
        [&] { return sensor.clearInterruptMode(); },
    };
    for (auto &operation : operations) {
        bus = SensorBus();
        assert(operation());
        int transfers = bus.transfers;
        for (int fail = 1; fail <= transfers; ++fail) {
            bus = SensorBus();
            bus.failAt = fail;
            assert(!operation());
            assert(bus.transfers == fail);
        }
    }
    for (int fail = 1; fail <= 2; ++fail) {
        bus = SensorBus();
        bus.failAt = fail;
        int x = 12, y = -34, z = 56;
        assert(!sensor.getAccXYZ(x, y, z));
        assert(x == 12 && y == -34 && z == 56);
        assert(bus.transfers == fail);
        bus = SensorBus();
        bus.failAt = fail;
        assert(sensor.getFIFOCount() == -1);
    }
    bus = SensorBus();
    bus.stickyCount = 0x80 | 32; // Overflow flag is separate from count.
    assert(sensor.getFIFOCount() == 32);
    bus.stickyCount = 33;
    assert(sensor.getFIFOCount() == -1);
    bus.stickyCount = 63;
    assert(sensor.getFIFOCount() == -1);
    bus = SensorBus();
    bus.lastSample = {-8192, 8191, -1};
    int x, y, z;
    assert(sensor.getAccXYZ(x, y, z));
    assert(x == -8192 && y == 8191 && z == -1);
}

void driverTimeouts()
{
    MMA8451Q sensor(25, 24, 0x1d << 1, false);
    for (uint64_t start : {uint64_t(0), uint64_t(UINT32_MAX) - 5000}) {
        now_us = start;
        bus = SensorBus();
        bus.stuckReset = true;
        assert(!sensor.init());
        assert(now_us - start >= 10000 && now_us - start < 12000);
        assert(bus.transfers < 210);
        now_us = start;
        bus = SensorBus();
        bus.stuckActive = true;
        assert(!sensor.standby());
        assert(now_us - start >= 10000 && now_us - start < 12000);
        assert(bus.transfers < 210);
    }
}

void emptyReports()
{
    Config cfg;
    for (int range = AccelRange1G; range <= AccelRange8G; ++range) {
        bus = SensorBus();
        cfg.accel.range = range;
        Accel accel(cfg);
        assert(bus.resets == 1); // One setup per construction.
        for (int i = 0; i < 10; ++i) expectReport(accel, 0, 0);
        assert(accel.poll()); // Empty FIFO is normal, not a sensor fault.
        expectReport(accel, 0, 0);
        bus.samples = {{{1000, -2000, 4096}}, {{3000, -4000, 4096}}};
        assert(accel.poll());
        int divisor = range == AccelRange1G ? 1 : 2;
        expectReport(accel, 2000/divisor, -3000/divisor);
        for (int i = 0; i < 10; ++i) {
            now_us += 100;
            assert(accel.poll());
            expectReport(accel, 2000/divisor, -3000/divisor);
        }
        accel.reset();
        expectReport(accel, 0, 0);
    }
}

void boundedFIFO()
{
    Config cfg;
    Accel accel(cfg);
    bus.stickyCount = 32;
    bus.lastSample = {1000, -2000, 4096};
    int reads = bus.sampleReads, statuses = bus.statusReads;
    assert(accel.poll());
    assert(bus.sampleReads == reads + 32 && bus.statusReads == statuses + 1);
    expectReport(accel, 1000, -2000);
    // The wedged-sensor check must still run with a permanently nonempty FIFO.
    now_us += 2000001;
    assert(!accel.poll());
    expectReport(accel, 0, 0);
    assert(accel.getVX() == 0 && accel.getVY() == 0);
}

void recovery()
{
    Config cfg;
    now_us = UINT32_MAX - 500000ull;
    bus.disconnected = true;
    {
        Accel accel(cfg);
        assert(!accel.poll());
        expectReport(accel, 0, 0);
        int calls = bus.transfers;
        // No bus traffic during backoff, including across ticker wrap.
        for (int i = 0; i < 999; ++i) {
            now_us += 1000;
            assert(!accel.poll());
            Accel::softReset(&accel, cfg);
        }
        assert(bus.transfers == calls);
        now_us += 1000;
        bus.disconnected = false;
        Accel::softReset(&accel, cfg);
        assert(bus.transfers > calls && bus.resets == 1);
        assert(accel.poll());
        bus.samples.push_back({1234, -2345, 4096});
        assert(accel.poll());
        expectReport(accel, 1234, -2345);
        // Repeated permanent failures must neither leak timer locks nor
        // reconstruct the I2C peripheral on every main-loop iteration.
        bus.disconnected = true;
        assert(!accel.poll());
        expectReport(accel, 0, 0);
        for (int i = 0; i < 300; ++i) {
            now_us += 1000000;
            Accel::softReset(&accel, cfg);
            assert(!accel.poll());
            assert(running_timers == 2 && i2c_instances == 1);
        }
    }
    assert(running_timers == 0 && i2c_instances == 0);
}

void pollErrors()
{
    Config cfg;
    for (int fail = 1; fail <= 4; ++fail) {
        bus = SensorBus();
        Accel accel(cfg);
        bus.samples.push_back({1000, -2000, 4096});
        assert(accel.poll());
        expectReport(accel, 1000, -2000);
        bus.samples.push_back({3000, -4000, 4096});
        bus.failAt = bus.transfers + fail;
        assert(!accel.poll());
        expectReport(accel, 0, 0);
        assert(accel.getVX() == 0 && accel.getVY() == 0);
        int calls = bus.transfers;
        assert(!accel.poll() && bus.transfers == calls);
    }
    bus = SensorBus();
    Accel accel(cfg);
    bus.stickyCount = 63;
    assert(!accel.poll());
    expectReport(accel, 0, 0);
    bus.stickyCount = -1;
    accel.reset();
    now_us += 100001;
    assert(!accel.poll());
    expectReport(accel, 0, 0);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    std::string name = argv[1];
    if (name == "accel_driver_errors") driverErrors();
    else if (name == "accel_timeouts") driverTimeouts();
    else if (name == "accel_empty_reports") emptyReports();
    else if (name == "accel_fifo") boundedFIFO();
    else if (name == "accel_recovery") recovery();
    else if (name == "accel_poll_errors") pollErrors();
    else return 2;
    assert(running_timers == 0 && i2c_instances == 0);
}
