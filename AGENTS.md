# Pinscape Controller: agent handoff

This file records the project state and the hardware debugging work as of
2026-11-02. Use it to resume work, then check the current source and Git status:
commit IDs, build measurements, and outstanding findings below are snapshots.
New user instructions take precedence over this handoff.

## Start here

- This is virtual pinball firmware for the **FRDM-KL25Z**, using a KL25Z
  Cortex-M0+ with **128 KiB flash and 16 KiB RAM**. It provides cabinet inputs,
  accelerometer nudging, plunger support, and output control.
- The original firmware used old Mbed libraries and the online toolchain. This
  branch migrates it to **Mbed OS Community Edition, GCC ARM, and CMake**. An
  Open Pinball Device (OPD) HID interface was added afterward. The user wants
  reviews to cover the library migration onward, not only the OPD commits.
- The latest firmware the user confirmed working is
  **`Pinscape_Controller_NudgeFix_Release.bin`**. Keep it as a reference.
- The original **77-byte joystick HID descriptor is the only implementation**.
  It restored Windows axis movement on this board. Preserve its wire layout,
  logical range, and neutral values when changing USB reporting.
- The `mbed-ce-port` branch carries the imported migration and OPD history plus
  the local firmware fixes, diagnostics, tests, and this handoff. Check Git
  status before editing and preserve any new uncommitted work. Do not reset,
  clean, or overwrite local firmware artifacts as part of routine setup.
- The latest user hardware feedback was **“this works”** after the release
  nudge fix. There is no unresolved nudge failure reported after that point.
  There are other known review findings, listed below; this document does not
  authorize fixing all of them automatically.

## User context and prior decisions

- The user's missed-event concern is **cabinet buttons**, including flippers
  and coin inputs. They have no mapped joystick buttons but do use nudging.
  Joystick axis reports are still needed with that configuration.
- The plunger answer was “No plunger sensor / not sure”; do not assume a
  particular sensor or a large sensor buffer is installed.
- The user asked about nonblocking keyboard transfers, an event queue's RAM
  cost, and measuring latency on real hardware. No keyboard transition queue
  was implemented. No precise end-to-end hardware latency benchmark was made.
- The user requested production firmware without debugging after the startup
  crashes were resolved. Keep production UART diagnostics disabled by default.
- Two reviews used numbered findings. In an earlier review, **issue 2 was the
  keyboard event queue**, and the user said to fix everything except that issue.
  In the latest eight-item review, **issue 2 was the TLC59116 pointer bug**; the
  user requested fixes for **issues 1 and 3 only**. Do not confuse these lists
  or treat a selective request as approval for every remaining finding.
- The user specifically asked to consider the UART discussion at
  <https://github.com/mjrgh/Pinscape_Controller/pull/4>. Consult it when making
  further UART/pin-reservation changes; saved cabinet configurations matter.
- Windows controller-axis movement and the final firmware were confirmed on
  hardware. VPX, Future Pinball, OPD client behavior, every plunger type, and
  every expansion board were **not** all validated by those confirmations.

## Repository and dependency state

At this handoff:

| Item | Value |
| --- | --- |
| Workspace | `/home/bremen/coding/arm/Pinscape_Controller` |
| Branch | `mbed-ce-port` |
| Last imported port commit | `a9b73bd` — Report the accelerometer scale and firmware version where they belong |
| Root origin / user's fork | `https://github.com/bremen79/Pinscape_Controller.git` |
| Imported port remote (`port-source`) | `https://github.com/Le-Syl21/Pinscape_Controller.git` |
| Original project | `https://github.com/mjrgh/Pinscape_Controller` |
| Pre-port comparison baseline used in reviews | `41e746b` |
| OPD addition | `a61d629` |
| Mbed checkout | `mbed-os/`, origin `https://github.com/mbed-ce/mbed-os.git`, HEAD `c75bace4` |
| Mbed configuration version | Development version `7.1.99` |
| Firmware version header | `Version.h`, version `2.1.0` |

