# Testing on Real Hardware

This procedure tests the actual Pico, interface wiring, and power supply.
Start with no load connected and complete each stage before continuing.
For protocol assumptions and calibration details, see [calibration.md](calibration.md).
For the development-container setup, see [setup.md](setup.md).
For automatic timestamped experiments and logs to return for analysis, see
[hardware_probe.md](hardware_probe.md).

## 1. Prepare the Bench

You need:

- The correct Pico board and a USB data cable.
- The supply's panel interface with suitable signal injection and level translation.
- A multimeter, and preferably a logic analyzer for timing verification.
- A host with Bash, `stty`, and `awk` for the supplied smoke-test script.
- A serial terminal or VS Code serial monitor for individual SCPI commands.

Disconnect all loads and turn the output off using the physical panel. Record
the original voltage and current-limit settings. Choose initial targets within
the supply's rated range; the firmware does not know its electrical ratings.
Do not open or probe mains circuitry as part of this procedure.

Before connecting signals, remove power as appropriate and verify the interface
electrically. Pico GPIO is not 5 V tolerant. Check the grounding/isolation
arrangement before connecting USB, a grounded analyzer, or a bench instrument.
Do not directly connect Pico outputs across unknown panel switches or encoders.

Default connections are GPIO numbers, not physical header pin numbers:

| Function | Pico GPIO | Direction at Pico |
| --- | --- | --- |
| Display DIO | GP28 | Input |
| Display CLK | GP21 | Input |
| Display CS | GP22 | Input |
| Voltage encoder A / B | GP19 / GP20 | Output through interface |
| Voltage knob button | GP17 | Output through interface |
| Current encoder A / B | GP27 / GP26 | Output through interface |
| Current knob button | GP18 | Output through interface |
| Output button | GP16 | Output through interface |

Saved calibration can reassign the seven panel-control pins. Check those
assignments before sending setting commands. Even during read-only tests,
firmware startup configures these seven lines as outputs held low; the device
is not an electrically passive logic analyzer.

## 2. Build and Flash

1. Open the repository in its configured VS Code development container.
2. Select the correct Pico board/toolchain in the CMake configuration. The
   board choice determines flash size and therefore calibration storage location.
3. Use **CMake: Build Target**, select `spi_listner`, and require a successful build.
4. Disconnect the Pico USB cable, hold BOOTSEL, and reconnect it. Release BOOTSEL
   when its mass-storage drive appears.
5. Copy [build/spi_listner.uf2](build/spi_listner.uf2) to that drive. The Pico
   should reboot and enumerate as a USB serial device.

Use the UF2 produced by this build, not an older downloaded image. A whole-flash
erase removes saved calibration; an ordinary UF2 update should not erase the
last two sectors unless the flashing tool explicitly does so.

On Windows/WSL, pass the Pico through to WSL using the USB attachment procedure
in [setup.md](setup.md). BOOTSEL and running firmware can enumerate differently;
check attachment again after flashing. Reopen/reload the container if needed.

## 3. Find and Open the Serial Port

In the Linux host/container that will run the test:

```sh
ls /dev/ttyACM* /dev/serial/by-id/ 2>/dev/null
```

Unplug/replug the Pico if necessary to identify its port. The examples below
use `/dev/ttyACM0`; substitute your actual port. If permission is denied, fix
device access on the host/container rather than changing the firmware.

The transport is **USB CDC serial, not USBTMC**. Serial-terminal settings:
115200 baud, 8 data bits, no parity, 1 stop bit, no flow control, and LF/newline
command termination. USB CDC ignores baud rate. GPIO UART stdio is disabled.
Allow at least 120 seconds for ordinary setting-command completion.

Only one program may use the port at a time. Close the terminal before running
the smoke-test script; close the script before reopening the terminal. Enable
terminal logging if available. Send commands one at a time and inspect the
response before continuing; do not paste a block that enables output after
an unchecked setting command.

## 4. Read-Only Smoke Test

From the repository root, with the serial terminal closed:

```sh
bash tests/serial_smoke.sh /dev/ttyACM0
```

It prints identification, display status/timings, displayed voltage/current,
and `Serial smoke test passed`. It does not set targets or toggle output;
it clears and checks the SCPI error queue. Save earlier errors before running it.

Open your serial terminal and check these commands individually:

| Command | Expected result |
| --- | --- |
| `*IDN?` | Starts with `Pico,Powersupply,` |
| `OUTP?` | `0`, agreeing with the physical panel |
| `SYST:DISP:STAT?` | Five integers: accepted frames, frame age, V/I/P ages |
| `VOLT?` | Matches the displayed voltage |
| `CURR?` | Matches the displayed current |
| `SYST:ERR?` | `0,"No error"` |

Read `SYST:DISP:STAT?` twice a few seconds apart. The frame count must increase.
Ages are microseconds; `-1` means that no corresponding valid data has arrived.
They should normally remain small relative to the two-second stale-reading
limit. Blinking may temporarily increase a row's age while the last complete
reading is retained. A constantly invalid power row may indicate a decoder
mapping problem even if voltage and current work.

**Stop here** if output state disagrees with the panel, readings are wrong,
the count does not advance, or a query fails. Calibration cannot fix the
display bit mapping or an unsafe electrical interface.

## 5. Verify Bus and Edit Timing

Query `SYST:DISP:CAD?`. Its five values are:

```text
key_scan_count,scan_min_us,scan_max_us,display_min_us,display_max_us
```

The observations in [note](note) suggest about 10000 us between key scans and
50000 us between full display updates. These are expectations to check, not
hard-coded pass tolerances. The firmware recognizes `0x42` as a key-scan
command and `0xC0` plus 16 data bytes as a full display write. If scan count
stays zero, confirm the protocol with a logic analyzer before recalibration.

With output off and loads disconnected, enter editing using the voltage knob
button. Observe leftward digit selection and wrapping. Repeat for current.
Confirm roughly 1 Hz blinking and record each row separately.

For each row, check that after about two seconds of inactivity a first detent
only restarts blinking and a second immediate detent changes the target.
After more than ten seconds, check whether entry resets selection to digit 2
(middle-right). These actions can change targets; restore safe values before
continuing. Do not assume current behaves like voltage.

`SYST:DISP:TIM?` provides clock high/low widths, CS duration, all-transaction
intervals, and blink measurements; its field layout is in [setup.md](setup.md).
Compare them with an analyzer capture of CS, CLK, and DIO. Software polling
can miss fast edges; a decoded number alone is not proof of accurate timing.

## 6. Test Target Setting with Output Off

Choose values safe for your particular supply. The following example assumes
6.50 V and 9.50 V are within its rating. Otherwise substitute suitable values.
Keep loads disconnected and physically confirm output off before every trial.

With the terminal closed, run these separately:

```sh
bash tests/serial_smoke.sh /dev/ttyACM0 --voltage 9.50
bash tests/serial_smoke.sh /dev/ttyACM0 --voltage 6.50
bash tests/serial_smoke.sh /dev/ttyACM0 --voltage 6.50
```

This covers increasing, decreasing, and repeated targets. Repeat the sequence
at least ten times, watching the panel. Require every trial to pass; do not
accept intermittent success. The script's voltage tolerance is 0.01 V, so
also compare the exact rounded target at your display's native resolution.
It does not turn output on or off.

To test manually in the serial terminal, send `VOLT 6.50`. It has no direct
response. Next send `*OPC?` and wait for `1`, then send `SYST:ERR?`. Continue
only if it returns `0,"No error"`. Finally send `SOUR:VOLT?` and compare the
verified target with the panel. `*OPC?` means completion, not success.

Use the same sequence for current, substituting a safe limit, for example
`CURR 0.750` followed by `*OPC?`, `SYST:ERR?`, and `SOUR:CURR?`.
`SOUR` queries enter edit mode and require output off. Repeat both directions,
repeat an identical target, and test again after more than ten seconds idle.

Where supported by your supply's range, include a carry boundary such as
9.99 V to 10.00 V and back. Do not exceed the supply's ratings just to test
a boundary. On any failure, check the panel: the target can be partially
changed, and the firmware does not automatically roll it back.

## 7. Optional Full Recalibration

Read [calibration.md](calibration.md) completely first. **Disconnect all loads:
this routine intentionally enables output during button discovery/testing.**
Start from sensible nonzero voltage/current targets, away from display and
physical clamp boundaries. Maintain physical access to the supply's controls.

