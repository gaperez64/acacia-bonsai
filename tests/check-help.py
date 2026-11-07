#!/usr/bin/env python3
"""The binary's short and long help options must be successful CLI requests."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


binary = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(dir=Path.cwd(), prefix="check-help-") as directory:
    cwd = Path(directory)
    for option in ("-h", "--help"):
        for invalid_deadline in (False, True):
            env = os.environ.copy()
            if invalid_deadline:
                env["ACACIA_OUTER_DEADLINE_MONOTONIC"] = "invalid"
            else:
                env.pop("ACACIA_OUTER_DEADLINE_MONOTONIC", None)
            result = subprocess.run([str(binary), option], cwd=cwd, env=env,
                                    capture_output=True, text=True, check=False,
                                    timeout=10)
            assert result.returncode == 0, (option, invalid_deadline, result)
            assert result.stdout.startswith("Usage:"), (option, invalid_deadline, result)
            assert "--arms LIST" in result.stdout, (option, invalid_deadline, result)
            assert "--equivariance-budget F|DURATION|unbounded (default 3s)" in result.stdout
            assert "[scan|grouped] (default: grouped)" in result.stdout
            assert "--spot-fast VAL   use Spot NBA fast path from [off|det]\n" in result.stdout
            assert ("--translation-effort VAL  native translation effort "
                    "[high|medium|low] (default high)\n" in result.stdout)
            assert "--obligation-helper" not in result.stdout
            assert "--obligation-memory-share VAL [equal|alone] (default equal)" in result.stdout
            assert "--parity-memory-share VAL [equal|headroom] (default headroom)" in result.stdout
            assert "builds the tlsf-tools DPA" in result.stdout
            assert "validated offline on a recorded sample against a construction" in result.stdout
            assert ("both:obligation:spot: syntactic obligations, trusted Spot synthesis"
                    in result.stdout)
            assert "membership follows the build default; starts only when eligible" in result.stdout
            assert result.stderr == "", (option, invalid_deadline, result)
            assert list(cwd.iterdir()) == [], (option, invalid_deadline)

    for value in ("alone", "pressure", "unknown", "Headroom", ""):
        result = subprocess.run([str(binary), "--parity-memory-share=" + value],
                                cwd=cwd, capture_output=True, text=True,
                                check=False, timeout=10)
        assert result.returncode == 3, result
        assert "--parity-memory-share expects equal or headroom" in result.stderr, result
        assert list(cwd.iterdir()) == []

    for value in ("equal", "headroom"):
        result = subprocess.run([str(binary), "--parity-memory-share", value,
                                 "--arms", "both:parity:spot", "-f", "G o", "-i", "i", "-o", "o"],
                                cwd=cwd, capture_output=True, text=True,
                                check=False, timeout=10)
        assert result.returncode == 0, result
        assert list(cwd.iterdir()) == []

    for value in ("", "High", "fast", "high,low"):
        result = subprocess.run([str(binary), "--translation-effort=" + value],
                                cwd=cwd, capture_output=True, text=True,
                                check=False, timeout=10)
        assert result.returncode == 3, result
        assert "--translation-effort expects high, medium or low" in result.stderr, result

    # Each arm runs in a forked worker. Check its captured effective option and
    # the same tiny verdict with the default and each explicit effort level.
    for effort in (None, "high", "medium", "low"):
        for arm in ("real:small:backward", "unreal:formula:backward",
                    "unreal:automaton:backward"):
            with tempfile.TemporaryDirectory(dir=cwd) as capture_directory:
                env = dict(os.environ, ACACIA_SPOT_CAPTURE_DIR=capture_directory,
                           ACACIA_PHASE_RECORDS=capture_directory)
                formula = "G(i <-> X(o))" if arm.startswith("real:") else "G(o <-> X(i))"
                command = [str(binary), "--arms", arm, "--spot-fast", "off", "-K", "4",
                           "-f", formula, "-i", "i", "-o", "o"]
                if effort is not None:
                    command += ["--translation-effort", effort]
                result = subprocess.run(command, cwd=cwd, env=env, capture_output=True,
                                        text=True, check=False, timeout=10)
                expected = 0 if arm.startswith("real:") else 1
                assert result.returncode == expected, (command, result)
                assert ("UNREALIZABLE" if expected else "REALIZABLE") in result.stdout.splitlines()
                records = [json.loads(p.read_text())
                           for p in Path(capture_directory).glob("*.json")]
                assert len(records) == 1, (command, records)
                assert records[0]["translation_effort"] == (effort or "high"), records
                specs = [row for p in Path(capture_directory).glob("*.jsonl")
                         for line in p.read_text().splitlines()
                         if (row := json.loads(line)).get("event") == "worker_spec"]
                assert len(specs) == 1, (command, specs)
                assert specs[0]["translation_effort"] == (effort or "high"), specs
        assert list(cwd.iterdir()) == []

    # -F takes a filename, including one that begins with the help spelling.
    result = subprocess.run([str(binary), "-F", "-h-missing-formula"], cwd=cwd,
                            capture_output=True, text=True, check=False, timeout=10)
    assert result.returncode != 0, result
    assert result.stdout == "", result
    assert "unable to open file -h-missing-formula" in result.stderr, result
    assert list(cwd.iterdir()) == []
