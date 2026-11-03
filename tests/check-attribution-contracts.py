#!/usr/bin/env python3
"""Reach the original reduction/preparation/seed contracts and assert wire telemetry."""
from __future__ import annotations

import argparse
import importlib.util
import os
from pathlib import Path
import select
import signal
import subprocess
import tempfile

from typing import NamedTuple

spec = importlib.util.spec_from_file_location("attribution", Path(__file__).with_name(
    "check-attribution.py"))
assert spec and spec.loader
attribution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(attribution)


class Contract(NamedTuple):
    fault: str
    stage: str
    reason: str = "error"


REDUCTION_CASES = (
    Contract("source-duplicate", "signals"),
    Contract("formula-ap", "formula"),
    Contract("monitor-incomplete", "monitor"),
    Contract("monitor-nondeterministic", "monitor"),
    Contract("monitor-acceptance", "monitor"),
    Contract("monitor-empty", "monitor"),
    Contract("monitor-sticky", "monitor"),
    Contract("transition-ap", "transition"),
    Contract("transition-temporal", "transition"),
    Contract("provenance-boolean", "provenance"),
    Contract("unsupported-class", "mp-class", "decline"),
)
PREPARATION_CASES = (
    Contract("provenance-duplicate", "provenance"),
    Contract("provenance-retry", "mp-class"),
)
PROOF_CASES = (
    "source-hash", "swap-game", "swapped-game-with-matching-hash", "swap-certificate",
    "wrong-side", "wrong-method", "missing-policy-hash", "policy-hash",
    "unexpected-region-policy-hash", "corrupt-proof",
)
ARMS = ("both:gr1-lift:oxidd", "both:gr1-real-lift:oxidd", "real:param-lift:oxidd")
SOURCE = '''INFO { TITLE: "contract" SEMANTICS: Mealy TARGET: Mealy }
GLOBAL { PARAMETERS { extent = 5; } }
MAIN { INPUTS { demand[extent]; } OUTPUTS { response[extent]; }
GUARANTEES { G (||[0 <= i < extent] response[i]); } }
'''


def stopped(result, records: list[dict], cause: Contract, *, decline: bool = False) -> None:
    assert result.returncode == 2, result
    events = [row for row in records if row.get("event") in {"decline", "route_stopped"}]
    assert len(events) == 1, (cause, events, result.stderr)
    expected = "decline" if decline else "route_stopped"
    assert events[0]["event"] == expected and events[0]["stage"] == cause.stage, events
    if not decline:
        assert events[0]["reason"] == cause.reason, events
    terminal = next(row for row in records if row.get("event") == "terminal_result")
    assert terminal["reason"] == (cause.stage if decline else cause.reason), terminal
    attribution.complete_records(records, expected_winners=0)


def recorded_env(acknowledgement: Path | None, extra: dict | None = None) -> dict:
    env = dict(extra or {})
    if acknowledgement is not None:
        preload = env.get("LD_PRELOAD", os.environ.get("LD_PRELOAD", ""))
        env["LD_PRELOAD"] = " ".join(filter(None, (preload, str(acknowledgement))))
        env["ACACIA_TEST_RECORD_ACK"] = "1"
    return env


