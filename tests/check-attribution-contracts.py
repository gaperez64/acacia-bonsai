#!/usr/bin/env python3
"""Reach the original reduction/preparation/seed contracts and assert wire telemetry."""
from __future__ import annotations

import importlib.util
from pathlib import Path
import subprocess
import sys
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


def main() -> None:
    hook, api, build = (Path(value).resolve() for value in sys.argv[1:4])
    with tempfile.TemporaryDirectory(dir=build) as temporary:
        root = Path(temporary)
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
                result = attribution.execute(hook, args, directory, env)
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
            result = attribution.execute(hook, ["-T", str(source), "--arms", arm], directory,
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
                result = attribution.execute(hook, ["-T", str(proof_source), "--arms", arm],
                                             directory, {"ACACIA_NATIVE_TEST_LIFT_FAULT": fault})
                records = attribution.rows(directory)
                assert result.returncode == 2, (fault, arm, result)
                stops = [row for row in records if row.get("event") == "route_stopped"]
                assert len(stops) == 1 and stops[0]["reason"] == "error", (fault, arm, records)
                assert not any(row.get("event") == "decline" and
                               row.get("route") == stops[0]["route"] for row in records), records
                attribution.complete_records(records, expected_winners=0)
        directory = root / "direct-proof"
        directory.mkdir()
        result = attribution.execute(hook, ["-T", str(source), "--arms", "both:gr1:oxidd"],
                                     directory, {"ACACIA_NATIVE_TEST_CORRUPT_PROOF": "1"})
        records = attribution.rows(directory)
        assert result.returncode == 2, result
        stops = [row for row in records if row.get("event") == "route_stopped"]
        assert len(stops) == 1 and stops[0]["reason"] == "error", records
        assert not any(row.get("event") == "decline" for row in records), records
    print("All reduction/preparation/seed/proof contract telemetry assertions passed")


if __name__ == "__main__":
    main()