With output already off, check `OUTP?` and the panel, then send `CAL:FULL 1`.
The `1` confirms that loads have been disconnected. Poll `CAL:STAT?` every
few seconds; allow roughly an hour or more. Do not move knobs, send ordinary
setting commands, or disconnect power during the run. `*OPC?` is not a busy
polling command; it fails while calibration is running.

The first status field is 1=pending, 2=running, 3=done, 4=failed, or 5=aborted.
The phase and error fields identify progress or the reason for stopping.
Success requires state 3, an empty error string, and physical output off.
Check `CAL:PAR? "TESTED_MASK"`; a successful full run reports `127`.
Results are saved automatically only after every calibration group succeeds.

Use `CAL:ABOR` to request cancellation, then poll status until it stops and
check the panel. If output remains on or its state cannot be confirmed,
use the supply's physical controls. Do not rely on USB abort as an emergency
output disconnect. Failure restores the previous configuration, not necessarily
the targets changed during probing.

## 8. Verify Flash Persistence

With output off, record `CAL:LIST?`, `CAL:CONF?`, and `CAL:STAT?`. After a
successful calibration they should show a saved record and no dirty changes.
For manual configuration changes, use `CAL:SAVE` and check `SYST:ERR?` first.

Disconnect Pico USB power, then reconnect it, leaving loads disconnected.
If the interface can power the Pico another way, USB removal is not a reboot;
use a controlled reset or remove all Pico power safely. Never interrupt an
active calibration or flash save for this test.

After reconnecting the serial port, compare `CAL:CONF?` with the recorded
values. `CAL:STAT?` should indicate a saved record and `dirty=0`; the job state
itself returns to idle after reboot. Verify the panel settings separately.

## 9. Test Actual Readings and Output Control

Do this only after target setting and output-state decoding are reliable.
Keep the load disconnected for the first output-on test. Set safe targets,
check completion and errors, and connect a voltmeter across the supply output.

Send `OUTP ON`, then `*OPC?`, `SYST:ERR?`, and `OUTP?` one at a time. Require
successful completion, no error, and agreement with the physical panel.
Allow more than both configured `V_EXIT_US` and `I_EXIT_US` after the last
editing activity, with a couple of seconds of margin. Factory defaults are
about nine seconds, so start with at least twelve seconds if unmodified.
Do not touch knobs or send `SOUR` target queries during that wait.

Query `MEAS:VOLT?` and compare with the panel and voltmeter, allowing for
display resolution and the instruments' specified accuracy. `MEAS:CURR?`
should agree with the panel; unloaded current may legitimately be near zero.
Measurement queries can take an additional blink-period observation window.

Send `OUTP OFF`, check completion/errors and `OUTP?`, and confirm the panel
and meter show output disabled. A meter may show residual voltage while output
capacitance discharges. Do not infer safety from a software flag alone.

Only then consider a suitably rated dummy load, installed with output off,
to verify nonzero current measurement. Stay within voltage/current/power
ratings and use appropriate thermal precautions. **Never place a multimeter
in current mode directly across the output.**

## Troubleshooting and Test Record

| Symptom | Next check |
| --- | --- |
| No serial port | USB data cable, BOOTSEL vs running firmware, WSL/container attachment |
| Permission denied / mixed responses | Device permissions; close every other serial client |
| Identification works, no valid frames | CS/CLK/DIO wiring, levels, clock phase, frame protocol |
| Wrong digits or output flag | Display bit mapping; do not run active calibration |
| No scan cadence | Confirm actual key-read command; `0x42` is still an assumption |
| Setting times out / wrong direction | Check error queue and panel; inspect configured pins and pulse timing |
| `MEAS` returns nothing | Check output on, edit-exit guard, cross-row activity, and fresh complete rows |
| Calibration fails | Record the full `CAL:STAT?`; inspect its named phase/error before retrying |
| Values vanish on restart | Saved-record flag, `CAL:SAVE` error, board flash size, flashing/erase behavior |

A failed query can return no number. Wait for the operation or client timeout
before sending more commands, then query `SYST:ERR?` repeatedly until
`0,"No error"`. Avoid continuing a partially executed sequence that could
later enable output. Capture errors before using `*CLS`, which clears them.

For each run, record firmware commit/build, board and supply model, wiring,
calibration values/status, target before/after, panel and meter readings,
elapsed time, and every error. Keep failed trials rather than retrying until
one passes. A passing simulator test or one successful hardware adjustment
does not validate the electrical interface or all supply modes.