def gated_delivery(worker: Path, acknowledgement: Path, root: Path) -> None:
    # Hold the reader before its first append. The ordinary bounded close can
    # then lose every pipe row without reporting any producer-side drops.
    for reliable in (False, True):
        directory = root / ("acked-writer" if reliable else "unacked-writer")
        directory.mkdir()
        gate_read, gate_write = os.pipe()
        notify_read, notify_write = os.pipe()
        owned = {gate_read, gate_write, notify_read, notify_write}

        def close(fd):
            os.close(fd)
            owned.remove(fd)

        env = os.environ.copy()
        env.pop("ACACIA_TEST_RECORD_ACK", None)
        env.pop("ACACIA_TEST_OBLIGATION_RECORD_ACK", None)
        env.update(ACACIA_PHASE_RECORDS=str(directory),
                   ACACIA_TEST_RECORD_WRITER_GATE_FD=str(gate_read))
        if reliable:
            env.update(recorded_env(acknowledgement, {
                "ACACIA_TEST_RECORD_ACK_NOTIFY_FD": str(notify_write)}))
        process = subprocess.Popen([str(worker)], env=env, text=True, stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   pass_fds=(gate_read, notify_write), start_new_session=True)
        finished = False
        try:
            close(gate_read)
            close(notify_write)
            if reliable:
                process.stdin.write("x")
                process.stdin.flush()
                ready, _, _ = select.select([notify_read], [], [], 3)
                assert ready and os.read(notify_read, 1) == b"q", "producer did not queue a packet"
                assert process.poll() is None, "producer did not wait for append"
                assert not (directory / f"{process.pid}.jsonl").exists(), "writer bypassed its gate"
                close(gate_write)
                stdout, stderr = process.communicate(timeout=3)
            else:
                stdout, stderr = process.communicate("x", timeout=3)
            assert process.returncode == 0 and not stderr and "REALIZABLE" in stdout, (stdout, stderr)
            records = attribution.rows(directory)
            if reliable:
                assert all(row.get("dropped_records", 0) == 0 for row in records), records
                assert len([row for row in records if row.get("phase") == "record_failure_test"]) == 1, records
                assert len([row for row in records if row.get("phase") == "record_summary"]) == 2, records
                writer, = [row for row in records if row.get("event") == "writer_summary"]
                assert writer["failed_records"] == 0 and not writer["incomplete_packet"], writer
                assert writer["delivered_records"] == sum(not row.get("durable", False) for row in records) - 1
            else:
                assert len(records) == 1 and records[0]["event"] == "parent_record_summary", records
                assert records[0]["dropped_records"] == 0, records
            finished = True
        finally:
            if not finished:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.communicate(timeout=3)
            for fd in list(owned):
                close(fd)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("hook", "api", "build"):
        parser.add_argument(name, type=Path)
    parser.add_argument("--record-ack-library", type=Path)
    parser.add_argument("--record-failure-worker", type=Path)
    options = parser.parse_args()
    hook, api, build = (getattr(options, name).resolve() for name in ("hook", "api", "build"))
    acknowledgement = options.record_ack_library.resolve() if options.record_ack_library else None
    assert bool(acknowledgement) == bool(options.record_failure_worker)

    def recorded(args, directory, extra=None):
        return attribution.execute(hook, args, directory, recorded_env(acknowledgement, extra))

    with tempfile.TemporaryDirectory(dir=build) as temporary:
        root = Path(temporary)
        if acknowledgement is not None:
            gated_delivery(options.record_failure_worker.resolve(), acknowledgement, root)
        source = root / "contract.tlsf"
        source.write_text(SOURCE)
        # Every added contract mutation reaches the actual validator/check.
        for cause in (*REDUCTION_CASES, *PREPARATION_CASES):
            result = subprocess.run([str(api), cause.fault, str(source)], capture_output=True,
                                    text=True, timeout=20)
            assert result.returncode == 0, (cause, result)
            arms = ARMS if cause in PREPARATION_CASES else (*ARMS, "both:gr1:oxidd")
            for arm in arms:
                directory = root / f"{cause.fault}-{arm}"
                directory.mkdir()
                env = {"ACACIA_NATIVE_TEST_CONTRACT": cause.fault}
                args = ["-T", str(source), "--arms", arm]
                baseline = attribution.execute(hook, args, None, env)
                result = recorded(args, directory, env)
                assert (result.returncode, result.stdout) == (baseline.returncode, baseline.stdout)
                stage = "provenance" if cause.fault == "provenance-retry" and arm != ARMS[2] else cause.stage
                stopped(result, attribution.rows(directory), cause._replace(stage=stage),
                        decline=cause.reason == "decline")
        result = subprocess.run([str(api), "provenance-recover", str(source)],
                                capture_output=True, text=True, timeout=20)
        assert result.returncode == 0, result
        # Shared-seed validator rejection retains a verified direct fallback in both modes.
        for arm in ARMS[:2]:
            directory = root / f"seed-null-{arm}"
            directory.mkdir()
            result = recorded(["-T", str(source), "--arms", arm], directory,
                              {"ACACIA_NATIVE_TEST_CONTRACT": "seed-null"})
            assert result.returncode == 0, result
            records = attribution.rows(directory)
            stops = [row for row in records if row.get("event") == "route_stopped"]
            assert len(stops) == 1 and stops[0]["stage"] == "seed_solve", records
            assert stops[0]["reason"] == "error", stops
            assert not any(row.get("event") == "decline" for row in records), records
            attribution.complete_records(records)
            winner = next(row for row in records if row.get("event") == "parent_winner")
            assert winner["route"] == "direct", winner
        # Every Acacia proof corruption hook must be a STOPPED integrity error.
        fixtures = root / "proof.tlsf"
        fixtures.write_text(SOURCE.replace("G (||[0 <= i < extent] response[i])",
                                           "&&[0 <= i < extent] G F response[i]"))
        for fault in PROOF_CASES:
            for arm in ARMS:
                directory = root / f"proof-{fault}-{arm}"
                directory.mkdir()
                proof_source = fixtures
                if fault == "missing-policy-hash" and arm != ARMS[2]:
                    proof_source = root / "direct-proof.tlsf"
                    proof_source.write_text(SOURCE.replace(
                        "GLOBAL { PARAMETERS { extent = 5; } }", "").replace("extent", "5"))
                result = recorded(["-T", str(proof_source), "--arms", arm], directory,
                                  {"ACACIA_NATIVE_TEST_LIFT_FAULT": fault})
                records = attribution.rows(directory)
                assert result.returncode == 2, (fault, arm, result)
                stops = [row for row in records if row.get("event") == "route_stopped"]
                assert len(stops) == 1 and stops[0]["reason"] == "error", (fault, arm, records)
                assert not any(row.get("event") == "decline" and
                               row.get("route") == stops[0]["route"] for row in records), records
                attribution.complete_records(records, expected_winners=0)
        directory = root / "direct-proof"
        directory.mkdir()
        result = recorded(["-T", str(source), "--arms", "both:gr1:oxidd"], directory,
                          {"ACACIA_NATIVE_TEST_CORRUPT_PROOF": "1"})
        records = attribution.rows(directory)
        assert result.returncode == 2, result
        stops = [row for row in records if row.get("event") == "route_stopped"]
        assert len(stops) == 1 and stops[0]["reason"] == "error", records
        assert not any(row.get("event") == "decline" for row in records), records
    print("All reduction/preparation/seed/proof contract telemetry assertions passed")


if __name__ == "__main__":
    main()
