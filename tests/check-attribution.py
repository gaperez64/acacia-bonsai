#!/usr/bin/env python3
"""Check live attribution, interrupted native routes, and parent acceptance."""

from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time


def rows(directory: Path) -> list[dict]:
    result = []
    for path in directory.glob("*.jsonl"):
        for line in path.read_text().splitlines():
            try:
                row = json.loads(line)
                row["emitter_pid"] = int(path.stem)
                result.append(row)
            except json.JSONDecodeError:
                result.append({"event": "incomplete_json", "emitter_pid": int(path.stem)})
    return result


def execute(binary: Path, args: list[str], directory: Path | None,
            extra: dict[str, str] | None = None) -> subprocess.CompletedProcess:
    env = os.environ.copy()
    env.pop("ACACIA_PHASE_RECORDS", None)
    env.pop("ACACIA_TEST_ATTRIBUTION_PAUSE", None)
    env["ACACIA_OUTER_DEADLINE_MONOTONIC"] = str(time.monotonic() + 15)
    if directory is not None:
        env["ACACIA_PHASE_RECORDS"] = str(directory)
    env.update(extra or {})
    return subprocess.run([str(binary), *args], env=env, capture_output=True,
                          text=True, timeout=20)


def complete_records(records: list[dict], expected_workers: int = 1,
                     expected_winners: int = 1) -> None:
    assert not any(row.get("event") == "incomplete_json" for row in records), records
    assert all(row.get("dropped_records", 0) == 0 for row in records), records
    assert len([row for row in records if row.get("event") == "worker_spec"]) == expected_workers
    summaries = [row for row in records if row.get("phase") == "record_summary"]
    assert len(summaries) == expected_workers + 1, records
    workers = [row for row in records if row.get("event") == "worker_start"]
    terminals = [row for row in records if row.get("event") == "parent_terminal"]
    assert len(workers) == len(terminals) == expected_workers, records
    for terminal in terminals:
        assert terminal["telemetry"] == "complete", terminal
        assert terminal["dropped_records"] == 0, terminal
        worker = terminal["worker_pid"]
        events = [row for row in records if row.get("worker_pid") == worker
                  and row["emitter_pid"] == worker]
        sequences = [row["seq"] for row in events]
        assert sequences == list(range(1, len(sequences) + 1)), events
        for index, row in enumerate(events):
            if row["event"] == "route_selected":
                assert index + 1 < len(events), events
                assert events[index + 1]["event"] == "route_start", events
                assert events[index + 1]["route"] == row["route"], events
        for required in ("worker_start", "route_selected", "route_start", "terminal_result"):
            assert any(row.get("event") == required for row in events), (required, events)
        for field in ("requested_backend", "effective_backend", "original_polarity",
                      "proof_polarity"):
            assert field in terminal, terminal
    writers = [row for row in records if row.get("event") == "writer_summary"]
    assert len(writers) == 1 and writers[0]["failed_records"] == 0, records
    assert not writers[0]["incomplete_packet"], writers
    assert writers[0]["delivered_records"] == len(records) - 1, records
    winners = [row for row in records if row.get("event") == "parent_winner"]
    assert len(winners) == expected_winners, records
    for winner in winners:
        assert winner["reason"] == "accepted", winner
        assert winner["worker_pid"] in {row["worker_pid"] for row in terminals}, records


