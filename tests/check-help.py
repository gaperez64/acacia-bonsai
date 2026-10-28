#!/usr/bin/env python3
"""The binary's short and long help options must be successful CLI requests."""

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
            assert "--obligation-helper" not in result.stdout
            assert "--obligation-memory-share VAL [equal|alone] (default equal)" in result.stdout
            assert "--parity-memory-share VAL [equal|headroom] (default equal)" in result.stdout
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

    # -F takes a filename, including one that begins with the help spelling.
    result = subprocess.run([str(binary), "-F", "-h-missing-formula"], cwd=cwd,
                            capture_output=True, text=True, check=False, timeout=10)
    assert result.returncode != 0, result
    assert result.stdout == "", result
    assert "unable to open file -h-missing-formula" in result.stderr, result
    assert list(cwd.iterdir()) == []
