#!/usr/bin/env python3
"""Record raw Pico SPI traces and controlled panel experiments as JSON Lines."""

from __future__ import annotations

import argparse
import csv
import json
import math
import platform
import statistics
import sys
import time
from collections import Counter, defaultdict
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Protocol

MASK = 0xFFFFFFFF
ROWS = ("voltage", "current", "power")
FRAME, ACTION_START, ACTION_END, EDGE = 1, 2, 3, 5
DISPLAY = 4


class ProbeError(RuntimeError):
    pass


class SerialPort(Protocol):
    def write(self, data: bytes) -> int: ...
    def read_until(self, expected: bytes, size: int) -> bytes: ...
    def close(self) -> None: ...


class JsonLog:
    def __init__(self, path: Path, maximum_bytes: int) -> None:
        self.path = path
        self.maximum_bytes = maximum_bytes
        self.written = 0
        self.file = path.open("x", encoding="utf-8", buffering=1)

    def write(self, kind: str, **fields: Any) -> None:
        record = {
            "type": kind,
            "utc": datetime.now(timezone.utc).isoformat(),
            "host_monotonic_ns": time.monotonic_ns(),
            **fields,
        }
        line = json.dumps(record, separators=(",", ":"), allow_nan=False) + "\n"
        length = len(line.encode("utf-8"))
        if self.written + length > self.maximum_bytes:
            raise ProbeError("Log size limit reached; stopping rather than discarding evidence")
        self.file.write(line)
        self.written += length

    def close(self) -> None:
        self.file.close()


def csv_fields(response: str) -> list[str]:
    return next(csv.reader([response], strict=True))


def parse_state(response: str) -> dict[str, Any]:
    values = [int(value) for value in csv_fields(response)]
    if len(values) != 35 or values[0] != 1:
        raise ProbeError("Unsupported DIAG:STATE? schema; flash the matching firmware")
    names = (
        "version", "now_us", "arm_mode", "queued", "running", "completed_id",
        "ended_us", "error", "screen_sequence", "screen_updated_us", "output_on",
    )
    result: dict[str, Any] = dict(zip(names, values[:11]))
    result["rows"] = {}
    row_names = ("factor", "exponent", "valid", "sequence", "updated_us", "editing_digit",
                 "editing_sequence", "editing_updated_us")
    for row, name in enumerate(ROWS):
        result["rows"][name] = dict(zip(row_names, values[11 + row * 8:19 + row * 8]))
    return result


def parse_trace(response: str) -> tuple[dict[str, int], list[dict[str, Any]]]:
    fields = csv_fields(response)
    if len(fields) < 4:
        raise ProbeError("Incomplete trace header")
    header = dict(zip(("head", "oldest", "lost", "count"), map(int, fields[:4])))
    if not 0 <= header["count"] <= 24 or len(fields) != 4 + header["count"]:
        raise ProbeError("Trace count does not match packet contents")
    records: list[dict[str, Any]] = []
    names = ("sequence", "started_us", "ended_us", "kind", "flags", "action_id",
             "length", "data_hex", "screen_sequence", "output_on", "valid_mask",
             "fresh_mask", "blink_mask")
    for text in fields[4:]:
        parts = text.split("|")
        if len(parts) != 22:
            raise ProbeError("Unsupported trace event schema")
        event: dict[str, Any] = {
            name: value if name == "data_hex" else int(value)
            for name, value in zip(names, parts[:13])
        }
        raw = bytes.fromhex(event["data_hex"])
        if not 0 <= event["length"] <= 256 or len(raw) != min(event["length"], 32):
            raise ProbeError("Trace byte count is inconsistent")
        event["rows"] = {}
        for row, name in enumerate(ROWS):
            offset = 13 + row * 3
            factor, exponent, digit = map(int, parts[offset:offset + 3])
            if not 0 <= factor <= 9999 or not -3 <= exponent <= 0:
                raise ProbeError("Invalid decoded display value in trace")
            event["rows"][name] = {"factor": factor, "exponent": exponent, "editing_digit": digit}
        if event["kind"] == FRAME and raw:
            event["spi_command_hex"] = raw[:1].hex().upper()
        records.append(event)
    return header, records


