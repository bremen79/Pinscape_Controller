#!/usr/bin/env python3
"""Run host-side firmware regressions: python3 tests/run_regressions.py.

Requires Python 3 and g++ with AddressSanitizer/UndefinedBehaviorSanitizer.
Compile the actual firmware functions with a small simulated USB/timer layer;
do not copy their implementations into the tests. The ARM build separately
checks their integration with Mbed. No board is needed and no firmware is flashed.
"""

import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def without_comments(text):
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)


def definition(source, signature):
    start = source.index(signature)
    cursor = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[cursor] == "{") - (source[cursor] == "}")
        cursor += 1
    return source[start:cursor]


def generate(source_root):
    main_text = (source_root / "main.cpp").read_text()
    main = without_comments(main_text)
    usb = without_comments((source_root / "USBJoystick/USBJoystick.cpp").read_text())
    dma = without_comments((source_root / "SimpleDMA/SimpleDMA_KL25_46.cpp").read_text())
    allocator = without_comments((source_root / "NewMalloc/NewMalloc.cpp").read_text())
    diagnostics = without_comments((source_root / "CrashDiagnostics.cpp").read_text())
    config = (source_root / "config.h").read_text()
    pieces = [re.search(r"const int MAX_BUTTONS = \d+;", config).group()]
    pieces.append(definition(dma, "void SimpleDMA::irq_handler(void)"))
    pieces.append(definition(allocator, "void *xmalloc(size_t siz)"))
    pieces.append(definition(allocator, "size_t mallocBytesFree()"))
    for signature in ("bool consolePinsFree(", "void initCrashConsole(", "int mbed::minimal_console_putc("):
        pieces.append(definition(diagnostics, signature))
    pieces.append(definition(main, "struct ButtonState") + " __attribute__((packed));")
    pieces.append("ButtonState *buttonState;\nint nButtons;")
    pieces.append(definition(main, "void scanButtons()"))
    for name, prefix in (("JS", "static const uint8_t"), ("KB", "static const uint8_t"),
                         ("OPD", "static const uint8_t"), ("LW", "static const uint8_t")):
        # Older source snapshots have mutable descriptor declarations.
        signature = f"{prefix} reportDescriptor{name}[]"
        if signature not in usb:
            signature = f"static uint8_t reportDescriptor{name}[]"
        pieces.append(definition(usb, signature) + ";")
    for signature in (
        "bool USBJoystick::update(int16_t",
        "bool USBJoystick::update()",
        "bool USBJoystick::kbUpdate(",
        "bool USBJoystick::mediaUpdate(",
        "bool USBJoystick::sendOPD(",
        "bool USBJoystick::sendReply(",
        "bool USBJoystick::updateStatus(",
        "bool USBJoystick::reportRawBytes(",
        "bool USBJoystick::kbIdleExpired()",
        "bool USBJoystick::mediaIdleExpired()",
        "void USBJoystick::callback_request(",
        "void USBJoystick::callback_request_xfer_done(",
        "void USBJoystick::callback_set_interface(",
        "void USBJoystick::callback_state_change(",
        "const uint8_t *USBJoystick::reportDesc(",
    ):
        pieces.append(definition(usb, signature))
    pieces.append(definition(main, "void reportButtonStatus(USBJoystick &js)"))
    pieces.append(definition(main, "class MyUSBJoystick:") + ";")

    # Exercise the actual OPD dispatch, including conversion from joystick units.
    pieces.append(re.search(r"#define JOYMAX \d+", main).group())
    pieces.append("void serviceOPD(USBJoystick &js, int zReported) {\n"
                  "const int x = 100, y = -200, vx = 300, vy = -400, zvReported = -500;\n"
                  + definition(main, "if (js.opdEnabled())") + "\n}")

    # Exercise the main loop's real retry policy, not just the send functions.
    start = main_text.index("        processButtons(cfg);") + len("        processButtons(cfg);")
    end = main_text.index("        // collect diagnostic statistics, checkpoint 6", start)
    pieces.append("void serviceKeys(USBJoystick &js) {\n" + main_text[start:end] + "\n}")

    start = main_text.index("        LedWizMsg lwm;", main_text.index("int main(void)"))
    end = main_text.index("        // collect performance statistics on the message reader", start)
    pieces.append("void serviceCommands(USBJoystick &js, Accel &accel) {\n"
                  + main_text[start:end] + "\n}")
    return "\n".join(pieces)


