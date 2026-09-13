# Install requirements 
- wsl
- vscode
- usbipd

## in wsl
- docker

# Opening project
- Open vscode in wsl at .
- Reopen in devcontainer


# Debugger

connect debugger with target to windows USB 
in windows run ( with ?-? being the id of the debugger (cmsis-dap) ) (bind needs admin)
- usbipd list
- usbipd bind --busid ?-?
- usbipd attach --wsl --busid ?-?
after bind once only attach is required if hardware didnt change
after attach reload window to give container all usb devices 

# Firmware and SCPI

The firmware exposes SCPI over USB CDC serial, not USBTMC. Use the Pico's
USB connector and a serial terminal (115200 baud, no flow control, newline
termination). USB CDC ignores the baud rate. UART GPIO stdio remains disabled.
Only one program should open the serial port at a time.

Display wiring is DIO=GP28, CLK=GP21, CS=GP22. Voltage encoder connections are
A=GP19, B=GP20, push=GP17; output push is GP16. These are GPIO numbers, not
physical header pin numbers. Confirm signal voltage levels and ground before
connecting: Pico GPIO is not 5 V tolerant. Use suitable level translation and
the existing interface circuitry; do not directly drive the supply's panel
signals without checking its electrical interface.

Build the `spi_listner` target with the Pico SDK configured. The flashable
artifact is `build/spi_listner.uf2`. Copy it to the Pico's BOOTSEL drive.

## Commands

| Command | Meaning |
| --- | --- |
| `*IDN?` | Firmware identification |
| `VOLT 6.50` or `SOUR:VOLT 6.50` | Set voltage with output off; wait for verified display feedback |
| `VOLT?` or `DISP:VOLT?` | Last fresh complete displayed voltage, in V |
| `CURR?` or `DISP:CURR?` | Last fresh complete displayed current, in A |
| `DISP:POW?` | Last fresh complete displayed power, in W |
| `OUTP ON`, `OUTP OFF`, `OUTP?` | Set/query output with display confirmation |
| `OUTP:T` | Toggle output (`OUT` remains an alias for `OUTP`) |
| `*OPC?` | Return `1` after preceding synchronous commands finish |
| `SYST:ERR?`, `SYST:ERR:COUN?`, `*CLS` | Read/count/clear queued errors |
| `SYST:DISP:TIM?` | Timing measurements since boot |
| `SYST:DISP:STAT?` | Valid frame count, frame age, V/I/P reading ages |

`VOLT?` and `CURR?` report what the supply displays. Depending on the supply's
front-panel mode this may be the target/limit or the actual reading. The known
frame mapping does not identify that mode, so these are deliberately not
advertised as `MEASure` commands or separately cached setpoint queries.
Current-limit setting is not implemented. Blank/invalid digits preserve the
last complete reading; readings older than two seconds fail instead of
returning a fabricated zero. Failed queries produce no numeric response;
use `SYST:ERR?` after a query timeout to inspect the error.

Voltage-setting requires output off, rounds to the displayed resolution, and
accepts only values fitting the four-digit display at its current decimal
position. No model-specific maximum voltage is assumed. The first encoder
pulse wakes editing; subsequent pulses and digit selections are verified from
new display frames. A missed pulse, unexpected decimal position, or missing
feedback fails the command rather than continuing blindly. Individual feedback
waits are limited to two seconds; a long voltage adjustment can take tens of
seconds. Use a host read timeout of at least 45 seconds. On failure the voltage
may be partially changed; there is no automatic rollback or output enabling.
An error does not cancel later commands already queued by the host.

Example with the output already off:

```text
VOLT 6.50
*OPC?
SYST:ERR?
VOLT?
CURR?
```

Expect `1`, then `0,"No error"`, then the displayed values. `*OPC?` means
completion, not success: always inspect the error queue before enabling output.

## Discover Display Timing

Capture runs continuously on core 1 using rising CLK edges and LSB-first data
while CS is low, matching the existing display protocol. No output pulses are
generated for timing discovery. Query `SYST:DISP:TIM?` after several seconds;
turn the supply's voltage/current knobs manually to expose blinking digits.
The response contains 13 comma-separated integers:

```text
frames,rejected,clk_high_min,clk_high_max,clk_low_min,clk_low_max,cs_min,cs_max,refresh_min,refresh_max,voltage_blink,current_blink,power_blink
```

All durations are microseconds. Clock/CS/refresh values are observed ranges;
blink is the most recently observed interval between blank-phase onsets.
Zero means not yet observed (or below microsecond resolution). `frames` counts
all completed/timed-out CS transactions; `rejected` includes other display
commands, wrong headers/lengths, partial bytes, buffer overflow, and CS low for
100 ms. Only complete 17-byte `C0` display writes update readings. Refresh
intervals cover all CS transactions, not just full display writes.

`SYST:DISP:STAT?` returns accepted frame count and four ages in microseconds;
an age of `-1` means no corresponding valid data has arrived. A rising frame
count and low ages confirm reception. These are software-polled measurements,
not an oscilloscope: sub-microsecond edges or polling gaps can be missed.
Clock phase/bit order and button/encoder pulse widths are not auto-calibrated.
Validate the measured timing against a logic analyzer before relying on it.

## Test Without Hardware

The native CMake configuration needs only a C compiler and CMake, not the Pico
SDK. Configure a separate build directory with `POWERSUPPLY_HOST_TESTS=ON`,
build it, and run CTest. For example, from the repository root:

```sh
cmake -S . -B /tmp/powersupply-tests -DPOWERSUPPLY_HOST_TESTS=ON
cmake --build /tmp/powersupply-tests
ctest --test-dir /tmp/powersupply-tests --output-on-failure
```

Tests exercise the real decoder, capture engine, controller, and SCPI parser.
GPIO replay checks clock/CS/refresh timing, blink periods, bad frames,
overflow, timeout, and timer wraparound. The simulated supply refreshes every
10 ms, blinks every 500 ms, and covers upward/downward transitions, repeated
targets, digit carry boundaries, invalid targets, missed pulses, lost capture,
output interlocks, and SCPI errors. This verifies logic, not the electrical
interface or the supply's actual encoder response.

## Test on the Supply

Attach the USB device to the container as described above, flash the firmware,
and identify its serial device. The smoke test is read-only by default:

```sh
bash tests/serial_smoke.sh /dev/ttyACM0
```

With loads disconnected and output already off, explicitly test setting:

```sh
bash tests/serial_smoke.sh /dev/ttyACM0 --voltage 6.50
```

The script does not toggle output. It verifies completion, the SCPI error
queue, and the displayed target within 0.01 V. Repeat with `9.50`, `6.50`,
and `6.50` to cover both directions and repeated commands. Verify the panel
and an external meter before testing with a load. USBTMC enumeration and
actual hardware timing/control still require separate hardware validation.
