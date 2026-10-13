#!/usr/bin/env python3
"""Check original-game decisions, routing, failure boundaries and worker cleanup."""
from __future__ import annotations

import argparse
import json
import os
import resource
from pathlib import Path
import signal
import subprocess
import tempfile
import time

from moore_aiger_oracle import Aiger, check


def records(directory: Path) -> list[dict]:
    return [json.loads(line) for path in directory.glob("*.jsonl")
            for line in path.read_text().splitlines() if line.strip()]


def gone(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return True
    return False


def run(binary: Path, args: list[str], root: Path, fault_lib: Path,
        fault: str = "", delay_ms: int = 0, interrupt: bool = False,
        outer_seconds: float = 0, rss_limit: bool = False,
        fifo: Path | None = None, reader_delay: float = 0
        ) -> tuple[subprocess.CompletedProcess, list[dict]]:
    with tempfile.TemporaryDirectory(dir=root) as location:
        directory = Path(location)
        env = os.environ.copy()
        env["ACACIA_PHASE_RECORDS"] = str(directory)
        env.pop("ACACIA_OUTER_DEADLINE_MONOTONIC", None)
        env.pop("ACACIA_TEST_CHILD_MODES", None)
        env.pop("ACACIA_TEST_DELAY_AFTER_REAP", None)
        if fault or delay_ms:
            env["LD_PRELOAD"] = str(fault_lib)
            env["ACACIA_TEST_OBLIGATION_FAULT"] = fault
            env["ACACIA_TEST_OBLIGATION_LEGACY_DELAY_MS"] = str(delay_ms)
        if "-s" in args:
            env["ACACIA_TEST_OBLIGATION_OUTPUT"] = args[args.index("-s") + 1]
        if outer_seconds:
            env["ACACIA_OUTER_DEADLINE_MONOTONIC"] = str(time.monotonic() + outer_seconds)
        process = subprocess.Popen([str(binary), *args], env=env, text=True,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   preexec_fn=(lambda: resource.setrlimit(
                                       resource.RLIMIT_AS, (256 * 1024 * 1024, 256 * 1024 * 1024)))
                                   if rss_limit else None)
        if interrupt:
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                if any(row.get("event") == "obligation_start" for row in records(directory)):
                    break
                if process.poll() is not None:
                    raise AssertionError("parent exited before interruption")
                time.sleep(0.005)
            else:
                process.kill()
                process.communicate()
                raise AssertionError("helper failed to start")
            process.send_signal(signal.SIGTERM)
        reader = None
        if fifo:
            time.sleep(reader_delay)
            exited_before_reader = process.poll() is not None
            reader = os.open(fifo, os.O_RDWR | os.O_NONBLOCK)
        try:
            stdout, stderr = process.communicate(timeout=12)
        except subprocess.TimeoutExpired:
            process.send_signal(signal.SIGTERM)
            try:
                process.communicate(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.communicate()
            raise
        finally:
            if reader is not None:
                os.close(reader)
        if fifo and outer_seconds and reader_delay > outer_seconds:
            assert exited_before_reader, "parent waited for a FIFO reader beyond its deadline"
        rows = records(directory)
        for row in rows:
            if row.get("event", "").startswith("obligation_"):
                assert row["trust"] == "spot_exact" and not row["independent_checked"], row
                assert not row["same_library_checked"], row
        completed = subprocess.CompletedProcess(process.args, process.returncode, stdout, stderr)
        for row in rows:
            if row.get("event") != "worker_spawn":
                continue
            pid = row["worker_pid"]
            assert gone(pid), ("surviving child", row, completed)
            try:
                os.killpg(pid, 0)
            except ProcessLookupError:
                pass
            else:
                raise AssertionError(("surviving process group", pid))
        return completed, rows


def helper_rows(rows: list[dict], event: str) -> list[dict]:
    return [row for row in rows if row.get("event") == "obligation_" + event]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("unit", type=Path)
    parser.add_argument("fault_lib", type=Path)
    parser.add_argument("build_dir", type=Path)
    parser.add_argument("--ltlsynt", type=Path)
    parser.add_argument("--tlsf", action="store_true")
    parser.add_argument("--native", action="store_true")
    parser.add_argument("--default-arms", default="")
    options = parser.parse_args()
    binary, library = options.binary.resolve(), options.fault_lib.resolve()
    root = options.build_dir.resolve()

    unit = subprocess.run([str(options.unit.resolve())], capture_output=True, text=True, check=True)
    if options.ltlsynt:
        for line in unit.stdout.splitlines():
            if "\t" not in line:
                continue
            timing, formula, expected = line.split("\t")
            oracle = subprocess.run(
                [str(options.ltlsynt), "--obligation-synthesis=no", "--bypass=no", "--realizability",
                 "--ins=i,unused_input", "--outs=o,unused_output", "-f", formula,
                 *(["--semantics=Moore"] if timing == "moore" else [])],
                capture_output=True, text=True, timeout=10)
            assert oracle.returncode == (0 if expected == "completed_REAL" else 1), oracle

    arm = "both:obligation:spot"
    alone = ["--arms", arm]
    source = ["-f", "G (i <-> o)", "-i", "i", "-o", "o"]
    ordinary_pair = "real:small:backward,unreal:formula:backward"
    pair = ["--arms", ordinary_pair]
    race = ["--arms", ordinary_pair + "," + arm]
    real = ["--arms", "real:small:backward," + arm]
    default_arms = options.default_arms.split(",") if options.default_arms else []
    default_has_helper = arm in default_arms
    ordinary_defaults = [value for value in default_arms
                         if value != arm and ":oxidd" not in value]
    previous = ["--arms", ",".join(ordinary_defaults)] if ordinary_defaults else []
    # Explicit ordinary selections omit all obligation work.
    for selection in (previous, pair):
        result, rows = run(binary, [*source, *selection], root, library)
        assert result.returncode == 0 and "REALIZABLE" in result.stdout, result
        assert not any(row.get("event", "").startswith("obligation_") for row in rows)
        expected_workers = 2 if selection == pair else len(ordinary_defaults)
        if expected_workers:
            assert len([row for row in rows if row.get("event") == "worker_spawn"]) == (
                expected_workers)

    if not default_has_helper:
        result, rows = run(binary, source, root, library)
        assert result.returncode == 0, result
        assert not any(row.get("event", "").startswith("obligation_") for row in rows)

    for formula, expected in [("G (i <-> o)", 0), ("G (X i <-> o)", 1)]:
        args = ["-f", formula, "-i", "i", "-o", "o"]
        exact, exact_rows = run(binary, [*args, *previous], root, library)
        assert not any(row.get("event", "").startswith("obligation_") for row in exact_rows)
        selections = [[], race, alone] if default_has_helper else [race, alone]
        for selection in selections:
            candidate, rows = run(binary, [*args, *selection], root, library, delay_ms=300)
            assert exact.returncode == candidate.returncode == expected, (exact, candidate)
            winners = [row for row in rows if row.get("event") == "parent_winner"]
            assert len(winners) == 1 and winners[0]["effective_backend"] == "obligation", rows
            finish = helper_rows(rows, "completion")
            assert len(finish) == 1 and finish[0]["outcome"] == (
                "completed_REAL" if expected == 0 else "completed_UNREAL"), rows
            assert not finish[0]["same_library_checked"]
            assert not finish[0]["independent_checked"]
            assert finish[0]["trust"] == "spot_exact"
            assert all(row["arm"] == arm for row in rows
                       if row.get("event", "").startswith("obligation_")), rows
            helper_specs = [row for row in rows if row.get("kind") == arm]
            assert len(helper_specs) == 1, rows
            spawns = [row for row in rows if row.get("event") == "worker_spawn"]
            assert helper_specs[0]["worker"] == len(spawns) - 1, rows
            if selection == alone:
                assert len(spawns) == 1, rows
            elif selection == race:
                assert len(spawns) == 3, rows

    for formula, arms, expected in [
            ("G (i <-> o)", "unreal:formula:backward", 0),
            ("G (X i <-> o)", "real:small:backward", 1)]:
        result, rows = run(binary, ["-f", formula, "-i", "i", "-o", "o",
                                   "--arms", arms + "," + arm], root, library, delay_ms=300)
        assert result.returncode == expected, result
        assert [row for row in rows if row.get("event") == "parent_winner"][0][
            "effective_backend"] == "obligation", rows

    ineligible_source = ["-f", "G F (i <-> o)", "-i", "i", "-o", "o"]
    baseline, baseline_rows = run(binary, [*ineligible_source, *previous], root, library)
    for selection in ([], race, alone):
        ineligible, rows = run(binary, [*ineligible_source, *selection], root, library)
        assert ineligible.returncode == (2 if selection == alone else 0), ineligible
        spawns = [row for row in rows if row.get("event") == "worker_spawn"]
        if selection == alone:
            assert len(spawns) == 1, rows
            assert helper_rows(rows, "start")[0]["reason"] == "preparation", rows
            assert helper_rows(rows, "completion")[0]["outcome"] == "inapplicable", rows
            assert not any(row.get("stage") == "obligation_synthesis" for row in rows), rows
        else:
            assert not helper_rows(rows, "start"), rows
            assert not any(row.get("kind") == arm for row in rows), rows
        if selection:
            assert helper_rows(rows, "eligibility")[0]["outcome"] == "inapplicable", rows
        else:
            assert not any(row.get("event", "").startswith("obligation_") for row in rows)
            assert (ineligible.returncode, ineligible.stdout) == (
                baseline.returncode, baseline.stdout)
            def specs(records):
                return sorted(({key: value for key, value in row.items()
                                if key != "worker_pid"} for row in records
                               if row.get("event") == "worker_spec"),
                              key=lambda row: row["worker"])
            assert specs(rows) == specs(baseline_rows), (rows, baseline_rows)
        if selection == race:
            assert len(spawns) == 2, rows

    # One ordinary arm makes every record deterministic apart from sampled runtime values.
    # Compare ineligible helper selection with the same ordinary arm alone, including
    # phases, metrics, terminal records and the parent winner.
    sampled = {"worker_pid", "pid", "mono_ns", "entry_ns", "deadline_ns",
               "wall_ns", "cpu_ns", "rss_kb",
               "peak_rss_kb", "mallinfo2"}

    def stable_records(rows: list[dict]) -> list[str]:
        return sorted(json.dumps({key: value for key, value in row.items() if key not in sampled},
                                 sort_keys=True) for row in rows)

    for formula, ordinary in [("G F (i <-> o)", "real:small:backward"),
                              ("F G (i <-> o)", "unreal:formula:backward")]:
        args = ["-f", formula, "-i", "i", "-o", "o"]
        exact, exact_rows = run(binary, [*args, "--arms", ordinary], root, library)
        default, default_rows = run(binary, [*args, "--arms", ordinary + "," + arm],
                                    root, library)
        assert not helper_rows(default_rows, "start"), default_rows
        eligibility = helper_rows(default_rows, "eligibility")
        assert len(eligibility) == 1 and eligibility[0]["outcome"] == "inapplicable"
        # Explicit selection adds this one eligibility record to the writer's count.
        exact_writer = next(row for row in exact_rows if row.get("event") == "writer_summary")
        selected_writer = next(row for row in default_rows
                               if row.get("event") == "writer_summary")
        assert selected_writer["delivered_records"] == exact_writer["delivered_records"] + 1
        selected_writer["delivered_records"] -= 1
        default_rows = [row for row in default_rows
                        if row.get("event") != "obligation_eligibility"]
        assert (default.returncode, default.stdout, default.stderr, stable_records(default_rows)) == (
            exact.returncode, exact.stdout, exact.stderr, stable_records(exact_rows)), (
                default_rows, exact_rows)

    for route in (["-r", "small"], ["-u", "formula"]):
        result, rows = run(binary, [*source, *route], root, library)
        assert not any(row.get("event", "").startswith("obligation_") for row in rows)

    for marker in (".inputs", ".outputs"):
        for partition in ("-i", "-o"):
            args = ["-f", "G o", "-i", "i", "-o", "o", partition, marker]
            exact, _ = run(binary, [*args, *previous], root, library)
            selected, rows = run(binary, [*args, *alone], root, library, "prepare-stall")
            assert exact.returncode == selected.returncode == 3, (exact, selected)
            message = f"value '{marker}' is a partition marker, not an atomic proposition."
            assert message in exact.stderr and message in selected.stderr, (exact, selected)
            assert not helper_rows(rows, "start"), rows

    for fault, outcome in [("prepare-exception", "error"), ("prepare-allocation", "exhausted"),
                           ("prepare-real-exit", "error"), ("prepare-unreal-exit", "error")]:
        result, rows = run(binary, [*source, *alone], root, library, fault)
        assert result.returncode == 2 and "REALIZABLE" not in result.stdout, result
        assert helper_rows(rows, "completion")[0]["outcome"] == outcome, (fault, rows)
        assert not any(row.get("event") == "parent_winner" for row in rows), rows
    for interruption in (False, True):
        started = time.monotonic()
        result, rows = run(binary, [*source, *alone], root, library, "prepare-stall",
                           outer_seconds=0 if interruption else 0.2, interrupt=interruption)
        assert time.monotonic() - started < 2, result
        assert result.returncode == 2, result
        finish = helper_rows(rows, "completion")
        assert len(finish) == 1 and finish[0]["outcome"] == "cancelled", rows
        assert finish[0]["reason"] == ("interrupted" if interruption else "deadline"), rows
        assert not any(row.get("event") == "parent_winner" for row in rows), rows
    result, rows = run(binary, [*source, *alone], root, library, "decision-contract")
    assert result.returncode == 0, result
    result, rows = run(binary, [*source, *alone], root, library, "slow-decision",
                       outer_seconds=9)
    assert result.returncode == 0, result
    finish = helper_rows(rows, "completion")
    assert finish[0]["wall_ns"] > 5_000_000_000 and finish[0]["outcome"] == "completed_REAL"
    # Eligibility uses the post-simplification original objective: GF i becomes false.
    simplified, rows = run(binary, ["-f", "G F i", "-i", "i", "-o", "o", *alone],
                           root, library)
    assert simplified.returncode == 1, simplified
    assert helper_rows(rows, "eligibility")[0]["reason"] == "eligible", rows

    for fault, outcome in [("null", "error"), ("empty", "error"), ("exception", "error"),
                           ("allocation", "exhausted"), ("signal", "error"),
                           ("unreal-exit", "error"), ("real-exit", "error")]:
        result, rows = run(binary, [*source, *real], root, library, fault, 300)
        assert result.returncode == 0, result
        finish = helper_rows(rows, "completion")
        assert len(finish) == 1 and finish[0]["outcome"] == outcome, (fault, rows)
        assert all(row.get("effective_backend") != "obligation"
                   for row in rows if row.get("event") == "parent_winner"), rows

    result, rows = run(binary, [*source, *real], root, library, "stall", 300)
    assert result.returncode == 0, result
    finish = helper_rows(rows, "completion")
    assert len(finish) == 1 and finish[0]["outcome"] == "cancelled", rows
    assert finish[0]["reason"] == "winner_cancelled", rows
    result, rows = run(binary, [*source, *real], root, library, "stall", 7000)
    assert result.returncode == 0, result
    finish = helper_rows(rows, "completion")
    assert len(finish) == 1 and finish[0]["outcome"] == "cancelled", rows
    assert finish[0]["reason"] == "winner_cancelled" and finish[0]["wall_ns"] > 5_000_000_000
    result, rows = run(binary, [*source, *real], root, library, "memory", 300,
                       rss_limit=True)
    assert result.returncode == 0, result
    finish = helper_rows(rows, "completion")
    assert len(finish) == 1 and finish[0]["outcome"] == "exhausted", rows
    assert finish[0]["reason"] == "rss_share", rows
    result, rows = run(binary, [*source, *real], root, library, "stall", 7000,
                       outer_seconds=0.2)
    assert result.returncode == 2, result
    assert helper_rows(rows, "completion")[0]["outcome"] == "cancelled", rows
    result, rows = run(binary, [*source, *real], root, library, "stall", 7000, interrupt=True)
    assert result.returncode == 2, result
    assert helper_rows(rows, "completion")[0]["outcome"] == "cancelled", rows

    with tempfile.TemporaryDirectory(dir=root) as directory:
        controller = Path(directory) / "controller.aag"
        result, rows = run(binary, [*source, "-r", "small", "-s", str(controller)],
                           root, library, "exception")
        assert result.returncode == 0 and controller.read_text().startswith("aag"), result
        assert not helper_rows(rows, "eligibility"), rows
        assert not helper_rows(rows, "start"), rows
        assert len([row for row in rows if row.get("event") == "worker_spawn"]) == 1
        # Delay only the ordinary exporter so the helper's success/failure is observed.
        synth_source = ["-f", "o & G o", "-i", "i", "-o", "o"]
        for selection in (alone, real):
            result, rows = run(binary, [*synth_source, *selection, "-s", str(controller)],
                               root, library, delay_ms=300)
            assert result.returncode == 0 and controller.read_text().startswith("aag"), result
            winner = next(row for row in rows if row.get("event") == "parent_winner")
            assert winner["effective_backend"] == "obligation", rows
            finish = helper_rows(rows, "completion")
            assert not finish[0]["independent_checked"] and not finish[0]["same_library_checked"]
            assert finish[0]["trust"] == "spot_exact"
        def verify_controller():
            oracle = check(Aiger.read(controller), 0,
                           lambda state, letter: state if letter["o"] else None, moore=False)
            assert oracle.valid, oracle

        # Both workers must serialize through an ofstream opened on the requested
        # destination. The interposer rejects buffered or parent-side serialization.
        for inputs in (["-i", ""], ["-i", "i"]):
            outputs = []
            for selection, backend in ((alone, "obligation"),
                                       (["--arms", "real:small:backward"], "backward")):
                result, rows = run(binary, ["-f", "G o", *inputs, "-o", "o", *selection,
                                           "-s", str(controller)],
                                   root, library, "writer-contract", delay_ms=300)
                assert result.returncode == 0, result
                winner = next(row for row in rows if row.get("event") == "parent_winner")
                assert winner["effective_backend"] == backend, rows
                verify_controller()
                outputs.append(controller.read_bytes())
            assert outputs[0] == outputs[1], "arm and ordinary AIGER formats differ"

        # A symlink follows the same ofstream behavior as ordinary synthesis.
        target = Path(directory) / "target.aag"
        link = Path(directory) / "linked.aag"
        link.symlink_to(target)
        result, rows = run(binary, [*synth_source, *alone, "-s", str(link)],
                           root, library, delay_ms=300)
        assert result.returncode == 0 and link.is_symlink(), result
        assert next(row for row in rows if row.get("event") == "parent_winner")[
            "effective_backend"] == "obligation", rows
        assert check(Aiger.read(target), 0,
                     lambda state, letter: state if letter["o"] else None, moore=False).valid

        # A blocked destination remains in the worker under ordinary supervision.
        fifo = Path(directory) / "controller.fifo"
        os.mkfifo(fifo)
        result, rows = run(binary, [*synth_source, *alone, "-s", str(fifo)],
                           root, library, delay_ms=300, outer_seconds=.2,
                           fifo=fifo, reader_delay=.4)
        assert result.returncode == 2 and "REALIZABLE" not in result.stdout, result
        assert helper_rows(rows, "completion")[0]["outcome"] == "cancelled", rows
        assert not any(row.get("event") == "parent_winner" for row in rows), rows
        # There is no runtime product check. Test-only corruptions are accepted as
        # trusted results, and the offline oracle detects their bad controllers.
        for fault in ("wrong-strategy", "corrupt-controller", "check-exception"):
            result, rows = run(binary, [*synth_source, *alone, "-s", str(controller)],
                               root, library, fault, delay_ms=300)
            assert result.returncode == 0, (fault, result)
            assert next(row for row in rows if row.get("event") == "parent_winner")[
                "effective_backend"] == "obligation", rows
            assert helper_rows(rows, "completion")[0]["trust"] == "spot_exact", rows
            oracle = check(Aiger.read(controller), 0,
                           lambda state, letter: state if letter["o"] else None, moore=False)
            assert oracle.valid == (fault == "check-exception"), (fault, oracle)
            assert not any(row.get("stage") == "obligation_independent_check" for row in rows)
        for fault in ("export-open", "export-exception", "export-stream", "null", "exception"):
            controller.unlink(missing_ok=True)
            result, rows = run(binary, [*synth_source, *alone, "-s", str(controller)],
                               root, library, fault, delay_ms=300)
            assert result.returncode == 0 and controller.exists(), (fault, result)
            finish = helper_rows(rows, "completion")
            assert finish[0]["outcome"] == "error" and not finish[0]["independent_checked"], rows
            verify_controller()
            winner = next(row for row in rows if row.get("event") == "parent_winner")
            assert winner["effective_backend"] != "obligation", rows
            assert any(row.get("event") == "worker_spawn"
                       and row["worker_pid"] == winner["worker_pid"]
                       and row["mono_ns"] < finish[0]["mono_ns"] for row in rows), rows
        # Synthesis has the ordinary race/invocation bound, with no local allowance.
        result, rows = run(binary, [*synth_source, *alone, "-s", str(controller)],
                           root, library, "slow-decision", delay_ms=7000, outer_seconds=9)
        assert result.returncode == 0, result
        finish = helper_rows(rows, "completion")
        assert finish[0]["outcome"] == "completed_REAL" and finish[0]["wall_ns"] > 5_000_000_000
        assert next(row for row in rows if row.get("event") == "parent_winner")[
            "effective_backend"] == "obligation", rows
        verify_controller()
        for fault in ("stall", "export-stall"):
            result, rows = run(binary, [*synth_source, *alone, "-s", str(controller)],
                               root, library, fault, delay_ms=300)
            assert result.returncode == 0, result
            finish = helper_rows(rows, "completion")
            assert finish[0]["outcome"] == "cancelled" and finish[0]["reason"] == "winner_cancelled"
            verify_controller()
        for interrupted in (False, True):
            result, rows = run(binary, [*synth_source, *alone, "-s", str(controller)],
                               root, library, "export-stall", delay_ms=6000,
                               outer_seconds=0 if interrupted else .2, interrupt=interrupted)
            assert result.returncode == 2 and "REALIZABLE" not in result.stdout, result
            assert not any(row.get("event") == "parent_winner" for row in rows), rows
            finish = helper_rows(rows, "completion")
            assert finish[0]["outcome"] == "cancelled", rows
            assert finish[0]["reason"] == ("interrupted" if interrupted else "deadline"), rows
        # Ineligible and losing synthesis inputs use the existing route, even arm-alone.
        for formula, expected in (("G F (i <-> o)", 0), ("G i", 1)):
            result, rows = run(binary, ["-f", formula, "-i", "i", "-o", "o", *alone,
                                       "-s", str(controller)], root, library, delay_ms=300)
            assert result.returncode == expected, result
            assert next(row for row in rows if row.get("event") == "parent_winner")[
                "effective_backend"] != "obligation", rows
        if options.tlsf:
            malformed = Path(directory) / "malformed.tlsf"
            malformed.write_text("invalid TLSF")
            failed, _ = run(binary, ["-T", str(malformed), *pair], root, library)
            selected, selected_rows = run(binary, ["-T", str(malformed), *race], root, library)
            assert failed.returncode == selected.returncode == 3, (failed, selected)
            assert failed.stdout == selected.stdout and failed.stderr == selected.stderr
            assert "TLSF conversion failed" in selected.stderr, selected
            assert not any(row.get("event") == "worker_spawn" for row in selected_rows)
            for semantics, expected in [("Mealy", 0), ("Moore", 1), ("Strict,Mealy", 0),
                                        ("Strict,Moore", 1)]:
                tlsf = Path(directory) / "source.tlsf"
                tlsf.write_text(f'''INFO {{ TITLE: "generated" SEMANTICS: {semantics} TARGET: Moore }}
MAIN {{ INPUTS {{ i; }} OUTPUTS {{ o; }} GUARANTEES {{ G (i <-> o); }} }}
''')
                exact, _ = run(binary, ["-T", str(tlsf), *pair], root, library)
                result, rows = run(binary, ["-T", str(tlsf), *race], root, library, delay_ms=300)
                assert exact.returncode == result.returncode == expected, (exact, result)
                assert helper_rows(rows, "completion")[0]["outcome"] == (
                    "completed_REAL" if expected == 0 else "completed_UNREAL"), rows
            # Safety assumptions exercise the strict normalized objective too,
            # rather than only using a strict label on an assumption-free game.
            for semantics in ("Mealy", "Moore", "Strict,Mealy", "Strict,Moore"):
                tlsf.write_text(f'''INFO {{ TITLE: "generated" SEMANTICS: {semantics} TARGET: Mealy }}
MAIN {{ INPUTS {{ i; }} OUTPUTS {{ o; }}
       ASSUMPTIONS {{ G i; }} GUARANTEES {{ G (i <-> o); }} }}
''')
                exact, _ = run(binary, ["-T", str(tlsf), *pair], root, library)
                result, rows = run(binary, ["-T", str(tlsf), *race], root, library, delay_ms=300)
                assert exact.returncode in (0, 1) and result.returncode == exact.returncode, (
                    exact, result)
                finish = helper_rows(rows, "completion")
                assert len(finish) == 1 and finish[0]["outcome"] == (
                    "completed_REAL" if exact.returncode == 0 else "completed_UNREAL"), rows
            if options.native:
                tlsf.write_text('''INFO { TITLE: "generated" SEMANTICS: Mealy TARGET: Mealy }
MAIN { INPUTS { i; } OUTPUTS { o; } GUARANTEES { G (i <-> o); } }
''')
                selected, selected_rows = run(
                    binary, ["-T", str(tlsf), "--arms", "both:gr1:oxidd"], root, library)
                assert selected.returncode == 0, selected
                assert not helper_rows(selected_rows, "eligibility"), selected_rows
                assert len([row for row in selected_rows if row.get("event") == "worker_spawn"]) == 1
                result, rows = run(binary, ["-T", str(tlsf), "--arms", "both:gr1:oxidd," + arm],
                                   root, library)
                assert result.returncode == 0, result
                assert helper_rows(rows, "eligibility")[0]["reason"] == "eligible", rows

    for selection in (["--obligation-helper", "off"],
                      ["--obligation-helper", "on"],
                      ["--arms", arm + "," + arm]):
        invalid = subprocess.run([str(binary), *source, *selection],
                                 capture_output=True, text=True)
        assert invalid.returncode == 3, invalid
        if selection[0] == "--obligation-helper":
            assert "unrecognized option" in invalid.stderr, invalid
    print("obligation arm selection, routing, oracle, faults, synthesis and cleanup checks passed")


if __name__ == "__main__":
    main()