def interrupted(binary: Path, source: Path, directory: Path, route: str,
                *, kill_child: bool = False, interrupt_parent: bool = False) -> None:
    env = os.environ.copy()
    env["ACACIA_PHASE_RECORDS"] = str(directory)
    env["ACACIA_TEST_ATTRIBUTION_PAUSE"] = route
    env["MALLOC_PERTURB_"] = "0"
    env["ACACIA_OUTER_DEADLINE_MONOTONIC"] = str(time.monotonic() + 8)
    process = subprocess.Popen([str(binary), "-T", str(source), "--arms",
                                "both:gr1-lift:oxidd"], env=env,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    end = time.monotonic() + 7.5
    selected = None
    while time.monotonic() < end and process.poll() is None:
        selected = next((row for row in rows(directory)
                         if row.get("event") == ("verification_start" if route == "checking"
                                                  else "route_selected")
                         and (route == "checking" or row.get("route") == route)), None)
        if selected:
            break
        time.sleep(.002)
    if selected is None:
        process.terminate ()
        stdout, stderr = process.communicate(timeout=12)
        raise AssertionError((route, rows(directory), stdout, stderr))
    if kill_child:
        os.kill(selected["worker_pid"], signal.SIGKILL)
    elif interrupt_parent:
        os.kill(process.pid, signal.SIGTERM)
    stdout, stderr = process.communicate(timeout=12)
    assert process.returncode == 2 and "UNKNOWN" in stdout + stderr, (stdout, stderr)
    records = rows(directory)
    terminal = next(row for row in records if row.get("event") == "parent_terminal")
    assert terminal["worker_pid"] == selected["worker_pid"], records
    assert terminal["route"] == selected["route"], records
    assert terminal["telemetry"] == "incomplete" and terminal["signal"] == signal.SIGKILL
    assert terminal["reason"] == ("signal" if kill_child else "interrupted"
                                   if interrupt_parent else "deadline"), terminal
    assert not any(row.get("event") in {"parent_winner", "terminal_result"} for row in records)
    assert not any(row.get("event") == "decline" and row.get("route") == selected["route"]
                   for row in records), records


def legacy_resource_stops(binary: Path, root: Path,
                          modes: tuple[str, ...] = ("only", "fallback")) -> None:
    for mode in modes:
        directory = root / f"legacy-resource-{mode}"
        directory.mkdir()
        args = ["-f", "G (i <-> X o)", "-i", "i", "-o", "o", "--arms",
                "real:small:spot-guarded:spot-lazy", "--candidate", mode]
        env = {"ACACIA_SPOT_TAA_MAX_RANK_NODES": "0"}
        baseline = execute(binary, args, None, env)
        result = execute(binary, args, directory, env)
        assert (result.returncode, result.stdout) == (baseline.returncode, baseline.stdout)
        assert result.returncode == (2 if mode == "only" else 0), result
        records = rows(directory)
        stops = [row for row in records if row.get("event") == "route_stopped"]
        assert len(stops) == 1, records
        assert stops[0]["route"] == "spot-lazy" and stops[0]["reason"] == "resource", stops
        assert not any(row.get("event") == "decline" for row in records), records
        complete_records(records, expected_winners=0 if mode == "only" else 1)
        terminal = next(row for row in records if row.get("event") == "terminal_result")
        if mode == "only":
            assert terminal["reason"] == "resource" and terminal["exit_code"] == 2, terminal
        else:
            winner = next(row for row in records if row.get("event") == "parent_winner")
            assert winner["requested_backend"] == "spot-guarded", winner
            assert winner["effective_backend"] == "backward", winner
            assert terminal["reason"] == "none", terminal


def native_exception_stops(binary: Path, source: Path, root: Path) -> None:
    for mode, category in (("bad-alloc", "resource"), ("standard", "error"),
                           ("unknown", "error")):
        directory = root / f"native-exception-{mode}"
        directory.mkdir()
        args = ["-T", str(source), "--arms", "both:gr1-lift:oxidd"]
        env = {"ACACIA_NATIVE_TEST_EXCEPTION": mode}
        baseline = execute(binary, args, None, env)
        result = execute(binary, args, directory, env)
        assert (result.returncode, result.stdout) == (baseline.returncode, baseline.stdout)
        assert result.returncode == 2, result
        records = rows(directory)
        stops = [row for row in records if row.get("event") == "route_stopped"]
        assert len(stops) == 1 and stops[0]["stage"] == "native_exception", records
        assert stops[0]["reason"] == category, stops
        assert not any(row.get("event") == "decline" for row in records), records
        terminal = next(row for row in records if row.get("event") == "terminal_result")
        assert terminal["reason"] == category and terminal["exit_code"] == 2, terminal
        complete_records(records, expected_winners=0)


def main() -> None:
    binary, hook, integration, build = (Path(value).resolve() for value in sys.argv[1:5])
    spec = importlib.util.spec_from_file_location("native_fixtures",
                                                 Path(__file__).with_name(
                                                     "check-native-gr1-lift-cli.py"))
    assert spec and spec.loader
    fixtures = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fixtures)
    with tempfile.TemporaryDirectory(dir=build) as temporary:
        root = Path(temporary)
        legacy_resource_stops(integration, root)
        panel = [("R", fixtures.REAL, 0), ("U", fixtures.UNREAL, 1),
                 ("direct", fixtures.PLAIN, 0)]
        sources = {}
        for route, text, code in panel:
            source = root / f"{route}.tlsf"
            source.write_text(text)
            sources[route] = source
            directory = root / f"panel-{route}"
            directory.mkdir()
            args = ["-T", str(source), "--arms", "both:gr1-lift:oxidd"]
            baseline = execute(binary, args, None)
            observed = execute(binary, args, directory)
            assert (baseline.returncode, baseline.stdout) == (observed.returncode,
                                                            observed.stdout)
            assert observed.returncode == code, observed
            complete_records(rows(directory))
            winner = next(row for row in rows(directory) if row.get("event") == "parent_winner")
            assert winner["route"] == route, winner
            assert winner["original_polarity"] == winner["proof_polarity"] == (
                "REAL" if code == 0 else "UNREAL"), winner
        native_exception_stops(hook, sources["direct"], root)
        source = root / "prepare-budget.tlsf"
        source.write_text(fixtures.REAL.replace("extent = 5", "extent = 2050"))
        for arm in ("both:gr1-lift:oxidd", "both:gr1-real-lift:oxidd", "both:gr1:oxidd",
                    "real:param-lift:oxidd"):
            directory = root / f"prepare-budget-{arm}"
            directory.mkdir()
            args = ["-T", str(source), "--arms", arm]
            baseline = execute(binary, args, None)
            result = execute(binary, args, directory)
            assert (result.returncode, result.stdout) == (baseline.returncode, baseline.stdout)
            assert result.returncode == 2, result
            records = rows(directory)
            stops = [row for row in records if row.get("event") == "route_stopped"]
            assert len(stops) == 1, records
            assert stops[0]["stage"] == "budget-structure" and stops[0]["reason"] == "resource"
            assert not any(row.get("event") in {"decline", "parent_winner"}
                           for row in records), records
            terminal = next(row for row in records if row.get("event") == "terminal_result")
            assert terminal["reason"] == "resource" and terminal["exit_code"] == 2, terminal
        directory = root / "R-resource-stop"
        directory.mkdir()
        result = execute(hook, ["-T", str(sources["R"]), "--arms", "both:gr1-lift:oxidd"],
                         directory, {"ACACIA_NATIVE_TEST_LIFT_FAULT": "r-decline"})
        assert result.returncode == 0, result
        records = rows(directory)
        complete_records(records)
        stopped = [row for row in records if row.get("event") == "route_stopped"]
        assert len(stopped) == 1 and stopped[0]["route"] == "R", records
        assert stopped[0]["stage"] == "schema_capacity" and stopped[0]["reason"] == "resource"
        assert not any(row.get("event") == "decline" and row.get("route") == "R"
                       for row in records), records
        winner = next(row for row in records if row.get("event") == "parent_winner")
        assert winner["route"] == "direct", winner
        directory = root / "binding-error"
        directory.mkdir()
        result = execute(hook, ["-T", str(sources["direct"]), "--arms", "both:gr1-lift:oxidd"],
                         directory, {"ACACIA_NATIVE_TEST_LIFT_FAULT": "source-hash"})
        assert result.returncode == 2, result
        records = rows(directory)
        stops = [row for row in records if row.get("event") == "route_stopped"]
        assert len(stops) == 1 and stops[0]["stage"] == "binding", records
        assert stops[0]["route"] == "direct" and stops[0]["reason"] == "error", records
        assert not any(row.get("event") == "decline" and row.get("route") == "direct"
                       for row in records), records
        assert not any(row.get("event") == "parent_winner" for row in records), records
        # Reuse validation must reject missing/dropped telemetry and late or
        # absent acceptance, regardless of the apparent child verdict.
        confirmed = rows(root / "panel-R")
        for event in ("worker_spec", "worker_start", "route_start", "terminal_result",
                      "parent_terminal", "parent_winner", "writer_summary"):
            incomplete = [row for row in confirmed if row.get("event") != event]
            try:
                complete_records(incomplete)
            except AssertionError:
                pass
            else:
                raise AssertionError(("accepted incomplete attribution", event))
        for marker in ({"event": "incomplete_json"}, {"dropped_records": 1}):
            try:
                complete_records([*confirmed, marker])
            except AssertionError:
                pass
            else:
                raise AssertionError(("accepted lost telemetry", marker))
        for route in ("seed_discovery", "R", "U", "direct", "checking"):
            source = sources["R" if route == "seed_discovery" else "direct"
                             if route == "checking" else route]
            for mode in ("deadline", "kill", "cancel"):
                directory = root / f"interrupt-{route}-{mode}"
                directory.mkdir()
                interrupted(hook, source, directory, route,
                            kill_child=mode == "kill", interrupt_parent=mode == "cancel")
        directory = root / "late"
        directory.mkdir()
        result = execute(integration, ["-f", "G o", "-i", "i", "-o", "o", "--arms",
                                      "real:small:backward"], directory,
                         {"ACACIA_TEST_CHILD_MODES": "real",
                          "ACACIA_TEST_DELAY_AFTER_REAP": "1",
                          "ACACIA_OUTER_DEADLINE_MONOTONIC": str(time.monotonic() + .3)})
        assert result.returncode == 2, result
        assert not any(row.get("event") == "parent_winner" for row in rows(directory))
        assert any(row.get("event") == "parent_terminal"
                   and row["reason"] == "deadline_rejected" for row in rows(directory))
        directory = root / "race-winner"
        directory.mkdir()
        result = execute(integration, ["-f", "G o", "-i", "i", "-o", "o", "--arms",
                                      "real:small:backward,real:small:forward"], directory,
                         {"ACACIA_TEST_CHILD_MODES": "stall,real-delayed"})
        assert result.returncode == 0, result
        records = rows(directory)
        winner = next(row for row in records if row.get("event") == "parent_winner")
        assert winner["worker"] == 1 and winner["requested_backend"] == "forward", winner
        killed = next(row for row in records if row.get("event") == "parent_terminal"
                      and row["worker"] == 0)
        assert killed["worker_pid"] > 0 and killed["telemetry"] == "incomplete", killed
        assert killed["reason"] == "winner_cancelled" and killed["signal"] == signal.SIGKILL
        directory = root / "synthesis"
        directory.mkdir()
        result = execute(integration, ["-f", "G (i <-> X o)", "-i", "i", "-o", "o",
                                      "-s", str(root / "controller.aag"), "--arms",
                                      "real:small:forward"], directory)
        assert result.returncode == 0 and (root / "controller.aag").stat().st_size > 0, result
        complete_records(rows(directory))
        winner = next(row for row in rows(directory) if row.get("event") == "parent_winner")
        assert winner["requested_backend"] == "forward" and winner["effective_backend"] == "backward"
        for arm, formula, code, side in (("real:small:forward", "G (i <-> X o)", 0, "REAL"),
                                        ("unreal:formula:backward", "G (o <-> X i)", 1, "UNREAL")):
            directory = root / f"legacy-{side}"
            directory.mkdir()
            result = execute(integration, ["-f", formula, "-i", "i", "-o", "o", "--arms",
                                          arm], directory)
            assert result.returncode == code, result
            complete_records(rows(directory))
            winner = next(row for row in rows(directory) if row.get("event") == "parent_winner")
            assert winner["original_polarity"] == side and winner["proof_polarity"] == "REAL"
    print("attribution panel, interruptions, and parent acceptance passed")


if __name__ == "__main__":
    main()
