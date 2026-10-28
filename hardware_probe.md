# Collecting Hardware Investigation Logs

[tests/hardware_probe.py](tests/hardware_probe.py) runs on the computer connected
to the **Pico's USB serial port**. It does not talk directly to a supply's own
USB port. It is standalone: the logging computer needs Python 3.10+ and
`pyserial`, not the Pico SDK, C compiler, or the rest of the Python tests.
It works with Linux/macOS serial paths and Windows ports such as `COM5`.

This is an experimental data-collection tool, not just a wrapper around automatic
calibration. It never invokes `CAL:FULL`, writes calibration parameters, saves
flash, or searches alternative GPIO mappings. It uses the currently configured
panel-interface mapping and records that configuration in its log.

## First Update the Pico

Build the `spi_listner` target in VS Code and flash the resulting
[build/spi_listner.uf2](build/spi_listner.uf2). This version adds raw tracing and
diagnostic actions. Old firmware is not sufficient: `DIAG:VERS?` must return `1`.
The identification firmware version is `3`. Flashing steps and wiring precautions
are in [hardware_testing.md](hardware_testing.md).

Only one process may open the serial port. Close serial terminals, the existing
smoke test, and other instrument software before starting this script.

## Install on the Logging Computer

```sh
python -m pip install pyserial
python -m serial.tools.list_ports
```

Use `python3` instead of `python` where necessary. Commands below assume a
repository checkout on the logging computer; if running the script by itself,
use its actual path instead of `tests/hardware_probe.py`.

## 1. Start with a Passive Run

```sh
python tests/hardware_probe.py --port /dev/ttyACM0 --duration 30 --log passive.jsonl --label "supply model and interface wiring"
```

On Windows, replace `/dev/ttyACM0` with the correct port, for example `COM5`.
No panel button or encoder pulses are generated in passive mode. Firmware
startup still configures the seven injection pins low; it is not an electrically
passive logic analyzer. The script clears only the diagnostic trace buffer after
recording metadata. It logs and drains existing SCPI errors rather than silently
discarding them.

Check that raw `C0` display frames and scan traffic are present. Passive data is
useful even if the configured scan command or display decoder is incorrect:
unrecognized transactions are also included. If an old diagnostic fault is
reported, check the panel before acknowledging it or restarting the Pico.

## 2. Run Controlled Button and Encoder Tests

**Physically disconnect all loads. Confirm output off on the panel.** Choose
safe, nonzero voltage/current targets within the supply's ratings, away from
zero and clamps. The script requires a displayed factor between 4 and 9995
when selecting a least-significant digit. The firmware does not know the
supply's electrical ratings.

```sh
python tests/hardware_probe.py --port /dev/ttyACM0 --active --loads-disconnected --log active.jsonl --label "supply model, initial targets, wiring"
```

The two flags are deliberate: active tests can change **both** targets. They
do not enable output by default, but a wrong interface mapping could press the
wrong physical button. The interlock stops queued actions on an unexpected
output-state change; it cannot guarantee output-off with bad wiring.
Keep physical controls accessible and do not touch the knobs during a run.

The initial active plan takes several minutes, depending on display response
and host throughput. It collects:

- Raw SPI before, during, and after every action, including unrecognized commands.
- Voltage and current button presses in active, approximately 2.5-second idle,
  and approximately 12-second idle contexts.
- Rotation probes on either side of the suspected wake-only timeout.
- Button-selection probes below and above the suspected full-edit-exit timeout.
- Short/long button presses and paired presses with different release gaps.
- Paired rotations with different phase widths and inter-detent gaps.
- Activity on the opposite row between a reference action and an idle/exit probe.

Before rotation trials it selects digit 3 from actual blink observations, rather
than deliberately changing a more significant digit. Small up/down reference
actions help create a known timing context. If a reference is ignored, a clamp
is encountered, or no fresh whole-row frame exists, interpretation may remain
inconclusive. Raw evidence is retained regardless.

