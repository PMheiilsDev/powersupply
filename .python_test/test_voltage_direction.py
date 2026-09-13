#!/usr/bin/env python3
"""Test voltage changes in both directions with fixed timing."""

from __future__ import annotations

import argparse
import json
import math
import sys
import time
from contextlib import closing

try:
    from serial import Serial, SerialException
except ImportError:
    print("This test requires pyserial: apt install python3-serial", file=sys.stderr)
    raise SystemExit(2)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="USB serial device, for example /dev/ttyACM0")
    parser.add_argument("--repetitions", type=int, default=20, help="number of cycles per direction (default: 20)")
    parser.add_argument("--high", type=float, default=9.50, help="high voltage in each pair (default: 9.50)")
    parser.add_argument("--low", type=float, default=6.50, help="low voltage in each pair (default: 6.50)")
    parser.add_argument("--pre-query-delay", type=float, default=0.1, help="fixed wait before each query (default: 0.1 s)")
    parser.add_argument("--post-query-delay", type=float, default=0.1, help="fixed wait after each query (default: 0.1 s)")
    parser.add_argument("--setup-delay", type=float, default=5.0, help="wait after establishing the initial voltage (default: 5 s)")
    parser.add_argument("--timeout", type=float, default=3.0, help="maximum time to wait for a response (default: 3 s)")
    parser.add_argument("--output", default="voltage_direction_commands.jsonl", help="JSONL output path")
    return parser.parse_args()


def query_voltage(device: Serial, timeout: float) -> tuple[float | None, str]:
    device.reset_input_buffer()
    device.write(b"VOLTAGE?\n")
    device.flush()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        response = device.readline().decode("ascii", errors="replace").strip()
        if not response:
            continue
        try:
            return float(response), response
        except ValueError:
            return None, response
    return None, ""


def set_voltage(device: Serial, voltage: float) -> None:
    device.write(f"VOLTAGE {voltage:.2f}\n".encode("ascii"))
    device.flush()


def is_correct(actual: float | None, target: float) -> bool:
    return actual is not None and math.isclose(actual, target, rel_tol=0.0, abs_tol=1e-9)


def run() -> None:
    args = parse_args()
    if args.repetitions < 1:
        raise ValueError("--repetitions must be at least 1")
    if args.high <= args.low:
        raise ValueError("--high must be greater than --low")

    transitions = []
    with closing(Serial(args.port, 115200, timeout=0.1)) as device:
        device.reset_input_buffer()
        set_voltage(device, args.high)
        time.sleep(args.setup_delay)
        initial, initial_text = query_voltage(device, args.timeout)
        if not is_correct(initial, args.high):
            raise RuntimeError(f"could not establish {args.high:.2f} V; read {initial_text!r}")

        current_expected = args.high
        for cycle in range(1, args.repetitions + 1):
            for direction, target in (("down", args.low), ("up", args.high)):
                before, before_text = query_voltage(device, args.timeout)
                set_voltage(device, target)
                time.sleep(args.pre_query_delay)
                after, after_text = query_voltage(device, args.timeout)
                result = {
                    "cycle": cycle,
                    "direction": direction,
                    "actual_before": before_text,
                    "expected_before": current_expected,
                    "target": f"{target:.2f}",
                    "actual_after": after_text,
                    "success": is_correct(after, target),
                    "pre_query_delay": args.pre_query_delay,
                    "post_query_delay": args.post_query_delay,
                }
                transitions.append(result)
                print(
                    f"cycle {cycle:02d} {direction:4s}: "
                    f"before={before_text or '<empty>'} target={target:.2f} "
                    f"after={after_text or '<empty>'} "
                    f"[{ 'ok' if result['success'] else 'FAIL' }]"
                )
                current_expected = target
                time.sleep(args.post_query_delay)

    with open(args.output, "w", encoding="utf-8") as output:
        for result in transitions:
            output.write(json.dumps(result) + "\n")

    passed = sum(result["success"] for result in transitions)
    print(f"direction test: {passed}/{len(transitions)} passed")
    if passed != len(transitions):
        raise AssertionError("one or more voltage direction transitions failed")


if __name__ == "__main__":
    try:
        run()
    except (AssertionError, OSError, SerialException, RuntimeError, ValueError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
