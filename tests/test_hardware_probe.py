import csv
import io
import json
import tempfile
import unittest
from contextlib import redirect_stderr
from pathlib import Path
from unittest.mock import patch

import hardware_probe as probe


def trace_packet(*records, head=None, oldest=1, lost=0):
    stream = io.StringIO()
    csv.writer(stream, lineterminator="").writerow([head or len(records), oldest, lost, len(records), *records])
    return stream.getvalue()


def trace_record(sequence=1, started=100, ended=200, kind=1, flags=4, action_id=0, factor=950):
    return (f"{sequence}|{started}|{ended}|{kind}|{flags}|{action_id}|1|C0|1|0|7|7|0|"
            f"{factor}|-2|3|1234|-3|2|1172|-2|255")


class FakeSerial:
    def __init__(self):
        self.responses = bytearray()
        self.commands = []

    def write(self, data):
        command = data.decode().strip()
        self.commands.append(command)
        if command == "*IDN?":
            response = "Pico,Powersupply,0,3"
        elif command == "DIAG:VERS?":
            response = "1"
        elif command == "DIAG:STAT?":
            response = "1,100,0,0,0,0,0,0,1,100,0," + ",".join(["950,-2,1,1,100,3,1,100"] * 3)
        elif command == "CAL:STAT?":
            response = '0,"IDLE",255,0,0,0,"",0,0'
        elif command == "CAL:LIST?":
            response = '"V_PRESS_US","I_PRESS_US"'
        elif command == "CAL:CONF?":
            response = "61000,61000"
        elif command == "SYST:ERR?":
            response = '0,"No error"'
        elif command == "SYST:DISP:TIM?":
            response = ",".join(["0"] * 13)
        elif command == "SYST:DISP:CAD?":
            response = "3,10000,10000,50000,50000"
        elif command == "DIAG:TRAC? 0":
            response = trace_packet(trace_record())
        elif command == "DIAG:TRAC? 1":
            response = "1,1,0,0"
        else:
            return len(data)
        self.responses.extend((response + "\r\n").encode())
        return len(data)

    def read_until(self, expected, size):
        length = min(7, len(self.responses), size)
        if b"\n" in self.responses[:length]:
            length = self.responses.index(b"\n") + 1
        result = bytes(self.responses[:length])
        del self.responses[:length]
        return result

    def close(self):
        pass