class DeviceClock:
    def __init__(self) -> None:
        self.previous: int | None = None
        self.elapsed = 0

    def extend(self, event: dict[str, Any]) -> None:
        ended = event["ended_us"]
        if not 0 <= ended <= MASK or not 0 <= event["started_us"] <= MASK:
            raise ProbeError("Invalid device timestamp")
        if self.previous is None:
            self.elapsed = ended
            event_end = self.elapsed
            self.previous = ended
        else:
            delta = (ended - self.previous) & MASK
            if delta > MASK // 2:
                backwards = MASK + 1 - delta
                if backwards > 100000:
                    raise ProbeError("Device clock went backwards: possible reboot or corrupt trace")
                event_end = self.elapsed - backwards
                event["publication_reordered_us"] = backwards
            else:
                self.elapsed += delta
                self.previous = ended
                event_end = self.elapsed
        event["device_end_us"] = event_end
        event["device_start_us"] = event_end - ((ended - event["started_us"]) & MASK)


@dataclass(frozen=True)
class Action:
    id: int
    role: str
    kind: int
    row: int
    up: bool
    delay_us: int
    pulse_us: int
    gap_us: int = 0

    def command(self) -> str:
        return (f"DIAG:ACT? {self.kind},{self.row},{int(self.up)},{self.delay_us},"
                f"{self.pulse_us},{self.gap_us},{self.id}")


def timing_stats(values: list[int]) -> dict[str, int | float] | None:
    if not values:
        return None
    return {"count": len(values), "min_us": min(values), "median_us": statistics.median(values),
            "max_us": max(values)}


