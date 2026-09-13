# Panel Calibration

## Observations and Mode Model

The operator's observations are preserved in [note](note). Voltage editing
appears to have three states:

| State | Rotation | Knob press |
| --- | --- | --- |
| Active blinking | Changes the selected digit | Selects one digit left, wrapping |
| Blink stopped, selection retained | First detent only wakes blinking | Selects one digit left |
| Full exit | First detent enters editing at the default digit | Enters editing at the default digit |

The observed voltage boundaries are about 2 seconds and 9 seconds; blinking
is about 1 Hz. Current initially uses separate defaults with these same values,
not an assertion that it behaves identically. Calibration tests each row.

Normal control uses button entry to avoid the wake-only rotation ambiguity.
Every setting operation enters editing before interpreting the number and
decimal position, even for a repeated target. It selects and verifies digits,
then applies direct up/down steps without deliberately wrapping a digit.

`VOLT?` / `CURR?` are passive display queries. `SOUR:VOLT?` / `SOUR:CURR?`
enter editing with output off and return target/limit values. `MEAS:VOLT?` /
`MEAS:CURR?` require output on, a complete fresh row, expiration of the
configured edit-exit guard, and a quiet full blink period. Returned data must
have arrived after the exit guard, not just before it. Unknown or shared
cross-row exit behavior is handled conservatively. A failed query returns no
number and queues a SCPI error; it never substitutes zero.

## Before Recalibration

**Physically disconnect all loads. Full calibration intentionally toggles
output and changes voltage/current targets while discovering pin roles.**
Do not use it to identify arbitrary unknown electrical connections. Confirm
that the interface circuitry is suitable for active-high panel injection,
that Pico pins cannot be exposed to 5 V, and that passive SPI decoding works.
Check output state on the supply itself, not just through software.

Only GP16, GP17, GP18, GP19, GP20, GP26, and GP27 may be driven. This is the
single allowlist in `calibration_candidate_pins`; all seven must map uniquely
to the two encoders' A/B pins, their buttons, and the output button. The
routine tries every button candidate, then all unordered encoder pairs in
both directions. It does not drive other GPIOs or rediscover DIO/CLK/CS.
Do not enlarge the allowlist without checking the hardware and updating the
seven-role discovery algorithm.

Start with nonzero targets away from the display endpoints. Pair discovery
requires factors between 4 and 9995 and uses the least-significant digit for
the intended row. Wrong-pair trials can affect the other row at another digit;
all loads must remain disconnected. Both rows must return to their starting
values for the probe to be accepted. A boundary, clamp, or unexpected carry
can stop calibration rather than being silently accepted.

## Run and Monitor

```text
OUTP OFF
SYST:ERR?
CAL:FULL 1
CAL:STAT?
```

The `1` explicitly confirms that loads have been disconnected. No argument,
`0`, missing/stale display data, or output on rejects the request. The job
starts from known probe defaults, not potentially broken manual settings.
It runs from the main loop, servicing SCPI during waits. Do not send normal
setting commands while it is running; they are rejected. Do not adjust the
physical knobs or attach another controller during calibration.

Poll `CAL:STAT?`. Its nine fields are:

```text
state,"phase",row,lower_us,upper_us,binary_trial_count,"error",saved_record_available,dirty
```

States: 0=idle, 1=pending, 2=running, 3=done, 4=failed, 5=aborted.
Rows: 0=voltage, 1=current, 255=not row-specific. Bounds describe the active
search; while a trial is running the lower field shows its candidate delay.
The final output-button searches use row 0 as a placeholder, not a voltage
measurement. Job failures are reported in this status, not asynchronously
printed on the SCPI transport. `*OPC?` during a busy job fails rather than
incorrectly returning `1`. Afterward it indicates completion, not success.

```text
CAL:ABOR
CAL:STAT?
```