class ProbeTests(unittest.TestCase):
    def test_trace_csv_and_state(self):
        header, events = probe.parse_trace(trace_packet(trace_record()))
        self.assertEqual(header["count"], 1)
        self.assertEqual(events[0]["rows"]["voltage"]["factor"], 950)
        self.assertEqual(events[0]["data_hex"], "C0")
        state = probe.parse_state(
            "1,100,0,0,0,0,0,0,1,100,0," + ",".join(["950,-2,1,1,100,3,1,100"] * 3))
        self.assertEqual(state["rows"]["current"]["editing_digit"], 3)
        with self.assertRaises(probe.ProbeError):
            probe.parse_trace("1,1,0,1")
        with self.assertRaises(probe.ProbeError):
            probe.parse_trace(trace_packet(trace_record().replace("|1|C0|", "|2|C0|")))

    def test_clock_wrap_and_reboot(self):
        clock = probe.DeviceClock()
        first = {"started_us": probe.MASK - 10, "ended_us": probe.MASK - 5}
        second = {"started_us": probe.MASK - 2, "ended_us": 10}
        clock.extend(first)
        clock.extend(second)
        self.assertEqual(second["device_end_us"] - first["device_end_us"], 16)
        self.assertEqual(second["device_end_us"] - second["device_start_us"], 13)
        reordered = {"started_us": 1, "ended_us": 2}
        clock.extend(reordered)
        self.assertEqual(reordered["publication_reordered_us"], 8)
        clock.extend({"started_us": 1000000, "ended_us": 1000001})
        with self.assertRaises(probe.ProbeError):
            clock.extend({"started_us": 1, "ended_us": 2})

    def test_jsonl_exclusive_and_analysis(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "run.jsonl"
            log = probe.JsonLog(path, 10000)
            log.write("session_start", schema_version=1)
            for index, started in enumerate((100, 10100), 1):
                event = probe.parse_trace(trace_packet(trace_record(index, started, started + 10)))[1][0]
                event.update(device_start_us=started, device_end_us=started + 10)
                log.write("spi", event=event)
            log.close()
            self.assertEqual(probe.analyze_log(path)["command_periods"]["C0"]["median_us"], 10000)
            with self.assertRaises(FileExistsError):
                probe.JsonLog(path, 10000)
            with path.open(encoding="utf-8") as source:
                self.assertEqual(json.loads(source.readline())["schema_version"], 1)

    def test_active_confirmation(self):
        with redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                probe.arguments(["--port", "COM5", "--active"])
            with self.assertRaises(SystemExit):
                probe.arguments(["--port", "COM5", "--include-output-on"])
        args = probe.arguments(["--port", "COM5", "--active", "--loads-disconnected"])
        self.assertFalse(args.include_output_on)
        self.assertEqual(args.repeats, 1)
        self.assertFalse(probe.arguments(["--port", "COM5"]).active)

    def test_action_and_inconclusive_evidence(self):
        action = probe.Action(3, "probe", 2, 0, True, 2000000, 1250)
        self.assertEqual(action.command(), "DIAG:ACT? 2,0,1,2000000,1250,0,3")
        result = probe.summarize_trial([], [action], 0, "idle_boundary")
        self.assertEqual(result["classification"], "inconclusive")
        self.assertFalse(result["actions"][0]["markers_complete"])

    def test_fragmented_serial_passive_session(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "run.jsonl"
            log = probe.JsonLog(path, 100000)
            serial = FakeSerial()
            client = probe.Probe(serial, log)
            client.startup(False)
            state = client.observe(0)
            log.close()
            self.assertEqual(state["output_on"], 0)
            self.assertEqual(probe.analyze_log(path)["command_counts"], {"C0": 1})
            self.assertFalse(any(command.startswith("CAL:FULL") for command in serial.commands))
            self.assertFalse(any(command.startswith("DIAG:ARM") for command in serial.commands))

    def test_serial_timeout_is_not_retried(self):
        with tempfile.TemporaryDirectory() as directory:
            log = probe.JsonLog(Path(directory) / "run.jsonl", 100000)
            serial = FakeSerial()
            client = probe.Probe(serial, log)
            with patch("hardware_probe.time.monotonic", side_effect=[0, 0, 4]):
                with self.assertRaises(probe.ProbeError):
                    client.query("NO_RESPONSE?")
            self.assertEqual(serial.commands, ["NO_RESPONSE?"])
            log.close()

    def test_idle_classification_requires_fresh_evidence(self):
        actions = [probe.Action(1, "anchor_down", 2, 0, False, 0, 1250),
                   probe.Action(2, "probe", 2, 0, True, 1999800, 1250)]
        texts = [trace_record(1, 100, 100, 2, 0, 1), trace_record(2, 100, 200, 3, 0, 1),
                 trace_record(3, 400, 500, factor=950), trace_record(4, 2000000, 2000000, 2, 0, 2),
                 trace_record(5, 2000000, 2004000, 3, 0, 2), trace_record(6, 2050000, 2051000, factor=951)]
        events = probe.parse_trace(trace_packet(*texts))[1]
        clock = probe.DeviceClock()
        for event in events:
            clock.extend(event)
        summary = probe.summarize_trial(events, actions, 0, "idle_boundary")
        self.assertEqual(summary["classification"], "active")
        self.assertEqual(summary["factor_change"], 1)
        self.assertEqual(summary["actions"][1]["actual_delay_us"], 1999800)
        events[-1]["fresh_mask"] = 0
        self.assertEqual(probe.summarize_trial(events, actions, 0, "idle_boundary")["classification"], "inconclusive")

    def test_loss_preserves_surviving_records(self):
        class LostSerial(FakeSerial):
            def write(self, data):
                if data.startswith(b"DIAG:TRAC?"):
                    self.responses.extend((trace_packet(trace_record(sequence=2), head=2, oldest=2, lost=1) + "\n").encode())
                    return len(data)
                return super().write(data)

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "run.jsonl"
            log = probe.JsonLog(path, 100000)
            client = probe.Probe(LostSerial(), log)
            with self.assertRaises(probe.ProbeError):
                client.drain()
            log.close()
            analysis = probe.analyze_log(path)
            self.assertEqual(analysis["lost_records"], 1)
            self.assertEqual(analysis["command_counts"], {"C0": 1})

    def test_full_experiment_plan_is_bounded(self):
        class MemoryLog:
            def __init__(self):
                self.records = []

            def write(self, kind, **fields):
                self.records.append((kind, fields))

        class PlanningProbe(probe.Probe):
            def __init__(self):
                self.next_id = 1
                self.config = {"V_PRESS_US": 61000, "I_PRESS_US": 61000,
                               "V_PHASE_US": 1250, "I_PHASE_US": 1250,
                               "V_IDLE_US": 2000000, "I_IDLE_US": 2000000,
                               "V_EXIT_US": 9000000, "I_EXIT_US": 9000000}
                self.default_digits = {0: 2, 1: 2}
                self.log = MemoryLog()
                self.plans = []

            def select_digit(self, row, digit=3, reset=False):
                pass

            def trial(self, label, row, actions, experiment="context", after_s=2.2, **metadata):
                self.plans.append((experiment, actions))
                classification = "active" if metadata.get("requested_delay_s", 0) < 2 else "wake_only"
                return {"classification": classification}

        client = PlanningProbe()
        client.run_active(True, 1)
        experiments = {experiment for experiment, _actions in client.plans}
        self.assertTrue({"idle_boundary", "exit_boundary", "press_width", "release_gap",
                         "rotation_phase", "rotation_gap", "coupling_exit", "coupling_idle"} <= experiments)
        identifiers = []
        for _experiment, actions in client.plans:
            self.assertLessEqual(len(actions), 16)
            for action in actions:
                self.assertIn(action.row, (0, 1))
                self.assertIn(action.kind, (1, 2))
                self.assertLessEqual(action.delay_us, 30000000)
                self.assertGreaterEqual(action.pulse_us, 1000 if action.kind == 1 else 100)
                self.assertLessEqual(action.pulse_us, 300000 if action.kind == 1 else 20000)
                identifiers.append(action.id)
        self.assertEqual(len(identifiers), len(set(identifiers)))
        self.assertEqual(sum(kind == "idle_search" for kind, _fields in client.log.records), 2)


if __name__ == "__main__":
    unittest.main()