def summarize_trial(events: list[dict[str, Any]], actions: list[Action], row: int,
                    experiment: str, default_digit: int | None = None) -> dict[str, Any]:
    starts = {event["action_id"]: event for event in events if event["kind"] == ACTION_START}
    ends = {event["action_id"]: event for event in events if event["kind"] == ACTION_END}
    frames = [event for event in events if event["kind"] == FRAME]
    displays = [event for event in frames if event["flags"] & DISPLAY]
    measurements = []
    previous_end: int | None = None
    for action in actions:
        start, end = starts.get(action.id), ends.get(action.id)
        edges = [event for event in events if event["kind"] == EDGE and event["action_id"] == action.id]
        item: dict[str, Any] = {"action": asdict(action), "markers_complete": bool(start and end)}
        if start and end:
            item["actual_duration_us"] = end["device_end_us"] - start["device_start_us"]
            if previous_end is not None:
                item["actual_delay_us"] = start["device_start_us"] - previous_end
                item["delay_lateness_us"] = item["actual_delay_us"] - action.delay_us
            previous_end = end["device_end_us"]
            item["edge_spacing_us"] = [later["device_start_us"] - earlier["device_start_us"]
                                        for earlier, later in zip(edges, edges[1:])]
            following = [frame for frame in frames if frame["device_start_us"] >= previous_end]
            if following:
                item["first_spi_transaction_us"] = following[0]["device_start_us"] - previous_end
            following_display = [frame for frame in displays if frame["device_start_us"] >= previous_end]
            if following_display:
                item["first_display_write_us"] = following_display[0]["device_start_us"] - previous_end
        measurements.append(item)
    result: dict[str, Any] = {"event_count": len(events), "frame_count": len(frames),
                              "command_counts": dict(Counter(frame.get("spi_command_hex", "") for frame in frames)),
                              "actions": measurements, "classification": "inconclusive"}
    result["blink_onsets_us"] = {}
    result["blink_periods"] = {}
    for row_index, name in enumerate(ROWS):
        onsets = []
        previously_blank = False
        for frame in sorted(displays, key=lambda event: event["device_start_us"]):
            blank = bool(frame["blink_mask"] & (1 << row_index))
            if blank and not previously_blank:
                onsets.append(frame["device_start_us"])
            previously_blank = blank
        result["blink_onsets_us"][name] = onsets
        result["blink_periods"][name] = timing_stats([later - earlier for earlier, later in zip(onsets, onsets[1:])])
    probe = next((action for action in actions if action.role == "probe"), None)
    anchor = next((action for action in actions if action.role == "anchor_down"), None)
    if probe and probe.id in starts and probe.id in ends:
        probe_start, probe_end = starts[probe.id]["device_start_us"], ends[probe.id]["device_end_us"]
        fresh_after = [frame for frame in displays if frame["device_start_us"] >= probe_end
                       and frame["fresh_mask"] & (1 << row)]
        before = [frame for frame in displays if frame["device_end_us"] < probe_start
                  and frame["fresh_mask"] & (1 << row)]
        if anchor and anchor.id in ends:
            before = [frame for frame in before if frame["device_start_us"] > ends[anchor.id]["device_end_us"]]
        name = ROWS[row]
        if before and fresh_after:
            initial, final = before[-1]["rows"][name], fresh_after[-1]["rows"][name]
            result["before_value"], result["after_value"] = initial, final
            if initial["exponent"] == final["exponent"]:
                result["factor_change"] = final["factor"] - initial["factor"]
                if experiment == "idle_boundary":
                    result["classification"] = {0: "wake_only", 1: "active"}.get(result["factor_change"], "inconclusive")
        blink_after = [frame for frame in displays if frame["device_start_us"] >= probe_end
                       and frame["blink_mask"] & (1 << row)]
        if experiment == "exit_boundary" and blink_after and default_digit is not None and default_digit != 1:
            observed = blink_after[-1]["rows"][name]["editing_digit"]
            result["observed_digit"] = observed
            result["classification"] = "retained" if observed == 1 else "reset" if observed == default_digit else "inconclusive"
    return result


