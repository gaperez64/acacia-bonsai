#!/usr/bin/env python3
"""Generated exact-game checks for the selectable weakening route."""

from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def run(command, **kwargs):
    return subprocess.run(command, capture_output=True, text=True, timeout=8, **kwargs)


def main():
    solver, helper, inspect, oracle, scratch = map(Path, sys.argv[1:])
    solver, helper, inspect, oracle = [p.resolve() for p in (solver, helper, inspect, oracle)]
    spec = importlib.util.spec_from_file_location(
        "census", Path(__file__).resolve().parents[1] / "benchmarking/weakening-census.py")
    help_text = run([solver, "--help"]).stdout
    assert "[basic|extended|off] (default basic)" in help_text
    reader = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(reader)
    with tempfile.TemporaryDirectory(dir=scratch) as temporary:
        root = Path(temporary)
        cases = [
            ("GF a -> (G(a -> F b) & G(a -> G !b) & GF c)", False),
            ("(G a & G !a) -> (GF b & FG !b & GF c)", True),
            ("(GF a & GF !a) -> (G(a -> F b) & GF c)", True),
            ("GF a -> (G(a <-> b) & G(a <-> !b) & GF c)", False),
            ("G(a <-> b) & GF c", True),
            ("G(b <-> X a) & GF c", False),
            ("true", True),
            ("GF a -> true", True),
            ("GF a -> G(a -> (F b & G !b & F c))", False),
            ("G((a & X a) -> (X b & X !b & F c))", False),
            ("GF a -> GF(b & c & X !b)", True),
            ("GF a -> F G((a -> b) & (a -> !b) & c)", False),
            ("GF a -> F(b & c & X !b)", True),
        ]
        for index, (formula, expected) in enumerate(cases):
            exact = run([oracle, "-f", formula, "--ins=a,unused_i", "--outs=b,c,unused_o",
                         "--realizability", "--bypass=no", "--decompose=no"])
            assert exact.returncode == (0 if expected else 1), (formula, exact)
            candidates = run([helper, "--extended-candidates", formula])
            assert candidates.returncode == 0, candidates
            derived = candidates.stdout.splitlines()[1:]
            assert len(derived) <= 12
            for candidate in derived:
                verdict = run([oracle, "-f", candidate, "--ins=a,unused_i",
                               "--outs=b,c,unused_o", "--realizability", "--bypass=no",
                               "--decompose=no"])
                assert verdict.returncode in (0, 1)
                if verdict.returncode == 1:
                    assert not expected, (formula, candidate)
            for mode in ("basic", "extended", "off"):
                directory = root / f"raw-{index}-{mode}"
                directory.mkdir()
                command = [solver, "-f", formula, "-i", "a,unused_i", "-o", "b,c,unused_o",
                           "--arms", "real:small:backward,unreal:formula:spot-guarded-sparse",
                           "--weakening", mode]
                actual = run(command, env=dict(os.environ, ACACIA_PHASE_RECORDS=str(directory),
                             ACACIA_OUTER_DEADLINE_MONOTONIC=str(time.monotonic() + 4)))
                assert actual.returncode == (0 if expected else 1), (command, actual)
                assert actual.stdout.strip() == ("REALIZABLE" if expected else "UNREALIZABLE")
        # Singletons are REAL; the first dependency group supplies a checked
        # derived-game proof, then the trusted implication transfers UNREAL.
        for group_index, group_formula in enumerate((
            "(GF a & GF !a) -> (G(a -> F b) & G(a -> G !b) & GF c)",
            "(GF a & GF !a) -> (G(a <-> X b) & G(!a <-> X b) & GF c)",
        )):
            directory = root / f"group-proof-{group_index}"
            directory.mkdir()
            command = [solver, "-f", group_formula, "-i", "a,unused_i", "-o", "b,c,unused_o",
                       "--arms", "unreal:formula:spot-guarded-sparse", "--weakening", "extended",
                       "-K", "3"]
            actual = run(command, env=dict(os.environ, ACACIA_PHASE_RECORDS=str(directory),
                         ACACIA_OUTER_DEADLINE_MONOTONIC=str(time.monotonic() + 4)))
            assert actual.returncode == 1, actual
            off = run(command)
            assert (off.returncode, off.stdout, off.stderr) == (
                actual.returncode, actual.stdout, actual.stderr)
            records, problems = reader.read_records(directory)
            assert not problems
            row, = reader.census(records)
            assert row["census"] == "complete" and row["started"] == 4 and row["successes"] == 1
            assert not row["full_solver_started"] and not row["full_solver_starved"]
            generated = [r for r in records
                         if r.get("event") == "weakening_candidate_generated"]
            assert [(r["candidate_index"], r["obligation_index"], r["run_id"])
                    for r in generated] == [(i, i, i + 1) for i in range(4)], generated
            checked = next(r for r in records if r.get("event") == "weakening_checked_game")
            assert checked["game_fnv1a64"] and checked["proof_fnv1a64"]
            proof = next(r for r in records if r.get("event") == "weakening_proof")
            assert proof["certificate_claim"] == "derived_game_only"
        # A standalone UNREAL worker on a realizable objective must exhaust only
        # its bounded pre-pass, then start the exact original solver with time left.
        formula = "GF a -> (G(a <-> b) & GF c & G(c -> b))"
        assert run([oracle, "-f", formula, "--outs=b,c,unused_o", "--realizability",
                    "--bypass=no", "--decompose=no"]).returncode == 0
        directory = root / "fallback"
        directory.mkdir()
        command = [solver, "-f", formula, "-i", "a,unused_i", "-o", "b,c,unused_o",
                   "--arms", "unreal:formula:spot-guarded-sparse", "--weakening", "extended",
                   "-K", "2"]
        actual = run(command, env=dict(os.environ, ACACIA_PHASE_RECORDS=str(directory),
                     ACACIA_OUTER_DEADLINE_MONOTONIC=str(time.monotonic() + 2)))
        assert actual.returncode == 2, actual
        records, problems = reader.read_records(directory)
        assert not problems
        row, = reader.census(records)
        assert row["mode"] == row["effective_mode"] == "extended", (row, records)
        assert row["census"] == "complete", (row, records)
        assert row["full_solver_started"] and row["full_solver_remaining_ns"] > 0
        assert row["full_solver_starved"] is False
        assert row["observed_started"] > 0, records
        limits = next(r for r in records if r.get("event") == "weakening_budget_limits")
        assert limits["attempt_fraction"] == .05 and limits["prepass_fraction"] == .20
        for deadline in (None, time.monotonic() + 2):
            for attempt_ms, total_ms in ((2000, 8000), (0, 0), (5, 20),
                                         (18_446_744_073_709, 18_446_744_073_709)):
                directory = root / f"allowance-{deadline is None}-{attempt_ms}"
                directory.mkdir()
                env = dict(os.environ, ACACIA_PHASE_RECORDS=str(directory))
                env.pop("ACACIA_OUTER_DEADLINE_MONOTONIC", None)
                if deadline is not None:
                    env["ACACIA_OUTER_DEADLINE_MONOTONIC"] = str(time.monotonic() + 2)
                actual = run(command + ["--weakening-attempt-ms", str(attempt_ms),
                                        "--weakening-total-ms", str(total_ms)], env=env)
                assert actual.returncode == 2, actual
                records, problems = reader.read_records(directory)
                assert not problems
                row, = reader.census(records)
                assert row["full_solver_started"] and row["full_solver_starved"] is False
                limits = next(r for r in records if r.get("event") == "weakening_budget_limits")
                assert limits["attempt_ms"] == attempt_ms and limits["total_ms"] == total_ms
                assert limits["unbounded_attempt_ns"] == attempt_ms * 1_000_000
                assert limits["unbounded_total_ns"] == total_ms * 1_000_000
                assert limits["attempt_override"] and limits["total_override"]
                effective = next(r for r in records if r.get("event") == "weakening_local_budget")
                if deadline is None:
                    assert effective["attempt_limit_ns"] == attempt_ms * 1_000_000
                else:
                    entry = next(r for r in records if r.get("event") == "weakening_entry")
                    assert effective["attempt_limit_ns"] <= entry["remaining_ns"] // 20
                    assert row["full_solver_remaining_ns"] > 0
        for option in ("--weakening-attempt-ms", "--weakening-total-ms"):
            for invalid in ("-1", "1.5", "1ms", "", "+2", "18446744073710"):
                actual = run([solver, "-f", "true", option, invalid])
                assert actual.returncode == 3 and option in actual.stderr, actual
        # Native TLSF normalization supplies the oracle's effective Mealy formula;
        # compare Mealy, Moore, strict, inconsistent and absent assumptions.
        for semantics in ("Mealy", "Moore", "Mealy,Strict", "Moore,Strict"):
            for assumption in ("G F a;", "G a; G !a;", ""):
                path = root / "generated.tlsf"
                path.write_text(f'INFO {{ TITLE: "generated" SEMANTICS: {semantics} '
                                'TARGET: Mealy } MAIN { INPUTS { a; unused_i; } '
                                'OUTPUTS { b; c; unused_o; } '
                                f'ASSUME {{ {assumption} }} '
                                'GUARANTEE { G (a -> F b); G (a -> G !b); G F c; } }')
                frontend = run([inspect, path])
                assert frontend.returncode == 0, frontend
                normalized, inputs, outputs, *_ = frontend.stdout.split("\0")
                exact = run([oracle, "-f", normalized, f"--ins={inputs}", f"--outs={outputs}",
                             "--realizability", "--bypass=no", "--decompose=no"])
                assert exact.returncode in (0, 1), exact
                for mode in ("basic", "extended", "off"):
                    actual = run([solver, "-T", path, "--weakening", mode])
                    assert actual.returncode == exact.returncode, (semantics, assumption, actual)
        # Generated positive G/consequent shapes use exact native normalization,
        # including strict initial framing and both timing conventions.
        for semantics in ("Mealy", "Moore", "Mealy,Strict", "Moore,Strict"):
            path.write_text(f'INFO {{ TITLE: "generated global scope" SEMANTICS: {semantics} '
                            'TARGET: Mealy } MAIN { INPUTS { a; unused_i; } '
                            'OUTPUTS { b; c; unused_o; } INITIALLY { !a; } '
                            'ASSUME { G F a; } '
                            'GUARANTEE { G(a -> (F b && G !b && F c)); } }')
            frontend = run([inspect, path])
            assert frontend.returncode == 0, frontend
            normalized, inputs, outputs, *_ = frontend.stdout.split("\0")
            exact = run([oracle, "-f", normalized, f"--ins={inputs}", f"--outs={outputs}",
                         "--realizability", "--bypass=no", "--decompose=no"])
            assert exact.returncode in (0, 1), exact
            for mode in ("basic", "extended", "off"):
                actual = run([solver, "-T", path, "--weakening", mode])
                assert actual.returncode == exact.returncode, (semantics, actual)
        for semantics in ("Mealy", "Moore", "Mealy,Strict", "Moore,Strict"):
            for guarantee in ("G F(b && c && X !b)", "F G(b && c && X !b)",
                              "F(b && c && X !b)",
                              "G((a && X a) -> ((a -> F b) && (!a -> G !b) && F c))"):
                path.write_text(f'INFO {{ TITLE: "generated future scope" SEMANTICS: {semantics} '
                                'TARGET: Mealy } MAIN { INPUTS { a; unused_i; } '
                                'OUTPUTS { b; c; unused_o; } ASSUME { G F a; } '
                                f'GUARANTEE {{ {guarantee}; }} }}')
                frontend = run([inspect, path])
                assert frontend.returncode == 0, frontend
                normalized, inputs, outputs, *_ = frontend.stdout.split("\0")
                exact = run([oracle, "-f", normalized, f"--ins={inputs}", f"--outs={outputs}",
                             "--realizability", "--bypass=no", "--decompose=no"])
                assert exact.returncode in (0, 1), exact
                for mode in ("basic", "extended", "off"):
                    actual = run([solver, "-T", path, "--weakening", mode])
                    assert actual.returncode == exact.returncode, (semantics, guarantee, actual)
        # A strict weak-until context is declined, while the original exact
        # normalized objective still decides with both modes.
        path.write_text('INFO { TITLE: "generated strict" SEMANTICS: Mealy,Strict TARGET: Mealy } '
                        'MAIN { INPUTS { a; } OUTPUTS { b; c; } REQUIRE { a; } ASSERT { b; } '
                        'ASSUME { G F a; } GUARANTEE { G F b; G F c; } }')
        normalized, inputs, outputs, *_ = run([inspect, path]).stdout.split("\0")
        exact = run([oracle, "-f", normalized, f"--ins={inputs}", f"--outs={outputs}",
                     "--realizability", "--bypass=no", "--decompose=no"])
        assert run([solver, "-T", path, "--weakening", "extended"]).returncode == exact.returncode
        invalid = run([solver, "-f", "true", "--weakening", "invalid"])
        assert invalid.returncode == 3
        assert invalid.stderr == "Error: --weakening expects basic, extended or off.\n"
        # A renamed generated pair must give the same verdict in every mode.
        for formula, expected in (cases[0], cases[8], cases[10]):
            twin = formula.replace("a", "renamed_i").replace("b", "renamed_o").replace(
                "c", "renamed_other")
            for mode in ("basic", "extended", "off"):
                actual = run([solver, "-f", twin, "-i", "renamed_i,unused_i",
                              "-o", "renamed_o,renamed_other,unused_o", "--weakening", mode])
                assert actual.returncode == (0 if expected else 1), actual
        # Synthesis remains a direct original-game operation in extended mode.
        controller = root / "controller.aag"
        actual = run([solver, "-f", "G(a <-> b)", "-i", "a", "-o", "b", "--weakening",
                      "extended", "-s", controller])
        assert actual.returncode == 0 and controller.read_text().startswith("aag "), actual
    print("generated exact games, TLSF timing/strictness, modes and budgeted fallback passed")


if __name__ == "__main__":
    main()
