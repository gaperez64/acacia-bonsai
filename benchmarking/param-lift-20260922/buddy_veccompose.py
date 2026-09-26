"""Dated compatibility import for :mod:`acacia_lift.buddy_veccompose`."""

from __future__ import annotations

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "benchmarking/gr1-par2-20260923/oracle"))
from acacia_lift import buddy_veccompose as _implementation  # noqa: E402

sys.modules[__name__] = _implementation