Abort is checked during waits; a short pulse burst finishes before servicing
the next command. Known output-on trials are cleaned up even after abort.
An output-state confirmation failure takes precedence in the status error.
**Always check the panel after abort, communication loss, or any error.**
Software cannot guarantee output-off with broken wiring or lost feedback.
Settings may be partially changed on failure; only configuration is rolled
back. Successful probes are required to restore the values they changed.

A full run is deliberately slow: each timeout trial begins with 22 seconds
of inactivity, and each binary-search candidate is tested three times.
Depending on search results, allow roughly an hour or more. Status queries
and abort remain available during that time. The ordinary serial smoke test
does not launch calibration.

## What Gets Tested

1. Reset timing statistics and observe fresh SPI traffic for one second.
   Measure recognized `0x42` key-scan cadence separately from complete
   17-byte `0xC0` display updates. Missing or strongly irregular cadence fails.
2. After a 22-second idle before each candidate, identify voltage/current
   buttons by new row blink observations, and output by a reversible toggle.
3. Identify encoder pairs and direction by two forward and two reverse
   detents. Require unique mappings, expected row changes, and restoration
   of both rows. Overlapping or ambiguous pairs fail.
4. For each row, observe the default digit and blink period, then search
   encoder phase width, extra inter-detent gap, button press width, and
   release gap. Tests require two actions to be recognized, not just one.
5. Search the wake-only boundary by selecting digit 0, pressing once to
   select digit 3, waiting the trial delay, then issuing one rotation.
   A changed value means still active; an unchanged value means wake-only.
6. Search full exit by using a selection whose next button position differs
   from the measured default digit. After the trial delay, a button either
   advances retained selection or enters at the reset digit.
7. Test both directions of cross-row timer coupling. Activity on the other
   row occurs three quarters of the way through an idle/exit interval; probe
   the first row after another three quarters. Repeat three times.
8. Search output-button press and release widths with reversible toggles.
   Apply results and save to flash only after every group succeeds.

Increasing timing searches must bracket a failing and passing duration. If
the smallest tested duration already works, it is retained as a tested upper
bound, not a fictitious failure. Idle/exit searches must bracket opposite
behaviors. Bisection stops at the configured resolution; encoder phase uses
100 us. Other searches use at least 10 ms or the observed scan period.
Operating pulse timings include an extra resolution step as margin.
Each resulting operating value is verified in three further trials before
it is applied; a passing search boundary alone is not sufficient.

Search ranges: phase 100-10000 us, extra detent gap 0-100000 us, press
1000-200000 us, release 1000-100000 us, blink-idle 0.5-5 s, and full exit
5-20 s. Unsupported ranges, inconsistent repetitions, nonmonotonic responses,
or missing blink feedback fail the job. They are not extrapolated.

Timeout probes reference the button-release edge, excluding the configured
release gap. The exact controller scan at which an action is accepted is not
visible. Stored `*_IDLE_LOW_US`, `*_IDLE_HIGH_US`, `*_EXIT_LOW_US`, and
`*_EXIT_HIGH_US` therefore preserve a bracket expanded for the probe pulse
and scan uncertainty. `*_IDLE_US` uses the conservative active lower bound;
`*_EXIT_US` uses the conservative expired upper bound. These are operating
guards, not claims of microsecond-accurate internal timer measurements.

## Query and Change Configuration

```text
CAL:LIST?
CAL:CONF?
CAL:PAR? "V_IDLE_US"
CAL:PAR "V_IDLE_US",2000000
CAL:PAR? "V_IDLE_LOW_US"
CAL:SAVE
CAL:LOAD
```

`CAL:LIST?` returns names in the same order as the numeric values from
`CAL:CONF?`. Names are case-insensitive; durations are microseconds. Integer
setters reject invalid ranges, reserved pins, duplicate mappings, impossible
idle/exit ordering, or feedback shorter than two blink periods. Manual
changes, loads, and saves require fresh feedback, output off, and no active job.
Read-only queries remain available during calibration.

