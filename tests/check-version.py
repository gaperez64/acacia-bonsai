#!/usr/bin/env python3
"""Check that acacia-bonsai reports a concrete version in its CLI format."""

import pathlib
import re
import subprocess
import sys


def main() -> None:
    executable = pathlib.Path(sys.argv[1])
    completed = subprocess.run(
        [str(executable), "-V"],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    pattern = r"Version: (\d[\w.+-]*|[0-9a-f]{7,40})(-dirty)?\n"
    if completed.returncode != 2 or re.fullmatch(pattern, completed.stdout) is None:
        raise AssertionError(
            "unexpected -V result "
            f"(exit {completed.returncode}):\n{completed.stdout}{completed.stderr}"
        )


if __name__ == "__main__":
    main()
