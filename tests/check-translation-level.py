#!/usr/bin/env python3
"""Exhaustive small safety games, independent of Spot and rank-game construction."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from moore_aiger_oracle import Aiger, check, delayed_table_monitor


ARMS = ("real:small:backward", "real:small:forward",
        "unreal:formula:spot-guarded-sparse", "unreal:automaton:forward")
SETTINGS = "simul=0,ba-simul=0,det-simul=0,tls-impl=1,wdba-minimize=2"


def exact_game(mask, delay, moore):
    # The original monitor remembers the previous input (delay=1), or output
    # (delay=-1). State 2 is the unconstrained first step. Solve its safety game
    # by a greatest fixed point with the ORIGINAL timing quantifiers.
    states = {0, 1, 2} if delay else {2}
    winning = set(states)

    def safe(state, i, o):
        left = state if delay == 1 else i
        right = state if delay == -1 else o
        legal = (delay and state == 2) or bool(mask & (1 << (2 * left + right)))
        successor = i if delay == 1 else o if delay == -1 else 2
        return legal and successor in winning

    while True:
        keep = set()
        for state in states:
            choices = [[safe(state, i, o) for o in (0, 1)] for i in (0, 1)]
            wins = (any(all(choices[i][o] for i in (0, 1)) for o in (0, 1)) if moore
                    else all(any(row) for row in choices))
            if wins:
                keep.add(state)
        if keep == winning:
            return 2 in winning
        winning = keep


def objective(mask, delay):
    left = "X(i)" if delay == -1 else "i"
    right = "X(o)" if delay == 1 else "o"
    clauses = [f"({'!' if not i else ''}{left} & {'!' if not o else ''}{right})"
               for i in (0, 1) for o in (0, 1) if mask & (1 << (2 * i + o))]
    return "G(" + (" | ".join(clauses) if clauses else "false") + ")"


def invoke(binary, flags, env):
    return subprocess.run([str(binary), *flags], capture_output=True, env=env, timeout=15)


def main():
    binary, scratch = map(lambda arg: Path(arg).resolve(), sys.argv[1:3])
    baseline = Path(sys.argv[3]).resolve() if len(sys.argv) > 3 else None
    env = os.environ.copy()
    for name in ("ACACIA_PHASE_RECORDS", "ACACIA_OUTER_DEADLINE_MONOTONIC",
                 "ACACIA_SPOT_CAPTURE_DIR", "ACACIA_SPOT_CAPTURE_HISTORY"):
        env.pop(name, None)
    with tempfile.TemporaryDirectory(prefix="translation-level-", dir=scratch) as directory:
        root = Path(directory)
        invalid = invoke(binary, ["--translation-level", "fast"], env)
        assert invalid.returncode == 3 and b"expects high, medium or low" in invalid.stderr
        observed = set()
        cases = 0
        for mask in range(16):
            for delay in (-1, 0, 1):
                formula = objective(mask, delay).replace(" & ", " && ").replace(" | ", " || ")
                for semantics in ("Mealy", "Moore"):
                    expected = exact_game(mask, delay, semantics == "Moore")
                    source = root / "generated.tlsf"
                    source.write_text(f'INFO {{ TITLE: "generated safety game" '
                                      f'SEMANTICS: {semantics} TARGET: {semantics} }} '
                                      'MAIN { INPUTS { i; } OUTPUTS { o; } '
                                      f'GUARANTEES {{ {formula}; }} }}\n')
                    common = ["-T", str(source), "-K", "8", "--spot-fast", "off",
                              "--weakening", "off", "--equivariance", "off"]
                    for level in ("high", "low"):
                        for arm in ARMS:
                            capture = root / f"capture-{cases}"
                            phases = root / f"phases-{cases}"
                            phases.mkdir()
                            recording = dict(env, ACACIA_SPOT_CAPTURE_DIR=str(capture),
                                             ACACIA_PHASE_RECORDS=str(phases))
                            command = [*common, "--arms", arm, "--translation-level", level]
                            result = invoke(binary, command, recording)
                            matching = arm.startswith("real:") == expected
                            code = (0 if expected else 1) if matching else 2
                            assert result.returncode == code, (mask, delay, semantics, command,
                                                               code, result.stdout, result.stderr)
                            if matching:
                                verdict = b"REALIZABLE" if expected else b"UNREALIZABLE"
                                assert verdict in result.stdout
                            records = [json.loads(p.read_text()) for p in capture.glob("*.json")]
                            metrics = [json.loads(line) for p in phases.glob("*.jsonl")
                                       for line in p.read_text().splitlines()
                                       if '"stage_metric"' in line]
                            for record in records:
                                assert record["translation_level"] == level
                                assert record["translation_settings"] == SETTINGS
                                assert record["translation_pref"] == "small"
                                assert record["translation_type"] == "ba"
                                assert record["translation_state_acceptance"] == "on"
                                assert record["translation_gf_guarantee"] == (
                                    "off" if level == "low" else "on")
                            settings = {row["key"]: row["value"] for row in metrics}
                            if "translation_level" in settings:
                                assert settings["translation_level"] == level
                                assert settings["translation_settings"] == SETTINGS
                                assert settings["translation_type"] == "ba"
                                assert settings["translation_preference"] == "9"
                                assert settings["translation_gf_guarantee"] == (
                                    "off" if level == "low" else "on")
                                observed.add((arm, level))
                            cases += 1
        assert observed == {(arm, level) for arm in ARMS for level in ("high", "low")}, observed

        for semantics in ("Mealy", "Moore"):
            source = root / "synthesis.tlsf"
            source.write_text(f'INFO {{ TITLE: "controller" SEMANTICS: {semantics} '
                              f'TARGET: {semantics} }} MAIN {{ INPUTS {{ i; }} '
                              'OUTPUTS { o; } GUARANTEES { G(i <-> X(o)); } }')
            for level in ("high", "low"):
                aig = root / f"{semantics}-{level}.aag"
                result = invoke(binary, ["-T", str(source), "-s", str(aig),
                                         "--translation-level", level, "--spot-fast", "off",
                                         "--arms", ARMS[0]], env)
                assert result.returncode == 0, (semantics, level, result)
                monitor = delayed_table_monitor(["i"], "o", (False, True), 1, None)
                checked = check(Aiger.read(aig), *monitor, moore=semantics == "Moore")
                assert checked.valid, (semantics, level, checked)

        # Native workers do not construct the legacy translator.
        source = root / "native.tlsf"
        source.write_text('INFO { TITLE: "native" SEMANTICS: Mealy TARGET: Mealy } '
                          'MAIN { INPUTS { i; } OUTPUTS { o; } GUARANTEES { G(i <-> o); } }')
        native = []
        for level in ("high", "medium", "low"):
            phases = root / f"native-{level}"
            phases.mkdir()
            result = invoke(binary, ["-T", str(source), "--arms", "both:gr1:oxidd",
                                     "--translation-level", level],
                            dict(env, ACACIA_PHASE_RECORDS=str(phases)))
            assert result.returncode == 0, result
            assert all('"translation_level"' not in p.read_text() for p in phases.glob("*.jsonl"))
            native.append((result.returncode, result.stdout, result.stderr))
        assert native[0] == native[1] == native[2]

        # Omitted and explicit High produce the same ordinary output. An optional
        # matched base executable additionally checks the pre-change output bytes.
        for formula in ("G(i <-> X(o))", "G(o <-> X(i))", "GF(i) -> GF(o)"):
            common = ["-f", formula, "-i", "i", "-o", "o", "-K", "8"]
            for arms in (*ARMS, ",".join(ARMS), None):
                flags = [*common, "--arms", arms] if arms else common
                default = invoke(binary, flags, env)
                high = invoke(binary, [*flags, "--translation-level", "high"], env)
                assert (default.returncode, default.stdout, default.stderr) == (
                    high.returncode, high.stdout, high.stderr)
                if baseline:
                    base = invoke(baseline, flags, env)
                    assert (default.returncode, default.stdout, default.stderr) == (
                        base.returncode, base.stdout, base.stderr), (formula, arms, default, base)
    print(f"{cases} generated worker checks: exact Mealy/Moore games and both timing transforms; "
          "phase/capture settings, native isolation and default output checked")


if __name__ == "__main__":
    main()
