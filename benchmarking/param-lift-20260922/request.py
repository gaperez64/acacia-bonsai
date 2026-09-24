"""Dated compatibility import for :mod:`legacy_lift.capabilities`."""

from __future__ import annotations

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "scripts"))
from legacy_lift import capabilities as _implementation  # noqa: E402

sys.modules[__name__] = _implementation
