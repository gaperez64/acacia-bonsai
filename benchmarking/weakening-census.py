#!/usr/bin/env python3
"""Compatibility entry point; implementation lives in benchmarking/legacy-phase-table.py."""
import pathlib
import sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from benchlib import load_tool

_owner = load_tool('benchmarking/legacy-phase-table.py')
census = _owner.weakening_census
main = _owner.weakening_main
read_records = _owner.weakening_read_records


def __getattr__(name):
    return getattr(_owner, name)

if __name__ == "__main__":
    raise SystemExit(main())