`mbed-os/` is an ignored, local **nested Git checkout**, not a registered root
submodule. There is no root `.gitmodules`; the nested checkout was clean at
handoff. Do not assume cloning this root repository automatically supplies it.
Reproduce against the recorded Mbed revision before considering an upgrade.

The fixes touch `CMakeLists.txt`, `main.cpp`, `config.h`, `mbed_app.json5`,
`MMA8451Q/MMA8451Q.{cpp,h}`, `NewMalloc/NewMalloc.cpp`,
`SimpleDMA/SimpleDMA_KL25_46.cpp`, `USBJoystick/USBJoystick.{cpp,h}`, and
`OpenPinballDevice.h`. **`CrashDiagnostics.cpp`, the four test source files in
`tests/`, and this handoff belong in version control too.**

Build directories, generated firmware binaries, Python bytecode, the historical
patch export, and the two diagnostic photos are local artifacts excluded by
`.gitignore`. They may exist in this workspace but will not accompany a clone.
The confirmed binary hashes and historical diagnostic evidence below therefore
describe this workspace's retained artifacts, not files supplied by the fork.

`changes-since-last-commit.patch` is a historical export. It predates the latest
accelerometer and HID descriptor fixes and is **not the current complete diff**.
`git diff` alone also excludes untracked files. Check `git status --short`.
The inherited ignore rules are supplemented with explicit build/debug exclusions.

## Build instructions

Tools used successfully: CMake, Ninja, Python 3, host `g++` with ASan/UBSan, and
Arm GNU Toolchain **15.3.Rel2**, reporting GCC `15.3.1 20260627`. ARM executables
are installed under `/home/bremen/opt/arm-gnu/bin/` in this workspace environment.
Ensure that directory is in `PATH` if the toolchain is not found.

Production configuration:

```sh
cmake -S . -B build/descriptor-cleanup-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DPINSCAPE_CRASH_DIAGNOSTICS=OFF \
  -DPINSCAPE_NUDGE_DIAGNOSTICS=OFF \
  -DMBED_GENERATE_VSCODE_CONFIG=OFF \
  -DMBED_MANAGE_SUBMODULES=OFF
cmake --build build/descriptor-cleanup-release -j2
```

Outputs are `build/descriptor-cleanup-release/Pinscape_Controller.bin`, `.hex`, `.elf`,
and `.elf.map`. Preserve the matching ELF/map when giving a binary to the user;
crash addresses must be decoded against the exact build that produced them.
Give new candidate binaries distinct names so the confirmed release remains
available. An embedded build date can change a rebuilt binary's checksum.

For bench diagnostics, use a separate build directory:

```sh
cmake -S . -B build/descriptor-cleanup-diagnostics -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DPINSCAPE_CRASH_DIAGNOSTICS=ON \
  -DPINSCAPE_NUDGE_DIAGNOSTICS=ON \
  -DMBED_GENERATE_VSCODE_CONFIG=OFF \
  -DMBED_MANAGE_SUBMODULES=OFF
cmake --build build/descriptor-cleanup-diagnostics -j2
```

Nudge diagnostics require crash diagnostics for the guarded serial console.
For crash logging alone, leave `PINSCAPE_NUDGE_DIAGNOSTICS=OFF`.

Build traps and constraints:

- Target defaults are `PINSCAPE_KL25Z` and `GCC_ARM`. The custom target derives
  from KL25Z and enables `USBDEVICE`.
- Use **Release**. Mbed's default Develop profile previously overflowed flash.
  Release uses size optimization; the project also enables LTO and stack-usage
  reports. Do not replace all compiler flags to add a diagnostic flag.
- The old root `build/CMakeCache.txt` had a stale `/home/bremen/Coding` path
  with different capitalization. Prefer a fresh named build directory.
- The application is **bare metal**, with no RTOS, and has a **3072-byte
  (`0xC00`) boot stack**. Do not reduce this based only on static RAM totals.
- `target.c_lib: "small"` selects newlib-nano. It recovered flash/RAM; no
  main-loop speedup from that change was measured. It does not add an FPU.
  Minimal printf floating-point and 64-bit formatting are disabled separately.