class Probe:
    def __init__(self, port: SerialPort, log: JsonLog, poll_interval: float = 0.05) -> None:
        self.port = port
        self.log = log
        self.poll_interval = poll_interval
        self.clock = DeviceClock()
        self.cursor = 0
        self.next_id = 1
        self.trace_events: list[dict[str, Any]] | None = None
        self.label = "startup"
        self.config: dict[str, int] = {}
        self.results: list[dict[str, Any]] = []
        self.default_digits: dict[int, int] = {}
        self.armed = False
        self.enabled_output = False

    def query(self, command: str, timeout: float = 3.0) -> str:
        started = time.monotonic()
        self.log.write("request", command=command, label=self.label)
        payload = (command + "\n").encode("ascii")
        if self.port.write(payload) != len(payload):
            raise ProbeError("Incomplete serial write")
        response = bytearray()
        while time.monotonic() - started < timeout:
            response.extend(self.port.read_until(b"\n", 65536 - len(response)))
            if len(response) >= 65536:
                raise ProbeError("Serial response exceeded 64 KiB")
            if response.endswith(b"\n"):
                text = response.decode("ascii").strip()
                self.log.write("response", command=command, elapsed_s=time.monotonic() - started,
                               bytes=len(response), response=None if command.startswith("DIAG:TRAC?") else text,
                               label=self.label)
                return text
        self.log.write("timeout", command=command, partial_hex=response.hex(), label=self.label)
        raise ProbeError(f"Timed out on {command!r}; do not continue on a potentially misaligned serial stream")

    def command(self, command: str) -> None:
        self.log.write("request", command=command, label=self.label)
        data = (command + "\n").encode("ascii")
        if self.port.write(data) != len(data):
            raise ProbeError("Incomplete serial write")
        error = self.query("SYST:ERR?")
        if error != '0,"No error"':
            raise ProbeError(f"{command}: {error}")

    def state(self) -> dict[str, Any]:
        state = parse_state(self.query("DIAG:STAT?"))
        self.log.write("state", label=self.label, state=state)
        if state["error"]:
            raise ProbeError(f"Diagnostic interlock error {state['error']}: 1=stale feedback, 2=output changed, 3=host watchdog")
        return state

    def drain(self) -> None:
        for _batch in range(16):
            header, events = parse_trace(self.query(f"DIAG:TRAC? {self.cursor}"))
            self.log.write("trace_batch", label=self.label, **header)
            if header["lost"]:
                self.cursor = (header["oldest"] - 1) & MASK
            for event in events:
                if event["sequence"] != (self.cursor + 1) & MASK:
                    raise ProbeError("Non-contiguous trace sequence: possible reset or missing events")
                self.clock.extend(event)
                self.cursor = event["sequence"]
                self.log.write("spi" if event["kind"] == FRAME else "device_action", label=self.label, event=event)
                if self.trace_events is not None:
                    self.trace_events.append(event)
            if header["lost"]:
                raise ProbeError(f"Lost {header['lost']} trace records; timing evidence is incomplete")
            if not events or self.cursor == header["head"]:
                return
        self.log.write("reader_backlog", label=self.label)

    def observe(self, seconds: float) -> dict[str, Any]:
        deadline = time.monotonic() + seconds
        while True:
            self.drain()
            state = self.state()
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return state
            time.sleep(min(self.poll_interval, remaining))

    def arm(self, mode: int = 1) -> None:
        self.command(f"DIAG:ARM {mode}")
        self.armed = mode != 0

    def action(self, role: str, kind: int, row: int, delay_s: float = 0.0,
               pulse_us: int | None = None, up: bool = True, gap_us: int = 0) -> Action:
        prefix = "V" if row == 0 else "I"
        if pulse_us is None:
            pulse_us = self.config[f"{prefix}_PRESS_US" if kind == 1 else f"{prefix}_PHASE_US"]
        action = Action(self.next_id, role, kind, row, up, round(delay_s * 1_000_000), pulse_us, gap_us)
        self.next_id += 1
        return action

    def trial(self, label: str, row: int, actions: list[Action], experiment: str = "context",
              after_s: float = 2.2, **metadata: Any) -> dict[str, Any]:
        if not 1 <= len(actions) <= 16:
            raise ProbeError("A trial must contain 1..16 actions")
        self.label = label
        print(f"Testing {label}", flush=True)
        self.drain()
        before = self.state()
        self.log.write("trial_start", label=label, row=row, experiment=experiment,
                       actions=[asdict(action) for action in actions], before=before, **metadata)
        self.trace_events = []
        try:
            for index, action in enumerate(actions, 1):
                if int(self.query(action.command())) != index:
                    raise ProbeError("Unexpected diagnostic queue count")
            self.command("DIAG:RUN")
            timeout = sum(action.delay_us / 1e6 + action.pulse_us * (1 if action.kind == 1 else 3) / 1e6
                          + action.gap_us / 1e6 for action in actions) + 10
            deadline = time.monotonic() + timeout
            while True:
                self.drain()
                state = self.state()
                if state["completed_id"] == actions[-1].id and not state["queued"] and not state["running"]:
                    break
                if not state["arm_mode"] or time.monotonic() > deadline:
                    raise ProbeError("Diagnostic action did not complete")
                time.sleep(self.poll_interval)
            self.observe(after_s)
            self.drain()
            summary = summarize_trial(self.trace_events, actions, row, experiment, self.default_digits.get(row))
            summary.update(label=label, experiment=experiment, row=row, **metadata)
            self.results.append(summary)
            self.log.write("trial_result", **summary)
            return summary
        finally:
            self.trace_events = None

    def select_digit(self, row: int, digit: int = 3, reset: bool = False) -> None:
        if reset:
            self.label = f"{ROWS[row]}/idle_recovery"
            self.observe(12.0)
        self.trial(f"{ROWS[row]}/enter_edit", row, [self.action("enter", 1, row)], after_s=1.2)
        state = self.state()
        observed = state["rows"][ROWS[row]]["editing_digit"]
        if reset:
            self.default_digits[row] = observed
            self.log.write("observed_default_digit", row=row, digit=observed,
                           caveat="Observed after the configured recovery interval; not an internal mode flag")
        for move in range(4):
            value = state["rows"][ROWS[row]]
            age = (state["now_us"] - value["editing_updated_us"]) & MASK
            if observed >= 4 or age > 1_500_000:
                raise ProbeError("No recent blink digit: cannot select a safe least-significant digit")
            if observed == digit:
                if not value["valid"] or not 4 <= value["factor"] <= 9995:
                    raise ProbeError("Set both targets manually away from zero and display/clamp boundaries")
                return
            self.trial(f"{ROWS[row]}/select/{move}", row, [self.action("select", 1, row)], after_s=1.2)
            state = self.state()
            observed = state["rows"][ROWS[row]]["editing_digit"]
        raise ProbeError("Digit selection did not cycle as expected")

    def anchors(self, row: int) -> list[Action]:
        return [self.action("anchor_up", 2, row, up=True),
                self.action("anchor_down", 2, row, delay_s=0.1, up=False)]

    def idle_trial(self, row: int, delay_s: float, repeat: int) -> dict[str, Any]:
        self.select_digit(row, reset=True)
        actions = self.anchors(row) + [self.action("probe", 2, row, delay_s=delay_s)]
        return self.trial(f"{ROWS[row]}/idle/{delay_s:.6f}/{repeat}", row, actions,
                          "idle_boundary", requested_delay_s=delay_s)

    def run_active(self, full: bool, repeats: int) -> None:
        for row in (0, 1):
            self.select_digit(row, reset=True)
            for delay in (0.25, 2.5, 12.0):
                self.trial(f"{ROWS[row]}/button_context/{delay}", row,
                           [self.action("anchor", 1, row), self.action("probe", 1, row, delay_s=delay)],
                           requested_delay_s=delay)
            for repeat in range(repeats):
                for delay in ((0.5, 1.75, 2.0, 2.25, 3.0) if full else (0.5, 2.5)):
                    self.idle_trial(row, delay, repeat)
                for delay in ((6.0, 8.5, 9.0, 9.5, 12.0) if full else (6.0, 12.0)):
                    self.select_digit(row, reset=True)
                    self.trial(f"{ROWS[row]}/exit/{delay}/{repeat}", row,
                               [self.action("anchor", 1, row), self.action("probe", 1, row, delay_s=delay)],
                               "exit_boundary", requested_delay_s=delay)
            for group, values in (
                ("press_width", (1000, 10000, 30000, 50000, 61000, 100000) if full else (30000, 61000)),
                ("release_gap", (0, 5000, 10000, 20000, 50000) if full else (5000, 20000)),
                ("rotation_phase", (100, 250, 500, 1000, 1250, 2500, 5000, 10000) if full else (500, 2500)),
                ("rotation_gap", (0, 1000, 5000, 10000, 20000, 50000) if full else (0, 20000)),
            ):
                for value in values:
                    for repeat in range(repeats):
                        self.select_digit(row)
                        button = group in ("press_width", "release_gap")
                        pulse = value if group in ("press_width", "rotation_phase") else (
                            100000 if button else self.config["V_PHASE_US" if row == 0 else "I_PHASE_US"])
                        gap = value if group in ("release_gap", "rotation_gap") else (100000 if button else 20000)
                        actions = [] if button else self.anchors(row)
                        actions += [self.action("test1", 1 if button else 2, row, delay_s=0.3 if not button else 0,
                                                pulse_us=pulse),
                                    self.action("test2", 1 if button else 2, row, delay_s=gap / 1e6, pulse_us=pulse)]
                        self.trial(f"{ROWS[row]}/{group}/{value}/{repeat}", row, actions,
                                   experiment=group, candidate_us=value)
            for exit_test in (False, True):
                self.select_digit(row, reset=True)
                seconds = self.config[("V_" if row == 0 else "I_") + ("EXIT_US" if exit_test else "IDLE_US")] / 1e6
                actions = [self.action("anchor", 1, row)] if exit_test else self.anchors(row)
                actions += [self.action("other_row", 1, 1 - row, delay_s=seconds * 0.75),
                            self.action("probe", 1 if exit_test else 2, row, delay_s=seconds * 0.75)]
                self.trial(f"{ROWS[row]}/coupling/{'exit' if exit_test else 'idle'}", row, actions,
                           experiment="coupling_exit" if exit_test else "coupling_idle")
            if full:
                self.refine_idle_boundary(row, repeats)

    def refine_idle_boundary(self, row: int, repeats: int) -> None:
        def classify(delay: float) -> str:
            results = [self.idle_trial(row, delay, repeat)["classification"] for repeat in range(repeats)]
            return results[0] if len(set(results)) == 1 else "inconclusive"

        low, high = 0.5, 4.0
        if classify(low) != "active" or classify(high) != "wake_only":
            self.log.write("idle_search", row=row, result="not_bracketed_or_inconsistent",
                           lower_s=low, upper_s=high)
            return
        while high - low > 0.05:
            middle = (low + high) / 2
            outcome = classify(middle)
            if outcome == "inconclusive":
                self.log.write("idle_search", row=row, result="inconclusive",
                               lower_s=low, upper_s=high, candidate_s=middle)
                return
            if outcome == "active":
                low = middle
            else:
                high = middle
        self.log.write("idle_search", row=row, result="consistent_with_wake_boundary",
                       lower_s=low, upper_s=high, resolution_s=0.05,
                       caveat="Requested edge delays; use trial edge timestamps to assess jitter and scan uncertainty")

    def startup(self, active: bool) -> None:
        identity = self.query("*IDN?")
        if not identity.startswith("Pico,Powersupply,"):
            raise ProbeError("Not the expected Pico power-supply bridge")
        if self.query("DIAG:VERS?") != "1":
            raise ProbeError("Matching diagnostic firmware is required")
        initial = self.state()
        if initial["arm_mode"] or initial["queued"]:
            raise ProbeError("Another diagnostic session appears active; do not share the serial port")
        calibration = csv_fields(self.query("CAL:STAT?"))
        if int(calibration[0]) in (1, 2):
            raise ProbeError("Automatic calibration is busy; this script does not invoke or interrupt it")
        errors = []
        for _error in range(9):
            error = self.query("SYST:ERR?")
            errors.append(error)
            if error == '0,"No error"':
                break
        if errors[-1] != '0,"No error"':
            raise ProbeError("Error queue did not drain")
        names = csv_fields(self.query("CAL:LIST?"))
        values = [int(value) for value in csv_fields(self.query("CAL:CONF?"))]
        if len(names) != len(values):
            raise ProbeError("Calibration schema mismatch")
        self.config = dict(zip(names, values))
        self.log.write("metadata", identity=identity, config=self.config, initial=initial,
                       previous_errors=errors, calibration_status=calibration,
                       bus_timing=csv_fields(self.query("SYST:DISP:TIM?")),
                       bus_cadence=csv_fields(self.query("SYST:DISP:CAD?")),
                       platform=platform.platform(), python=sys.version)
        if active and initial["output_on"]:
            raise ProbeError("Turn output off physically before starting active tests")
        self.command("DIAG:TRAC:CLE")
        self.cursor = 0
        if active:
            self.arm(1)

    def output_context(self) -> None:
        self.arm(0)
        self.enabled_output = True
        self.command("OUTP ON")
        if self.query("OUTP?") != "1":
            raise ProbeError("Output enable was not confirmed")
        self.arm(2)
        for row in (0, 1):
            self.trial(f"{ROWS[row]}/output_on_context", row,
                       [self.action("enter", 1, row, delay_s=12),
                        self.action("select", 1, row, delay_s=0.25),
                        self.action("probe", 1, row, delay_s=2.5)], after_s=3)
        self.arm(0)
        self.command("OUTP OFF")
        if self.query("OUTP?") != "0":
            raise ProbeError("Output disable was not confirmed: use the physical controls")
        self.enabled_output = False

    def cleanup(self) -> None:
        if not (self.armed or self.enabled_output):
            return
        try:
            self.port.write(b"\nDIAG:ARM 0\n")
            if self.enabled_output:
                self.port.write(b"OUTP OFF\n")
        except Exception as error:
            print(f"Cleanup could not reach the device: {error}", file=sys.stderr)
        print("Check physical output and both targets. No automatic target restoration is attempted.", file=sys.stderr)


