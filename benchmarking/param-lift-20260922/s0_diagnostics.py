"""Dated compatibility import for :mod:`acacia_lift.diagnostics`."""

from __future__ import annotations

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "scripts"))
from acacia_lift import diagnostics as _implementation  # noqa: E402

sys.modules[__name__] = _implementation