- The top **2048 flash bytes** are reserved for saved configuration. Current
  `sizeof(NVM)` is 1212 bytes, occupying two 1 KiB sectors. If NVM changes,
  revisit this reservation and `cmake/check_flash_budget.cmake` together.
- The KL25Z linker marks `.heap` as loadable. CMake deliberately removes it
  from `.bin` and `.hex` using `objcopy --remove-section=.heap`. Do not flash
  a manually generated image that accidentally includes this section.
- USB PHY patches are applied to a generated copy under
  `build/<configuration>/mbed_patched/`. Edit `cmake/patch_usb_phy.cmake` or
  project configuration, not the generated copy or the nested Mbed checkout.
- The patches supply missing includes, allow EP3 interrupt transfers, and
  handle an older hard-coded EP3 packet size. **Current Mbed uses
  `target.kinetis-max-packet-size-ep3: 64` in `mbed_app.json5`** for its buffer
  size. Removing that setting can bring back two 1023-byte buffers. Some
  patch-file comments describe the older header rather than this checkout.
- `mbed_Fixes/` is intentionally excluded: its obsolete ticker workaround
  must not replace the already corrected Mbed implementation.
- `NewMalloc/OpNew.cpp` is excluded because Mbed CE provides new/delete through
  wrapped malloc/free. GNU `.S` versions of the IAP and edge-scan routines are
  built; neighboring ARM Compiler `.s` files are retained for comparison.

## Confirmed artifact and memory evidence

**`Pinscape_Controller_NudgeFix_Release.bin`**:

```text
Size:   111456 bytes
SHA256: cb028bedf0dd03285724fe56800577b5001afc2991927971e43bb18e4dd09b76
Build:  build/nudge-fix-release/
Flags:  Release; CRASH_DIAGNOSTICS=OFF; NUDGE_DIAGNOSTICS=OFF
```

For this confirmed artifact, static data plus BSS was **6573 bytes**; the aligned RAM-bank
report was **6701 / 16384 bytes**. The LTO stack report gave **2080 bytes for
`main` alone**. Linker symbols were:

```text
__end__      = 0x20000c28
__HeapLimit  = 0x20002400
__StackLimit = 0x20002400
__StackTop   = 0x20003000
```

Flash headroom below the saved-config area was **17568 bytes**. Static RAM
totals exclude runtime allocations and stack usage; the main frame excludes
deeper calls and interrupt frames. Neither number proves a worst-case RAM
margin for every cabinet configuration. Use the Mbed memory report, map file,
linker symbols, `.su` files, and stripped binary size together. GNU `size`
alone gave misleading section totals for this ELF because of `.heap` and
section classifications.

The release's **77-byte joystick descriptor** was compared byte-for-byte with
the hardware-working legacy diagnostic image. Release ELF checks found no
nudge logger, crash-console initializer/startup-memory logger, or serial
init/baud/putc symbols; the nudge log strings were also absent.

Current source release build (2026-11-02):

```text
Binary: build/descriptor-cleanup-release/Pinscape_Controller.bin
Size:   111464 bytes
SHA256: dd4e0798a6ecae83e60c90da97db85d4d69dd2fc5a8ff4f7598280faa4f7a79b
```

This build passed the host tests and ARM build; it has not been tested on the
board. Its 77-byte joystick descriptor was extracted from the ELF and verified
byte-for-byte against the confirmed release. Static data plus BSS is 6501 bytes,
the aligned RAM-bank report is 6629 bytes, the main stack frame is 2072 bytes,
and flash headroom below configuration is 17560 bytes. The stack reservation
remains 3072 bytes. The confirmed root-level binary and its ELF were preserved.
The diagnostic configuration also built successfully in
`build/descriptor-cleanup-diagnostics/` with both diagnostic options enabled.

Other artifacts are historical, not interchangeable recommendations:

| Artifact | Evidence/status |
| --- | --- |
| `Pinscape_Controller_NudgeTest_Legacy.bin` | User confirmed Windows axes move; diagnostics enabled. SHA256 `46c4af0ac36367c7383160a2209a72cd59ac208fe634d027abec2ecb510286b1`. |
| `Pinscape_Controller_AccelFix_Release.bin`, `Pinscape_Controller_AccelFix_Diagnostics.bin` | Earlier accelerometer fixes, before the descriptor compatibility fix. |
| `Pinscape_Controller_Reviewed_Release.bin`, `Pinscape_Controller_Reviewed_Diagnostics.bin` | Earlier review builds. |
| `Pinscape_Controller_MemoryFix.bin` | User previously confirmed startup worked after memory fixes. |
| `Pinscape_Controller_FastPress.bin`, `Pinscape_Controller_CrashDiagnostics.bin`, `Pinscape_Controller_DMAFix.bin`, `Pinscape_Controller_Release.bin`, `Pinscape_Controller_original.bin` | Earlier investigation/baseline files; names alone do not establish current correctness. |

## Source map

| Location | Responsibilities |
| --- | --- |
| `main.cpp` | Startup/main loop, `MyUSBJoystick`, button processing, `Accel`/`AccHist`, `PlungerReader`, config protocol, output factories, sensor selection. |
| `config.h`, `USBProtocol.h` | Saved configuration/defaults and host configuration messages. |
| `USBJoystick/USBJoystick.{h,cpp}` | HID descriptors/reports, endpoints, USB callbacks, keyboard/media/joystick/OPD transport. |
| `OpenPinballDevice.h`, `Version.h` | OPD wire layout/string and firmware version. |
| `MMA8451Q/` | Physical accelerometer driver. |
| `NewMalloc/` | Permanent-allocation arena and heap diagnostics. |
| `SimpleDMA/`, `AltAnalogIn/`, `FastInterruptIn/`, `TinyDigitalIn/`, `NewPwm/` | KL25Z peripheral helpers. |
| `Plunger/`, `TCD1103/`, `TSL14xx/`, `AEAT6012/`, `VCNL4010/`, `VL6180X/` | Plunger/sensor implementations. |
| `TLC5940/`, `TLC59116/`, `74HC595/`, `IRRemote/` | Output expansion and infrared protocols. |
| `CrashDiagnostics.cpp` | Optional guarded UART, startup-memory reporting, fatal-error hook. |
| `CMakeLists.txt`, `mbed_app.json5`, `cmake/`, `custom_targets/`, `mbed_ce_compat.cpp` | Port/build configuration and compatibility glue. |
| `tests/` | Host regression runner, source-extraction fixtures, simulated hardware. |

## Input timing, USB behavior, and the 4096 fix

Cabinet buttons are sampled using a `Timeout` rearmed for **1000 microseconds**.
An active-low press is accepted immediately on a scan; release requires five
consecutive high samples, approximately 5 ms. This is a periodic scan, not an
edge queue and not a hard guarantee that every pulse is captured.
`ButtonState::physState` is a separate `volatile uint8_t` so the ISR and main
loop do not perform competing read/modify/write operations on one bitfield
storage unit. The logical/pulse-state bitfields remain main-loop owned.

The main loop builds the latest keyboard state. Keyboard/media transfers use
nonblocking sends and retain their changed flag until accepted; HID idle
resends are implemented. **There is no transition queue.** A press and release
can still coalesce before the main loop or endpoint accepts a report. USB
transport retries cannot reconstruct an event that was never queued. Send
acceptance also does not mean the host application has processed the event.
The keyboard uses the usual six non-modifier key slots.

The LedWiz command-draining loop has a **5 ms budget**, limiting continuous host
traffic's hold on the main loop. Individual handlers can still take time.
`sendReply` has a **100 ms bound** and fails promptly when disconnected/timed
out; ordinary joystick, keyboard, media, and OPD updates use nonblocking sends.
No joystick button mappings does **not** disable joystick updates when axes
are enabled: nudging still needs them.

