/* Copyright (c) 2010-2011 mbed.org, MIT License
*
* Permission is hereby granted, free of charge, to any person obtaining a copy of this software
* and associated documentation files (the "Software"), to deal in the Software without
* restriction, including without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all copies or
* substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
* BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
* DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "MMA8451Q.h"

#define REG_F_STATUS      0x00
#define REG_F_SETUP       0x09
#define REG_WHO_AM_I      0x0D
#define REG_CTRL_REG1     0x2A
#define REG_CTRL_REG2     0x2B
#define REG_CTRL_REG3     0x2c
#define REG_CTRL_REG4     0x2D
#define REG_CTRL_REG5     0x2E
#define REG_OFF_X         0x2F
#define REG_OFF_Y         0x30
#define REG_OFF_Z         0x31
#define XYZ_DATA_CFG_REG  0x0E
#define REG_OUT_X_MSB     0x01
#define REG_OUT_Y_MSB     0x03
#define REG_OUT_Z_MSB     0x05

#define UINT14_MAX        16383

#define CTL_ACTIVE        0x01
#define FS_MASK           0x03
#define FS_2G             0x00
#define FS_4G             0x01
#define FS_8G             0x02

#define F_STATUS_XDR_MASK 0x01  // F_STATUS - X sample ready
#define F_STATUS_YDR_MASK 0x02  // F_STATUS - Y sample ready
#define F_STATUS_ZDR_MASK 0x04  // F_STATUS - Z sample ready
#define F_STATUS_XYZDR_MASK 0x08 // F_STATUS - XYZ sample ready
#define F_STATUS_CNT_MASK 0x3F  // F_STATUS register mask for FIFO count

#define F_MODE_MASK       0xC0  // F_SETUP register mask for FIFO mode
#define F_WMRK_MASK       0x3F  // F_SETUP register mask for FIFO watermark

#define F_MODE_DISABLED   0x00  // FIFO disabled
#define F_MODE_CIRC       0x40  // circular FIFO
#define F_MODE_STOP       0x80  // FIFO stops when full
#define F_MODE_TRIGGER    0xC0  // FIFO triggers interrupt when watermark reached

#define HPF_OUT_MASK      0x10

#define SMODS_MASK        0x18

#define MODS_MASK         0x03
#define MODS_NORMAL       0x00   // mode 00 = normal power mode
#define MODS_LOW_NOISE    0x01   // mode 01 = low noise, low power
#define MODS_HI_RES       0x02   // mode 10 = high resolution
#define MODS_LOW_POWER    0x03   // mode 11 = low power

#define DR_MASK           0x38
#define DR_800_HZ         0x00
#define DR_400_HZ         0x08
#define DR_200_HZ         0x10
#define DR_100_HZ         0x18
#define DR_50_HZ          0x20
#define DR_12_HZ          0x28
#define DR_6_HZ           0x30
#define DR_1_HZ           0x38

#define F_READ_MASK       0x02  // CTRL_REG1 F_READ bit - sets data size mode:
                                // 1=fast read, 8-bit data; 0=14-bit data

#define CTRL_REG3_IPOL_MASK  0x02
#define CTRL_REG3_PPOD_MASK  0x01

#define INT_EN_DRDY       0x01
#define INT_CFG_DRDY      0x01


MMA8451Q::MMA8451Q(PinName sda, PinName scl, int addr, bool initialize) : m_i2c(sda, scl), m_addr(addr)
{
    // set the I2C to fast mode
    m_i2c.frequency(400000);

    // initialize parameters
    if (initialize)
        init();
}

// reset the accelerometer and set our parameters
bool MMA8451Q::init()
{    
    // reset all registers to power-on reset values
    uint8_t d0[2] = { REG_CTRL_REG2, 0x40 };
    if (!writeRegs(d0, 2))
        return false;
    
    // Allow the device's I2C logic to restart before reading its status.
    // NXP recommends 1 ms following software reset:
    // https://community.nxp.com/t5/Sensors/MMA8451-Interrupts-and-Reset-Behavior/td-p/821096
    wait_us(1000);

    // wait for the reset bit to clear
    if (!waitForClear(REG_CTRL_REG2, 0x40))
        return false;
    
    // go to standby mode
    if (!standby())
        return false;
    
    // turn off FIFO mode - this is required before changing the F_READ bit
    uint8_t d0a[2] = { REG_F_SETUP, 0 };
    if (!writeRegs(d0a, 2))
        return false;
    
    // read the curent config register
    uint8_t d1[1];
    if (!readRegs(XYZ_DATA_CFG_REG, d1, 1))
        return false;
    
    // set 2g mode by default
    uint8_t d2[2] = { XYZ_DATA_CFG_REG, (d1[0] & ~FS_MASK) | FS_2G };
    if (!writeRegs(d2, 2))
        return false;
    
    // read the ctl2 register
    uint8_t d3[1];
    if (!readRegs(REG_CTRL_REG2, d3, 1))
        return false;
    
    // set the high resolution mode
    uint8_t d4[2] = {REG_CTRL_REG2, (d3[0] & ~MODS_MASK) | MODS_HI_RES};
    if (!writeRegs(d4, 2))
        return false;
    
    // set 800 Hz mode, 14-bit data (clear the F_READ bit)
    uint8_t d5[1];
    if (!readRegs(REG_CTRL_REG1, d5, 1))
        return false;
    uint8_t d6[2] = {REG_CTRL_REG1, (d5[0] & ~(DR_MASK | F_READ_MASK)) | DR_800_HZ};
    if (!writeRegs(d6, 2))
        return false;
    
    // set circular FIFO mode
    uint8_t d7[1];
    if (!readRegs(REG_F_SETUP, d7, 1))
        return false;
    uint8_t d8[2] = {REG_F_SETUP, (d7[0] & ~F_MODE_MASK) | F_MODE_CIRC};
    if (!writeRegs(d8, 2))
        return false;

    // enter active mode
    return active();
}

MMA8451Q::~MMA8451Q() { }

bool MMA8451Q::sampleReady()
{
    uint8_t d;
    return readRegs(REG_F_STATUS, &d, 1)
        && (d & F_STATUS_XYZDR_MASK) == F_STATUS_XYZDR_MASK;
}

int MMA8451Q::getFIFOCount()
{
    uint8_t d;
    if (!readRegs(REG_F_STATUS, &d, 1))
        return -1;
    int count = d & F_STATUS_CNT_MASK;
    return count <= 32 ? count : -1;
}

bool MMA8451Q::setInterruptMode(int pin)
{
    // go to standby mode
    if (!standby())
        return false;

    // set IRQ push/pull and active high
    uint8_t d1[1];
    if (!readRegs(REG_CTRL_REG3, d1, 1))
        return false;
    uint8_t d2[2] = {
        REG_CTRL_REG3, 
        (d1[0] & ~CTRL_REG3_PPOD_MASK) | CTRL_REG3_IPOL_MASK
    };
    if (!writeRegs(d2, 2))
        return false;
    
    // set pin 2 or pin 1
    if (!readRegs(REG_CTRL_REG5, d1, 1))
        return false;
    uint8_t d3[2] = { 
        REG_CTRL_REG5, 
        (d1[0] & ~INT_CFG_DRDY) | (pin == 1 ? INT_CFG_DRDY : 0)
    };
    if (!writeRegs(d3, 2))
        return false;
    
    // enable data ready interrupt
    if (!readRegs(REG_CTRL_REG4, d1, 1))
        return false;
    uint8_t d4[2] = { REG_CTRL_REG4, d1[0] | INT_EN_DRDY };
    if (!writeRegs(d4, 2))
        return false;
    
    // enter active mode
    return active();
}

bool MMA8451Q::clearInterruptMode()
{
    // go to standby mode
    if (!standby())
        return false;
    
    // clear the interrupt register
    uint8_t d1[2] = { REG_CTRL_REG4, 0 };
    if (!writeRegs(d1, 2))
        return false;

    // enter active mode
    return active();
}

bool MMA8451Q::setRange(int g)
{
    // go to standby mode
    if (!standby())
        return false;
    
    // read the curent config register
    uint8_t d1[1];
    if (!readRegs(XYZ_DATA_CFG_REG, d1, 1))
        return false;
    
    // figure the mode flag for the desired G setting
    uint8_t mode = (g == 8 ? FS_8G : g == 4 ? FS_4G : FS_2G);
    
    // set new mode
    uint8_t d2[2] = { XYZ_DATA_CFG_REG, (d1[0] & ~FS_MASK) | mode };
    if (!writeRegs(d2, 2))
        return false;
    
    // enter active mode
    return active();
}

bool MMA8451Q::standby()
{
    // read the current control register
    uint8_t d1[1];
    if (!readRegs(REG_CTRL_REG1, d1, 1))
        return false;
    
    // Request standby once, then wait for acknowledgement with a deadline.
    uint8_t d2[2] = { REG_CTRL_REG1, uint8_t(d1[0] & ~CTL_ACTIVE) };
    if (!writeRegs(d2, 2))
        return false;
    return waitForClear(REG_CTRL_REG1, CTL_ACTIVE);
}

bool MMA8451Q::active()
{
    // read the current control register
    uint8_t d1[1];
    if (!readRegs(REG_CTRL_REG1, d1, 1))
        return false;
    
    // write it back out with the Active bit set
    uint8_t d2[2] = { REG_CTRL_REG1, d1[0] | CTL_ACTIVE };
    return writeRegs(d2, 2);
}

uint8_t MMA8451Q::getWhoAmI() {
    uint8_t who_am_i = 0;
    if (!readRegs(REG_WHO_AM_I, &who_am_i, 1))
        return 0;
    return who_am_i;
}

float MMA8451Q::getAccX() {
    return (float(getAccAxis(REG_OUT_X_MSB))/4096.0);
}

bool MMA8451Q::getAccXY(float &x, float &y)
{
    // read the X and Y output registers
    uint8_t res[4];
    if (!readRegs(REG_OUT_X_MSB, res, 4))
        return false;
    
    // translate the x value
    uint16_t acc = (res[0] << 8) | (res[1]);
    x = int16_t(acc)/(4*4096.0);
    
    // translate the y value
    acc = (res[2] << 9) | (res[3]);
    y = int16_t(acc)/(4*4096.0);
    return true;
}

bool MMA8451Q::getAccXYZ(float &x, float &y, float &z)
{
    // read the X, Y, and Z output registers
    uint8_t res[6];
    if (!readRegs(REG_OUT_X_MSB, res, 6))
        return false;
    
    // translate the x value
    uint16_t acc = (res[0] << 8) | (res[1]);
    x = int16_t(acc)/(4*4096.0);
    
    // translate the y value
    acc = (res[2] << 8) | (res[3]);
    y = int16_t(acc)/(4*4096.0);
    
    // translate the z value
    acc = (res[4] << 8) | (res[5]);
    z = int16_t(acc)/(4*4096.0);
    return true;
}

bool MMA8451Q::getAccXYZ(int &x, int &y, int &z)
{    
    // read the X, Y, and Z output registers
    uint8_t res[6];
    if (!readRegs(REG_OUT_X_MSB, res, 6))
        return false;
    
    // translate the register values
    x = xlat14(&res[0]);
    y = xlat14(&res[2]);
    z = xlat14(&res[4]);
    return true;
}

float MMA8451Q::getAccY() {
    return (float(getAccAxis(REG_OUT_Y_MSB))/4096.0);
}

float MMA8451Q::getAccZ() {
    return (float(getAccAxis(REG_OUT_Z_MSB))/4096.0);
}

void MMA8451Q::getAccAllAxis(float * res) {
    res[0] = getAccX();
    res[1] = getAccY();
    res[2] = getAccZ();
}

int16_t MMA8451Q::getAccAxis(uint8_t addr) {
    int16_t acc;
    uint8_t res[2];
    if (!readRegs(addr, res, 2))
        return 0;

    acc = (res[0] << 6) | (res[1] >> 2);
    if (acc > UINT14_MAX/2)
        acc -= UINT14_MAX;

    return acc;
}

// These register polls must not hold up the main loop indefinitely when a
// sensor acknowledges I2C requests but never completes reset or standby.
bool MMA8451Q::waitForClear(int addr, uint8_t mask)
{
    const uint32_t start = us_ticker_read();
    do {
        uint8_t value;
        if (!readRegs(addr, &value, 1))
            return false;
        if ((value & mask) == 0)
            return true;
        wait_us(100);
    } while (uint32_t(us_ticker_read() - start) < 10000);
    return false;
}

bool MMA8451Q::readRegs(int addr, uint8_t *data, int len)
{
    char reg = static_cast<char>(addr);
    int rc = m_i2c.write(m_addr, &reg, 1, true);
#ifdef PINSCAPE_NUDGE_DIAGNOSTICS
    if (rc != 0)
        printf("Nudge I2C: select reg=%02X result=%d\r\n", unsigned(addr), rc);
#endif
    if (rc != 0)
        return false;
    rc = m_i2c.read(m_addr, reinterpret_cast<char *>(data), len);
#ifdef PINSCAPE_NUDGE_DIAGNOSTICS
    if (rc != 0)
        printf("Nudge I2C: read reg=%02X result=%d\r\n", unsigned(addr), rc);
#endif
    return rc == 0;
}

bool MMA8451Q::writeRegs(uint8_t *data, int len)
{
    int rc = m_i2c.write(m_addr, reinterpret_cast<char *>(data), len);
#ifdef PINSCAPE_NUDGE_DIAGNOSTICS
    if (rc != 0)
        printf("Nudge I2C: write reg=%02X value=%02X result=%d\r\n",
            unsigned(data[0]), unsigned(data[1]), rc);
#endif
    return rc == 0;
}
