// Optional error reporting for the optimized diagnostic build. Mbed invokes
// this hook even with NDEBUG defined, before entering its four-fast/four-slow
// LED halt loop. No work is done on the normal input/reporting path.
#include "mbed.h"
#include "config.h"
#include "NewMalloc.h"
#include "platform/internal/mbed_fault_handler.h"

#if !TEST_KEEP_PRINTF || !MBED_CONF_TARGET_CONSOLE_UART
#error Crash diagnostics require the OpenSDA console and its reserved UART pins
#endif

extern "C" char __HeapLimit;

// The UART is deliberately silent until the saved configuration has been
// checked. Factory defaults alone cannot reserve pins in an existing cabinet.
static bool crashConsoleEnabled = false;

bool consolePinsFree(const Config &config)
{
    const auto uart = [](uint8_t pin) {
        return pin == PINNAME_TO_WIRE(CONSOLE_TX) || pin == PINNAME_TO_WIRE(CONSOLE_RX);
    };
    for (const auto &port : config.outPort)
    {
        if (port.typ == PortTypeDisabled) break;
        if ((port.typ == PortTypeGPIOPWM || port.typ == PortTypeGPIODig) && uart(port.pin))
            return false;
    }
    for (const auto &button : config.button)
        if (uart(button.pin)) return false;
    // Sensors can be initialized even when plunger reporting is disabled.
    if (config.plunger.sensorType != PlungerType_None)
        for (const auto pin : config.plunger.sensorPin)
            if (uart(pin)) return false;
    if (uart(config.plunger.cal.btn) || uart(config.plunger.cal.led)
        || uart(config.TVON.statusPin) || uart(config.TVON.latchPin) || uart(config.TVON.relayPin)
        || uart(config.IR.sensor) || uart(config.IR.emitter))
        return false;
    if (config.tlc5940.nchips != 0
        && (uart(config.tlc5940.sin) || uart(config.tlc5940.sclk) || uart(config.tlc5940.xlat)
            || uart(config.tlc5940.blank) || uart(config.tlc5940.gsclk)))
        return false;
    if (config.hc595.nchips != 0
        && (uart(config.hc595.sin) || uart(config.hc595.sclk)
            || uart(config.hc595.latch) || uart(config.hc595.ena)))
        return false;
    if (config.tlc59116.chipMask != 0
        && (uart(config.tlc59116.sda) || uart(config.tlc59116.scl) || uart(config.tlc59116.reset)))
        return false;
    return true;
}

void initCrashConsole(const Config &config)
{
    crashConsoleEnabled = consolePinsFree(config);
}

extern "C" {
    extern serial_t stdio_uart;
    extern int stdio_uart_inited;
}

// Mbed's weak minimal-console hook covers both printf and fatal-error output.
// Suppress all UART initialization/output if the cabinet owns either pin.
int mbed::minimal_console_putc(int c)
{
    if (!crashConsoleEnabled)
        return c;
    CriticalSectionLock lock;
    if (!stdio_uart_inited)
    {
        serial_init(&stdio_uart, CONSOLE_TX, CONSOLE_RX);
        serial_baud(&stdio_uart, MBED_CONF_PLATFORM_STDIO_BAUD_RATE);
    }
    serial_putc(&stdio_uart, c);
    return c;
}

void reportStartupMemory(const char *stage)
{
    printf("Startup %s: heap free=%lu, MSP=0x%08lX, stack limit=0x%08lX\r\n",
        stage, static_cast<unsigned long>(mallocBytesFree()),
        static_cast<unsigned long>(__get_MSP()),
        static_cast<unsigned long>(reinterpret_cast<uintptr_t>(&__HeapLimit)));
}

extern "C" void mbed_error_hook(const mbed_error_ctx *ctx)
{
    // Print only the first fatal error, avoiding recursion if the console
    // itself fails. Non-fatal warnings must not hide a later fatal error.
    static bool reporting = false;
    if (!mbed_get_error_in_progress() || reporting)
        return;
    reporting = true;

    mbed_error_printf("\nPINSCAPE CRASH DIAGNOSTICS\n");
    mbed_error_printf("Status=0x%08lX Code=%lu Module=%lu\n",
        static_cast<unsigned long>(ctx->error_status),
        static_cast<unsigned long>(MBED_GET_ERROR_CODE(ctx->error_status)),
        static_cast<unsigned long>(MBED_GET_ERROR_MODULE(ctx->error_status)));
    mbed_error_printf("Address=0x%08lX Value=0x%08lX SP=0x%08lX\n",
        static_cast<unsigned long>(ctx->error_address),
        static_cast<unsigned long>(ctx->error_value),
        static_cast<unsigned long>(ctx->thread_current_sp));

    if (ctx->error_status == MBED_ERROR_HARDFAULT_EXCEPTION && ctx->error_value != 0)
    {
        const auto *fault = reinterpret_cast<const mbed_fault_context_t *>(ctx->error_value);
        mbed_error_printf("PC=0x%08lX LR=0x%08lX xPSR=0x%08lX\n",
            static_cast<unsigned long>(fault->PC_reg),
            static_cast<unsigned long>(fault->LR_reg),
            static_cast<unsigned long>(fault->xPSR));
        mbed_error_printf("R0=0x%08lX R1=0x%08lX R2=0x%08lX R3=0x%08lX\n",
            static_cast<unsigned long>(fault->R0_reg),
            static_cast<unsigned long>(fault->R1_reg),
            static_cast<unsigned long>(fault->R2_reg),
            static_cast<unsigned long>(fault->R3_reg));
    }
}