Preserve the Mbed CE suspend callback, deferred reconnect handling, and
address-zero recovery guard in `MyUSBJoystick`. USB review fixes also cover
per-interface descriptors, HID idle/protocol handling, and control-report
direction/buffer validation. These paths are tested with host fixtures, not
a complete emulation of the Mbed USB stack.

### Standard joystick

- Reports are **22 bytes, unnumbered**, with 16-bit status at offset 0, a
  reserved word at 2, 32 button bits at 4, and seven signed 16-bit axes at
  offsets 8 through 20: X, Y, Z, Rx, Ry, Rz, Slider.
- The logical axis range is **-4096 to +4096**, with zero neutral.
  `JOYMAX` in `main.cpp` remains **4096**.
- Configured axis formats select XYZ, Rx/Ry/Rz, or the expanded format with
  acceleration/raw plunger, velocity axes, and processed plunger slider.
- The fixed descriptor omits physical-unit metadata and is stored as constant
  data. Its bytes match the original descriptor confirmed working on Windows.
  Descriptor compatibility changes require host testing as well as layout tests.

### Open Pinball Device

- This is a separate HID interface, not a replacement for keyboard/joystick.
  Current construction includes it only in the composite **`useKB` path**;
  a configuration without that interface does not automatically expose OPD.
- Report ID is **1**, followed by a **28-byte payload** (29 bytes on the wire).
  Identification string is **`OpenPinballDeviceStruct/1.0`**. The HID report
  descriptor's `STRING INDEX` uses `STRING_OFFSET_ICONFIGURATION`, served by
  `string_iconfiguration_desc()`. This reuses Mbed's configuration-string slot;
  the USB configuration descriptor's actual `iConfiguration` remains zero.
- Payload offsets: 64-bit microsecond timestamp at 0; 32-bit generic and
  pinball button masks at 8 and 12; signed 16-bit acceleration X/Y at 16/18,
  velocity X/Y at 20/22, plunger position/speed at 24/26. Writers explicitly
  encode little endian.
- The timestamp uses `ticker_read_us(get_us_ticker_data())`, preserving the
  wider time across a 32-bit microsecond wrap.
- The **4096 fix** scales OPD plunger position from internal joystick units
  to the OPD endpoint of **32767**, using 32-bit intermediate multiplication.
  It is not a change of every joystick/nudge axis to a 32767 range.
- The main loop currently supplies **zero for both OPD button masks**.
  Acceleration/velocity use the existing internal values; do not claim this
  establishes every client's expected physical-unit interpretation.
- The original user question concerned VPX and Future Pinball compatibility.
  Separate standard HID support from native OPD support, and verify current
  simulator documentation if asked. No blanket simulator compatibility was
  established by the Windows controller test.

## Accelerometer safeguards already implemented

The board's MMA8451Q uses **PTE25 SDA**, **PTE24 SCL**, and address **0x1D**
(shifted left for the Mbed I2C API). It is configured for a 400 kHz bus,
800 Hz sampling, and the 32-sample circular FIFO.

- Driver operations return checked success/failure. Multi-axis reads only
  publish outputs on success. FIFO-count reads return `-1` on bus failure or
  invalid counts greater than 32.
- Reset allows 1 ms before polling. Reset/standby polls have a 10 ms deadline
  and 100 microsecond waits. The underlying HAL transaction has its own finite
  poll count; the outer deadline is not a strict bound on the entire call
  including blocking I2C operations.
- `Accel::poll()` snapshots FIFO count and reads at most that many samples,
  bounded by 32. A continuously refilling FIFO cannot trap the main loop.
- Lack of fresh samples for 100 ms, or identical samples for 2 seconds,
  triggers recovery. Failures mark the sensor unavailable and clear cached
  acceleration/velocity to neutral.
- Recovery is throttled to once per second using unsigned ticker differences.
  Before placement-new reconstruction, the destructor runs to release Timer
  deep-sleep locks. Recovery performs bus clearing and preserves centering.
- `range_` and filters/history/cache are initialized/reset. The driver supports
  deferred initialization so the `Accel` wrapper does not initialize twice.