def analyze_log(path: Path) -> dict[str, Any]:
    commands: Counter[str] = Counter()
    periods: dict[str, list[int]] = defaultdict(list)
    previous: dict[str, int] = {}
    trials = []
    losses = 0
    truncated = 0
    session_end: dict[str, Any] | None = None
    with path.open(encoding="utf-8") as source:
        for line_number, line in enumerate(source, 1):
            try:
                record = json.loads(line)
            except json.JSONDecodeError:
                return {"error": "Invalid or incomplete JSONL record", "line": line_number}
            if record["type"] == "spi":
                event = record["event"]
                command = event.get("spi_command_hex", "")
                commands[command] += 1
                truncated += bool(event["flags"] & 2)
                if command in previous:
                    periods[command].append(event["device_start_us"] - previous[command])
                previous[command] = event["device_start_us"]
            elif record["type"] == "trace_batch":
                losses += record["lost"]
            elif record["type"] == "trial_result":
                trials.append({name: record[name] for name in record if name not in ("utc", "host_monotonic_ns", "type")})
            elif record["type"] == "session_end":
                session_end = record
    return {"schema_version": 1, "source": str(path), "command_counts": dict(commands),
            "command_periods": {command: timing_stats(values) for command, values in periods.items()},
            "lost_records": losses, "truncated_frames": truncated, "trials": trials,
            "session_end": session_end,
            "caveat": "Classifications are hypotheses; inspect raw frames, edge timestamps, and repeated trials."}