def generate_accelerometer(source_root):
    main = without_comments((source_root / "main.cpp").read_text())
    config = without_comments((source_root / "config.h").read_text())
    pieces = re.findall(r"const int AccelRange\w+\s*=\s*\d+;", config)
    pieces.append(re.search(r"#define JOYMAX \d+", main).group())
    for signature in ("struct AccHist", "class Accel\n"):
        pieces.append(definition(main, signature) + ";")
    return "\n".join(pieces)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=ROOT,
                        help="Optional source snapshot to check against the same regressions")
    args = parser.parse_args()
    cases = ("disconnect", "keyboard", "buttons", "axes", "commands", "timestamps", "opd_plunger", "dma", "heap", "usb_backpressure", "usb_control", "hid_idle", "button_scan", "console")
    failures = 0
    with tempfile.TemporaryDirectory(prefix="pinscape-regressions-") as directory:
        build = Path(directory)
        (build / "firmware_under_test.inc").write_text(generate(args.source_root))
        # Callback itself is portable; its toolchain header includes CMSIS,
        # but no CPU instructions are needed for these host-side tests.
        (build / "cmsis.h").write_text("#pragma once\n")
        executable = build / "firmware_regressions"
        compiler = shlex.split(os.environ.get("CXX", "g++"))
        firmware_command = compiler + [
            "-std=c++17", "-O1", "-g", "-fsanitize=address,undefined",
            "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
            "-DMBED_CONF_PLATFORM_CALLBACK_NONTRIVIAL=0",
            "-DMBED_CONF_PLATFORM_CALLBACK_COMPARABLE=1",
            "-I", str(ROOT), "-I", str(build),
            "-I", str(ROOT / "mbed-os/platform/include"),
            "-I", str(ROOT / "mbed-os/platform/cxxsupport"),
            str(ROOT / "tests/firmware_regressions.cpp"), "-o", str(executable),
        ]
        subprocess.run(firmware_command, check=True)
        for case in cases:
            result = subprocess.run([str(executable), case], capture_output=True, text=True)
            if result.returncode:
                failures += 1
                print(f"FAIL {case}\n{result.stdout}{result.stderr}")
            else:
                print(f"PASS {case}")
        # Compile the real MMA driver as well as the Accel wrapper against a
        # simulated sensor bus, exercising setup failures and FIFO behavior.
        (build / "accelerometer_under_test.inc").write_text(generate_accelerometer(args.source_root))
        executable = build / "accelerometer_regressions"
        subprocess.run(compiler + [
            "-std=c++17", "-O1", "-g", "-fsanitize=address,undefined",
            "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
            "-Wno-narrowing",  # Match the firmware's legacy register initializers.
            "-I", str(ROOT / "tests/accelerometer_stubs"), "-I", str(build),
            "-I", str(args.source_root / "MMA8451Q"),
            str(args.source_root / "MMA8451Q/MMA8451Q.cpp"),
            str(ROOT / "tests/accelerometer_regressions.cpp"), "-o", str(executable),
        ], check=True)
        for case in ("accel_driver_errors", "accel_timeouts", "accel_empty_reports",
                     "accel_fifo", "accel_recovery", "accel_poll_errors"):
            result = subprocess.run([str(executable), case], capture_output=True, text=True, timeout=10)
            if result.returncode:
                failures += 1
                print(f"FAIL {case}\n{result.stdout}{result.stderr}")
            else:
                print(f"PASS {case}")
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
