#!/usr/bin/env python3
"""Dated forwarding build command for the maintained native adapter."""

from __future__ import annotations

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[3] / "scripts"))
from acacia_lift.native.build import main  # noqa: E402

if __name__ == "__main__":
    raise SystemExit(main())
