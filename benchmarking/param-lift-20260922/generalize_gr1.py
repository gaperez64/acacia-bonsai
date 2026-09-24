#!/usr/bin/env python3
"""Dated compatibility entry for :mod:`acacia_lift.generalizer`."""

from __future__ import annotations

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "scripts"))
from acacia_lift import generalizer as _implementation  # noqa: E402

if __name__ == "__main__":
    raise SystemExit(_implementation.main())

# Alias the module so historical monkeypatches still address live globals.
sys.modules[__name__] = _implementation