Pin assignments are `V_A`, `V_B`, `V_BUTTON`, `I_A`, `I_B`, `I_BUTTON`, and
`OUTPUT_BUTTON`. Assigning a pin already used by another role swaps the two
roles atomically, so a permutation can be changed without a duplicate-pin
intermediate state. All candidate lines are released low on configuration
changes.

Each row has `*_PHASE_US`, `*_GAP_US`, `*_PRESS_US`, `*_RELEASE_US`,
`*_IDLE_US`, `*_EXIT_US`, `*_BLINK_US`, and `*_DEFAULT_DIGIT`.
Output has `OUTPUT_PRESS_US` and `OUTPUT_RELEASE_US`. Shared settings are
`SCAN_US`, `REFRESH_US`, `FEEDBACK_US`, and `RESOLUTION_US`.

`V_TO_I_COUPLING` means voltage-button activity affecting current timers;
`I_TO_V_COUPLING` is the reverse. Values: 0=unknown, 1=both independent,
2=both shared, 3=only blink-idle shared, 4=only edit-exit shared.
The full test specifically probes cross-row button activity; whether rotary
activity has exactly the same coupling remains a hardware question.

`TESTED_MASK=127` after a successful full run records these completed groups:
bit 0=buttons, 1=encoders, 2=bus cadence, 3=voltage timings,
4=current timings, 5=cross-row timers, 6=output timings. Manual changes clear
the mask. Timeout brackets retain the previous measurement until another
full run, so they must not be interpreted as proof of a manually edited value.
Manually writing the mask does not establish hardware validation.

## Flash Persistence

Two records occupy the last two 4096-byte flash sectors, sized according to
the selected Pico board. Each has a magic number, schema version, generation,
configuration, and CRC-32. The older slot is erased and replaced; the current
valid slot remains intact until the new write verifies. Startup selects the
newest valid record, including generation wraparound, or uses factory defaults
with `saved_record_available=0` if neither is valid.

Saving uses SDK multicore flash lockout. Capture pauses during erase/program
and can reject the interrupted transaction; it resumes on following traffic.
An unchanged saved configuration is not rewritten. If the firmware reaches
the storage area, storage reads/writes are refused rather than erasing code.
The region is protected by this runtime check, not a custom linker partition.
Do not use another subsystem that writes those sectors. Whole-flash erase
removes calibration; firmware updates normally writing only the image should
leave it alone, but verify the flashing tool's behavior.

## Unresolved Hardware Questions

- `0x42` is treated as the key-read command based on the assumed TM16xx-style
  protocol. Confirm it in a real capture. `SYST:DISP:CAD?` returns scan count,
  scan min/max, and full-display min/max. If it stays zero, do not infer a
  10 ms scan from unrelated CS transitions; calibration will refuse to proceed.
- Display segment mapping, output-status bit, active-high injection,
  least-significant-bit-first sampling, and clock phase remain the existing
  protocol assumptions. Full calibration does not rediscover those.
- Button recognition may depend on scan phase, long-press behavior, or a
  device-specific debounce rule. The current model tests paired actions and
  requires three matching outcomes; unstable boundaries need manual review.
- A first visible blink is not an explicit edit-mode flag. Actual-reading
  guards are intentionally conservative, but an undocumented display mode,
  different default blink period, or external simultaneous input could still
  invalidate the model. Confirm with a logic analyzer and meter.
- Cross-row coupling could differ for buttons and rotation, or vary with
  output state. Calibration tests buttons with loads disconnected and output
  off except for explicit output-button trials.
- Physical voltage/current ratings and clamp behavior are unknown. The
  firmware validates displayed range, not a model-specific electrical limit.
- Host tests exercise a two-row simulator, not the real supply. They cover
  swapped/reversed pin mappings, independent/shared timers, current control,
  wake-only behavior, actual-reading guards, abort-output cleanup, lost
  capture, CRC/generation handling, and flash-save failure. Hardware testing
  is still required before enabling a connected load.