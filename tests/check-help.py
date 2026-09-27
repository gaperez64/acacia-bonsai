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
            assert result.stderr == "", (option, invalid_deadline, result)
            assert list(cwd.iterdir()) == [], (option, invalid_deadline)

    # -F takes a filename, including one that begins with the help spelling.
    result = subprocess.run([str(binary), "-F", "-h-missing-formula"], cwd=cwd,
                            capture_output=True, text=True, check=False, timeout=10)
    assert result.returncode != 0, result
    assert result.stdout == "", result
    assert "unable to open file -h-missing-formula" in result.stderr, result
    assert list(cwd.iterdir()) == []