def arguments(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="Pico USB serial port, e.g. /dev/ttyACM0 or COM5")
    parser.add_argument("--log", type=Path, help="New JSONL path; existing files are never overwritten")
    parser.add_argument("--label", default="", help="Supply model / wiring / operator notes for this run")
    parser.add_argument("--duration", type=float, default=15, help="Initial passive observation seconds")
    parser.add_argument("--active", action="store_true", help="Inject panel actions; targets may change")
    parser.add_argument("--loads-disconnected", action="store_true", help="Confirm all loads are physically disconnected")
    parser.add_argument("--full", action="store_true", help="Dense timing matrices; implies neither active nor confirmation")
    parser.add_argument("--repeats", type=int, default=None, help="Repetitions per timing candidate (default: 1 quick, 3 full)")
    parser.add_argument("--include-output-on", action="store_true", help="Explicitly permit an optional no-load output-on context")
    parser.add_argument("--poll-interval", type=float, default=0.05, help="Maximum pause between trace polls, seconds")
    parser.add_argument("--max-log-mb", type=float, default=512, help="Stop before exceeding this JSONL size")
    parser.add_argument("--analyze", type=Path, help="Analyze an existing log without serial hardware or pyserial")
    args = parser.parse_args(argv)
    if args.analyze:
        return args
    if not args.port:
        parser.error("--port is required unless --analyze is used")
    if args.active and not args.loads_disconnected:
        parser.error("--active requires --loads-disconnected")
    if (args.full or args.include_output_on) and not args.active:
        parser.error("--full and --include-output-on require --active")
    if not math.isfinite(args.duration) or not 0 < args.duration <= 3600:
        parser.error("--duration must be in (0, 3600]")
    if not math.isfinite(args.poll_interval) or not 0.001 <= args.poll_interval <= 0.5:
        parser.error("--poll-interval must be in [0.001, 0.5]")
    if not math.isfinite(args.max_log_mb) or not 1 <= args.max_log_mb <= 16384:
        parser.error("--max-log-mb must be in [1, 16384]")
    args.repeats = args.repeats if args.repeats is not None else (3 if args.full else 1)
    if not 1 <= args.repeats <= 10:
        parser.error("--repeats must be in [1, 10]")
    return args