Targets are **not automatically restored**, even on success. A missed pulse or
an undocumented wake behavior could make a blind rollback unsafe. Check the
physical panel afterward, and restore safe targets yourself with output off.

## 3. Collect Dense Timing Data

After the shorter run completes reliably:

```sh
python tests/hardware_probe.py --port /dev/ttyACM0 --active --loads-disconnected --full --log full.jsonl --max-log-mb 1024 --label "same bench setup as active run"
```

Full mode uses denser candidate timings and three repetitions by default.
It also attempts a separate software-side binary search of the wake-only
boundary, based on the recorded raw experiment results, to a requested
50 ms bracket. It stops that search as inconclusive if the endpoints do not
bracket opposite behavior, repeated outcomes disagree, or complete display
evidence is missing. It does not invoke firmware auto-calibration or change
the stored timing configuration.

Allow roughly an hour or more for a full run. `--repeats 2` changes repetition
count; the shorter plan defaults to one. The script observes the bus throughout
long recovery intervals instead of leaving them unlogged. The approximately
12-second recovery is an experiment condition, not proof that an unknown
current controller has exited editing; check the resulting traces.

## 4. Optional Output-On Context

Only after output-state decoding and the injection mapping have been verified,
and with **all loads still physically disconnected**, this additional flag
allows deliberate output-on tests:

```sh
python tests/hardware_probe.py --port /dev/ttyACM0 --active --loads-disconnected --include-output-on --log output_context.jsonl
```

The script starts with output off, performs the usual active plan, disarms
diagnostics, enables output explicitly, then records button/editing contexts for
both rows. It disables output and checks the reported state afterward. Set
safe targets and use an appropriate voltmeter before this test. Never use a
current-mode meter directly across the output. This option is not needed for
the first logs; output-off data should be collected first.

## What to Return

Each run produces:

- `name.jsonl`: the primary, append-only evidence, with UTC and host-monotonic
  timestamps, requests/responses, configuration, raw SPI bytes, decoded values,
  device action/edge markers, trial results, and errors.
- `name.summary.json`: a convenience summary with per-command transaction
  intervals, action timings, blink observations, and tentative classifications.

Return **both files**, including partial or failed runs. Include the supply model,
interface wiring, initial/final targets, observations from the physical panel,
and any logic-analyzer captures. Large logs can be compressed before transfer.
The JSONL is more important than the summary: we can reanalyze it when the
first interpretation proves wrong. Do not retry until one run passes and discard
the failures; intermittent behavior is part of the evidence.

Existing output files are never overwritten. The default name includes a UTC
timestamp. Default JSONL size limit is 512 MiB; `--max-log-mb` changes it. Reaching
the limit stops probing instead of silently omitting data. A near-limit log may
not have room for a final status record, but prior complete records remain.

Offline summary generation needs neither serial hardware nor `pyserial`:

```sh
python tests/hardware_probe.py --analyze active.jsonl
```

## Timing and Evidence Limits

The trace ring retains 256 events and exposes up to 24 per request. The script
drains it frequently; the default maximum pause between polls is 50 ms.
If the ring overruns, surviving records are logged and the run stops with an
explicit lost-record count. Slow machines, busy USB links, or excessive traffic
may require a smaller `--poll-interval`; inspect the log rather than assuming
capture was continuous.

Actions are queued on the Pico before execution. Delays between actions use
Pico timestamps relative to the preceding action's completion, not the host's
sleep duration. GPIO edges and action start/end are recorded individually.
Actual spacing is also reported: main-loop scheduling, trace traffic, USB
interrupts, GPIO writes, and logging can add jitter. A zero requested gap is
not a claim of zero physical spacing. Use the actual edge records for analysis.
The trace contains sampled bytes, not a waveform of every SPI clock edge.

Raw timestamps are 32-bit microseconds and wrap about every 72 minutes. The
client extends them while preserving originals. Small cross-core publication
reordering is annotated; a large backward jump stops the run as a possible
reboot. Frame start/end times bound CS-low transactions, and edge timestamps
are close software observations of GPIO writes, not hardware-latched samples.