- `Accel::get()` with no new samples returns the last valid report, or zero
  before a valid sample/after a fault, without dividing by zero or adding
  another stale sample to history.

The later hardware nudge failure was resolved by the HID descriptor change.
Do not loosen I2C checks based on earlier unconfirmed reset/NACK theories.
This user's sensor worked with old firmware and again with the final build;
the upstream warning about some newer boards lacking an accelerometer does
not explain this resolved case.

## Crash history, memory allocation, and diagnostics

The initial FastPress image failed immediately after flashing, even with only
the programming USB attached. Four slow red flashes followed by four rapid
ones indicated a generic fatal halt, not a specific missing USB or plunger.

- **`photo1.jpg`** shows a HardFault with PC `0x0`, LR `0x260F`, and SP
  `0x200027B8`. The earlier investigation traced this to invoking an empty
  Mbed CE `Callback` in the DMA IRQ handler. The handler now checks the
  callback before calling it and still clears the DMA interrupt flags.
- **`photo2.jpg`** shows another HardFault, PC `0x671A`, LR `0x34E9`, SP
  `0x20002720`. The earlier investigation identified stack/heap overlap with
  Mbed CE's larger main frame. The fixes increased the stack reservation,
  bounded the allocator with `__HeapLimit`, and used the smaller C library.
- These are historical addresses. Use the **matching historical ELF** when
  interpreting them, not the latest release ELF. Temporary logs under `/tmp`
  from earlier work are not a durable source of evidence.

`NewMalloc` is a permanent arena, reached through linker wrappers for
malloc/free. It probes `__real_malloc` once, bounds the remaining pool by
`__HeapLimit`, checks alignment overflow, and rounds allocations to four-byte
boundaries. **`free` intentionally does nothing**; exhaustion halts. Repeated
allocate/free patterns in the main loop or recovery would consume the arena.
Do not assume ordinary desktop allocator behavior or add recurring allocations
without accounting for this design.

Serial/flashing guidance for this user's board:

1. **Programming/OpenSDA USB** supplies the `FRDM-KL25Z` drag-and-drop drive
   and the debugging serial connection. Windows showed **PEMicro/Freescale
   CDC Serial Port**; the separate **PEMicro/Freescale Debug App** entry is
   not the serial port. COM numbers vary; the photos used COM4.
2. **Normal/target USB** carries joystick, keyboard, and OPD HID traffic;
   it does not expose this diagnostic COM port.
3. Flashing and serial diagnostics alone use the programming port. Connect
   both ports when also testing game-controller USB reports. Two computers
   can handle those separate host connections; neither task inherently
   requires two computers.
4. For a diagnostic image, open its serial COM port in PuTTY at **115200,
   8 data bits, no parity, 1 stop bit, no flow control**, then reset the board.
   For a full power cycle, disconnect both USB power sources.

Production does not initialize the UART. Diagnostic builds check **saved
configuration pin assignments** before enabling it, including button, output,
plunger, calibration, IR, TV-ON, and expansion assignments that could occupy
PTA1/PTA2. Serial output is intentionally suppressed if the pins are in use.
`TEST_KEEP_PRINTF=1` reserves pins in factory defaults; it does not by itself
enable release logging or override saved configurations.

Nudge diagnostic builds print sensor/center/report/sample-age information and
completed joystick-transfer counts roughly once per second. I2C failures
include the operation/register/result. Transfer counters are updated in the
USB callback; printing happens in the main loop. Logging changes timing, so
use it for diagnosis rather than production latency measurements.

For the Windows axis test, gentle tilt held briefly is easier to observe than
a very short bump. Reconnect the normal USB after descriptor changes so the
host enumerates the new descriptor. Distinguish raw sensor movement, queued
USB reports, completed transfers, and host-visible axis movement when debugging.
Likewise, a button latency measurement must distinguish GPIO edge, firmware
processing, USB submission/completion, and host receipt; none of those alone
measures all the others.

## Known remaining findings