def main(argv: list[str] | None = None) -> int:
    args = arguments(argv)
    if args.analyze:
        print(json.dumps(analyze_log(args.analyze), indent=2, allow_nan=False))
        return 0
    try:
        import serial
    except ImportError:
        print("Install pyserial on this device: python -m pip install pyserial", file=sys.stderr)
        return 2
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    path = args.log or Path(f"powersupply_probe_{stamp}.jsonl")
    try:
        log = JsonLog(path, int(args.max_log_mb * 1024 * 1024))
    except OSError as error:
        print(f"Cannot create log: {error}", file=sys.stderr)
        return 2
    probe: Probe | None = None
    port: SerialPort | None = None
    result = 1
    try:
        log.write("session_start", schema_version=1, options={key: str(value) if isinstance(value, Path) else value
                                                            for key, value in vars(args).items()},
                  warning="Active probing can change targets. No electrical limits or automatic target restoration are assumed.")
        port = serial.Serial(args.port, baudrate=115200, timeout=0.1, write_timeout=3,
                             xonxoff=False, rtscts=False, dsrdtr=False)
        probe = Probe(port, log, args.poll_interval)
        probe.startup(args.active)
        print(f"Logging to {path}", flush=True)
        probe.label = "passive_baseline"
        probe.observe(args.duration)
        if args.active:
            print("Active tests can change both targets. Keep loads disconnected; verify the panel afterward.", flush=True)
            probe.run_active(args.full, args.repeats)
        if args.include_output_on:
            probe.output_context()
        if probe.armed:
            probe.arm(0)
        probe.drain()
        log.write("session_end", success=True, trials=len(probe.results), final=probe.state(),
              bus_timing=csv_fields(probe.query("SYST:DISP:TIM?")),
              bus_cadence=csv_fields(probe.query("SYST:DISP:CAD?")))
        result = 0
    except KeyboardInterrupt:
        print("Interrupted; cancelling queued actions. Check the physical supply.", file=sys.stderr)
        try:
            log.write("session_end", success=False, error="KeyboardInterrupt")
        except ProbeError:
            pass
        result = 130
    except Exception as error:
        print(f"Probe stopped: {error}", file=sys.stderr)
        try:
            log.write("session_end", success=False, error=f"{type(error).__name__}: {error}")
        except ProbeError:
            pass
    finally:
        if probe:
            probe.cleanup()
        if port:
            port.close()
        log.close()
    summary = path.with_suffix(".summary.json")
    try:
        with summary.open("x", encoding="utf-8") as output:
            json.dump(analyze_log(path), output, indent=2, allow_nan=False)
        print(f"Return both files: {path} and {summary}", flush=True)
    except OSError as error:
        print(f"Summary not written: {error}; the JSONL remains the primary evidence", file=sys.stderr)
    return result


if __name__ == "__main__":
    raise SystemExit(main())