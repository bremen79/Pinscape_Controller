#pragma once
#include <cstdint>

// Host-only Mbed substitutes. The sensor protocol is simulated in the fixture;
// both the MMA8451Q driver and Accel implementation come from production code.
extern uint64_t now_us;
extern int running_timers;
extern int i2c_instances;
inline uint32_t us_ticker_read() { return static_cast<uint32_t>(now_us); }
inline void wait_us(int us) { now_us += us; }

using PinName = int;
constexpr int PIN_INPUT = 0, PullNone = 0, PullUp = 1;
class DigitalInOut {
public:
    DigitalInOut(PinName, int, int, int) {}
    void mode(int) {}
    void output() {}
    void input() {}
    DigitalInOut &operator=(int) { return *this; }
    operator int() const { return 1; }
};

class Timer {
    uint64_t start_us = 0;
    bool running = false;
public:
    ~Timer() { if (running) --running_timers; }
    void start() {
        if (!running) { ++running_timers; start_us = now_us; running = true; }
    }
    void reset() { start_us = now_us; }
    int read_us() const { return static_cast<int32_t>(now_us - start_us); }
};

class I2C {
public:
    I2C(PinName, PinName) { ++i2c_instances; }
    ~I2C() { --i2c_instances; }
    void frequency(int) {}
    int write(int, const char *, int, bool = false);
    int read(int, char *, int, bool = false);
};
