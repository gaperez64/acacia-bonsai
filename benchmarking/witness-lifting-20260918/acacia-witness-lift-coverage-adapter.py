#!/usr/bin/env python3
"""Adapt acacia-witness-lift's evidence-bearing verdict to coverage syntax.

The research wrapper prints ``REALIZABLE PATH`` or ``UNREALIZABLE PATH`` so a
consumer can locate the checked witness.  The existing coverage runner's
Acacia parser expects a bare decisive word.  This adapter calls the wrapper in
process (so the runner's time/memory scope and cancellation handling still
cover every child), validates its one-line protocol, emits the bare word, and
preserves Acacia's 0/1/2 exit convention.  All arguments, including the
runner-appended ``-T FILE``, are forwarded unchanged.
"""

from __future__ import annotations

import contextlib
import importlib.util
import io
import re
import sys
from pathlib import Path


WRAPPER_PATH = Path(__file__).with_name("acacia-witness-lift.py")
_VERDICT = re.compile(
    r"^(?:(?P<decisive>REALIZABLE|UNREALIZABLE) (?P<witness>.+)|"
    r"UNKNOWN (?P<stage>[A-Za-z0-9_.-]+) (?P<reason>[A-Za-z0-9_.-]+))$"
)


def _load_wrapper():
    name = "acacia_witness_lift_coverage_wrapped"
    spec = importlib.util.spec_from_file_location(name, WRAPPER_PATH)
    if spec is None or spec.loader is None:
        raise ImportError(f"cannot load wrapper from {WRAPPER_PATH}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def adapt_line(line: str, exit_code: int) -> tuple[str, int]:
    match = _VERDICT.fullmatch(line)
    if match is None:
        return "UNKNOWN", 2
    decisive = match.group("decisive")
    if decisive == "REALIZABLE" and exit_code == 0:
        return decisive, exit_code
    if decisive == "UNREALIZABLE" and exit_code == 1:
        return decisive, exit_code
    if decisive is None and exit_code == 2:
        return "UNKNOWN", exit_code
    return "UNKNOWN", 2


def main(argv: list[str] | None = None) -> int:
    wrapper = _load_wrapper()
    captured = io.StringIO()
    with contextlib.redirect_stdout(captured):
        exit_code = wrapper.main(argv)
    lines = captured.getvalue().splitlines()
    if len(lines) != 1:
        print("coverage adapter: invalid wrapper stdout protocol", file=sys.stderr)
        print("UNKNOWN")
        return 2
    verdict, adapted_exit = adapt_line(lines[0], exit_code)
    print(verdict)
    return adapted_exit


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