These are findings from the last eight-item review, retained for future scoped
work. Items **1 (accelerometer bounded polling/recovery)** and **3 (empty nudge
sample division)** were fixed. The following were not included in that fix
request and remain in the source at handoff:

| Review item | Location and trigger | Consequence / intended investigation |
| --- | --- | --- |
| 2 | Output factory in `main.cpp`: `LwOut *lwp` is not assigned for `PortTypeTLC59116` when `tlc59116 == 0`. | Later wrapping or `lwp->set(0)` uses an uninitialized pointer. Check a virtual-output fallback if this is selected for repair. |
| 4 | `USBJoystick::callback_state_change`: pending keyboard resend state is cleared on disconnect; configuring again does not force the current held-key state to be sent. | A held key with no new change and idle interval zero can be missing after reconnect. |
| 5 | `PlungerReader::onUpdateCal()` divides by `cfg.plunger.cal.max - cfg.plunger.cal.zero`. | Invalid/equal calibration endpoints can divide by zero; validate range and define fallback behavior. |
| 6 | Night-mode checks in `processButtons()` compare the configured button with `i + 1`, the compact live-button index. | Sparse button configurations address the wrong button; examine `bs.cfgIndex + 1` instead. |
| 7 | `tReportPlungerStat` in `main.cpp` is `uint8_t` but receives a microsecond timestamp. | Truncation can expire requested extra exposure immediately/early. The separate duration byte `reportPlungerStatTime` is a different field. |
| 8 | Roku handler in `IRRemote/IRProtocols.h`: `tclRokuPrvCode == code ^ 0x80800000`. | Equality binds before XOR; intended comparison needs parentheses around the XOR expression. Distinct commands can be discarded. |

The earlier keyboard event-queue issue is separate from this table and also
remains unimplemented. Revalidate findings against the current revision before
editing. Passing the current regression suite does not mean these cases have
been repaired or are all covered.

## Verification workflow and limits

For firmware changes, run the host suite and build the relevant ARM image:

```sh
python3 tests/run_regressions.py
cmake --build build/descriptor-cleanup-release -j2
```

The **last firmware validation passed 20 host cases**: disconnect, keyboard,
buttons, axes, commands, timestamps, OPD plunger conversion, DMA, heap, USB
backpressure, USB control requests, HID idle, button scanning, console guard,
and six accelerometer error/timeout/empty-report/FIFO/recovery cases. This was
validated on 2026-11-02, not a claim that every later edit has been tested. Documentation-only changes do not require rebuilding firmware.

The runner extracts actual function definitions from firmware source, compiles
them with simulated USB/timer/peripheral support, and uses ASan/UBSan. It also
compiles the real MMA8451Q driver and extracts the full `Accel` class against
a simulated sensor bus. Non-PIE host binaries avoid prior ASan layout issues.
Extraction depends on signatures and some comment boundaries; update the
runner if refactoring those locations. `--source-root` can test compatible
source snapshots, but arbitrary old snapshots may lack the expected APIs.

These tests do not model all of Mbed, physical I2C/USB timing, Windows HID
interpretation, or simulator input handling. Passing a descriptor layout test
alone does not establish host compatibility.

After changes affecting RAM, inspect the map, `.su` output, heap/stack symbols,
and flash-budget result. A successful link alone did not prevent the earlier
stack collision. Known build warnings have included legacy register-initializer
narrowing, a library `_fstat` stub, and an RWX segment; do not describe a build
as warning-free without checking its output. The uninitialized `lwp` warning
corresponds to a real outstanding finding, not harmless noise.

Most legacy firmware sources use **CRLF**; CMake, newer diagnostic code, and
tests generally use LF. Preserve existing file endings to avoid huge unrelated
diffs. For whitespace review of mixed legacy files, use:

```sh
git -c core.whitespace=cr-at-eol diff --check
git status --short
```

Keep changes scoped to the user's request, preserve the hardware-confirmed
release and uncommitted work, and report separately what was built, host-tested,
and confirmed on the physical board. Update this handoff when those facts change.
