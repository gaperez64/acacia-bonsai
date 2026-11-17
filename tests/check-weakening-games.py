#!/usr/bin/env python3
"""Compare basic/off weakening and implicit defaults with the exact parity oracle."""

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
    solver, inspect, oracle, scratch = map(Path, sys.argv[1:])
    solver, inspect, oracle = [p.resolve() for p in (solver, inspect, oracle)]
    spec = importlib.util.spec_from_file_location(
        "census", Path(__file__).resolve().parents[1] / "benchmarking/weakening-census.py")
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
            ("(GF a & GF !a) -> (G(a -> F b) & G(a -> G !b) & GF c)", False),
            ("(GF a & GF !a) -> (G(a <-> X b) & G(!a <-> X b) & GF c)", False),
        ]
        for index, (formula, expected) in enumerate(cases):
            exact = run([oracle, "-f", formula, "--ins=a,unused_i", "--outs=b,c,unused_o",
                         "--realizability", "--bypass=no", "--decompose=no"])
            assert exact.returncode == (0 if expected else 1), (formula, exact)
            for mode in ("default", "basic", "off"):
                directory = root / f"raw-{index}-{mode}"
                directory.mkdir()
                command = [solver, "-f", formula, "-i", "a,unused_i", "-o", "b,c,unused_o",
                           "--arms", "real:small:backward,unreal:formula:spot-guarded-sparse",
                           *([] if mode == "default" else ["--weakening", mode])]
                actual = run(command, env=dict(os.environ, ACACIA_PHASE_RECORDS=str(directory),
                             ACACIA_OUTER_DEADLINE_MONOTONIC=str(time.monotonic() + 4)))
                assert actual.returncode == (0 if expected else 1), (command, actual)
                assert actual.stdout.strip() == ("REALIZABLE" if expected else "UNREALIZABLE")
        # Both modes must fall back to the full objective when basic's threshold
        # is not met; the standalone UNREAL worker cannot prove this REAL game.
        formula = "GF a -> (G(a <-> b) & GF c & G(c -> b))"
        assert run([oracle, "-f", formula, "--ins=a,unused_i", "--outs=b,c,unused_o",
                    "--realizability", "--bypass=no", "--decompose=no"]).returncode == 0
        for mode in ("basic", "off"):
            directory = root / f"fallback-{mode}"
            directory.mkdir()
            command = [solver, "-f", formula, "-i", "a,unused_i", "-o", "b,c,unused_o",
                       "--arms", "unreal:formula:spot-guarded-sparse", "--weakening", mode,
                       "-K", "2"]
            actual = run(command, env=dict(os.environ, ACACIA_PHASE_RECORDS=str(directory),
                         ACACIA_OUTER_DEADLINE_MONOTONIC=str(time.monotonic() + 4)))
            assert actual.returncode == 2, actual
            records, problems = reader.read_records(directory)
            assert not problems, problems
            row, = reader.census(records)
            assert row["mode"] == row["effective_mode"] == mode, (row, records)
            assert row["census"] == "complete", (row, records)
            assert row["full_solver_started"] and row["full_solver_remaining_ns"] > 0
            assert row["full_solver_starved"] is False
            assert row["observed_started"] == 0, records
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
                for mode in ("basic", "off"):
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
            for mode in ("basic", "off"):
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
                for mode in ("basic", "off"):
                    actual = run([solver, "-T", path, "--weakening", mode])
                    assert actual.returncode == exact.returncode, (semantics, guarantee, actual)
        # Keep strict weak-until normalization covered against the exact game.
        path.write_text('INFO { TITLE: "generated strict" SEMANTICS: Mealy,Strict TARGET: Mealy } '
                        'MAIN { INPUTS { a; } OUTPUTS { b; c; } REQUIRE { a; } ASSERT { b; } '
                        'ASSUME { G F a; } GUARANTEE { G F b; G F c; } }')
        normalized, inputs, outputs, *_ = run([inspect, path]).stdout.split("\0")
        exact = run([oracle, "-f", normalized, f"--ins={inputs}", f"--outs={outputs}",
                     "--realizability", "--bypass=no", "--decompose=no"])
        for mode in ("basic", "off"):
            assert run([solver, "-T", path, "--weakening", mode]).returncode == exact.returncode
        # A renamed generated pair must give the same verdict in every mode.
        for formula, expected in (cases[0], cases[8], cases[10]):
            twin = formula.replace("a", "renamed_i").replace("b", "renamed_o").replace(
                "c", "renamed_other")
            for mode in ("basic", "off"):
                actual = run([solver, "-f", twin, "-i", "renamed_i,unused_i",
                              "-o", "renamed_o,renamed_other,unused_o", "--weakening", mode])
                assert actual.returncode == (0 if expected else 1), actual
        # Synthesis remains a direct original-game operation in both modes.
        controller = root / "controller.aag"
        for mode in ("basic", "off"):
            actual = run([solver, "-f", "G(a <-> b)", "-i", "a", "-o", "b", "--weakening",
                          mode, "-s", controller])
            assert actual.returncode == 0 and controller.read_text().startswith("aag "), actual
    print("generated LTL/TLSF exact games, defaults, basic/off, renaming, synthesis and fallback passed")


if __name__ == "__main__":
    main()
