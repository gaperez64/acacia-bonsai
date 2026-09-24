#!/usr/bin/env python3
"""Dated compatibility entry for :mod:`legacy_lift.runner`."""

from __future__ import annotations

import pathlib
import sys
import types

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "scripts"))
from legacy_lift import runner as _implementation  # noqa: E402


class _ForwardingModule(types.ModuleType):
    """Preserve attribute patches on file-loaded historical campaign modules."""

    def __getattribute__(self, name: str):
        if name.startswith("__"):
            return super().__getattribute__(name)
        return getattr(_implementation, name)

    def __setattr__(self, name: str, value: object) -> None:
        if name.startswith("__"):
            super().__setattr__(name, value)
        else:
            setattr(_implementation, name, value)


# unittest.mock checks the module dictionary before setting a patch. Keep
# those names present while all live reads and writes reach the package.
globals().update({name: value for name, value in vars(_implementation).items()
                  if not name.startswith("__")})

if __name__ == "__main__":
    raise SystemExit(_implementation.main())

# Alias the module so historical monkeypatches still address live globals.
sys.modules[__name__].__class__ = _ForwardingModule
sys.modules[__name__] = _implementation