Recorded display values can be cached across blank/invalid digits. `valid_mask`
means a row has had valid data before; `fresh_mask` means it decoded in this
specific display frame; `blink_mask` means a single blank digit was observed
in that frame. Analyses requiring a value change use fresh whole-row frames.
The remembered editing digit alone is not evidence that editing is active.

An active/wake-only classification is a hypothesis from a known probe plus
before/after values. An unchanged number could also mean a missed pulse or a
clamp. Full-run bracketing and repeats reduce ambiguity but do not expose the
controller's actual internal state. Output-on display modes, electrical limits,
clock phase, bit mapping, and independent row behavior still need physical
verification. See the unresolved questions in [calibration.md](calibration.md).

## Firmware Diagnostic Interface

These commands exist for the script; normal use does not require typing them:

| Command | Purpose |
| --- | --- |
| `DIAG:VERS?` | Protocol version, currently 1 |
| `DIAG:ARM 1` | Arm against the currently observed output-off state |
| `DIAG:ARM 2` | Explicitly allow arming with output on; still reject state changes |
| `DIAG:ARM 0` | Cancel queued actions and release panel injection lines |
| `DIAG:ACT? kind,row,up,delay_us,pulse_us,gap_us,id` | Queue one action; return queue depth |
| `DIAG:RUN` | Start the prepared queue |
| `DIAG:STAT?` | Timing, queue, interlock, and decoded-state snapshot |
| `DIAG:TRAC? after_sequence` | Read a trace batch after the last received record |
| `DIAG:TRAC:CLE` | Clear the trace, only while no actions/calibration are busy |

Kind 1 is a button press; kind 2 is a quadrature detent. Row 0 is voltage,
row 1 current. Button high time is bounded to 1-300 ms; rotation phase width
to 100-20000 us. At most 16 actions can be queued, with delays up to 30 seconds.
The previous action's requested gap is an additional minimum before the next.
Ordinary setters, configuration changes, and full calibration are blocked
while the diagnostic session is armed. Diagnostic actions never save flash.

A trace response starts with `head,oldest,lost,count`, followed by quoted
pipe-separated records. Each record has these 22 fields:

```text
sequence|start_us|end_us|kind|flags|action_id|length|hex|screen_sequence|output_on|valid_mask|fresh_mask|blink_mask|V_factor|V_exp|V_digit|I_factor|I_exp|I_digit|P_factor|P_exp|P_digit
```

Trace kinds: 1=SPI frame, 2=action start, 3=action end, 5=GPIO edge. Start/end
metadata bytes are action kind, row, direction. Edge bytes are row, GPIO number,
logic level, edge index. Frame flags: bit 0=partial byte, 1=payload truncated,
2=complete display write, 3=CS timeout, 4=capture overflow. At most 32 bytes per
transaction are exported; the full 17-byte display write fits. Captured length
is capped at the existing 256-byte capture buffer. Incomplete trailing bits
are flagged, not exported as a reconstructed byte.

`DIAG:STAT?` has 35 integer fields: protocol version, now, arm mode, queued
count, running flag, completed action ID, last completion timestamp, error,
screen sequence, screen timestamp, output flag; then eight fields per V/I/P
row: factor, exponent, valid, row sequence, row timestamp, remembered digit,
editing sequence, editing timestamp. Error 1=stale feedback, 2=unexpected
output-state change, 3=host-contact watchdog expiry.

## Interruptions and Recovery

Ctrl+C requests disarm and cancellation. The firmware also cancels on about
three seconds without diagnostic polling. An in-progress bounded pulse finishes
first; serial cancellation is not a hardware emergency stop. On an unexpected
output state the routine releases injection lines but does not blindly press
another possibly miswired output button.

Always check physical output and both targets after failure, disconnection,
watchdog stop, or interruption. If output-on testing was requested, the client
attempts `OUTP OFF` during cleanup, but a lost USB link cannot guarantee it.
Never connect a load based only on the process exit code or software flag.