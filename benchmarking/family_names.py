"""Filename-based benchmark family names.

This is the *fallback* notion of a family: it reads a name and guesses which
parameterized series it belongs to.  It is right often enough to group a panel,
but it is a heuristic and must never be presented as the parameter structure of
an instance.  Where the TLSF release manifest records a `param:` origin, that
origin is exact and `family_metadata.py` uses it instead; this module is only
consulted for `direct:` origins, which carry no parameter data at all.

It lives here rather than in make-panel.py so that the panel builder and the
coverage-frontier tooling share one definition of a family instead of two
regex lists that can drift apart.
"""
from __future__ import annotations

import pathlib
import re


def family_of(instance: str) -> str:
    """Collapse common parameterized benchmark names into stable families."""
    stem = pathlib.Path(instance).stem
    stem = re.sub(r"_[0-9a-f]{8}$", "", stem, flags=re.IGNORECASE)

    patterns = (
        (r"^(prioritized_arbiter_unreal)", 1),
        (r"^(round_robin_arbiter_unreal)", 1),
        (r"^(full_arbiter_unreal)", 1),
        (r"^(ltl2dba_[A-Za-z]+)", 1),
        (r"^(ltl2dpa)", 1),
        (r"^(robot_grid)", 1),
        (r"^(collector)_v", 1),
    )
    for pattern, group in patterns:
        match = re.match(pattern, stem)
        if match:
            return match.group(group)

    # Strip a trailing sequence of numeric parameters without a nested
    # repetition regex.  The previous expression could backtrack
    # exponentially on long digit runs that did not match at the end.
    suffix_start = len(stem)
    if suffix_start and stem[-1].isdigit():
        while suffix_start:
            while suffix_start and stem[suffix_start - 1].isdigit():
                suffix_start -= 1
            if suffix_start == 0 or stem[suffix_start - 1] not in "_-":
                break
            suffix_start -= 1
            if suffix_start == 0 or not stem[suffix_start - 1].isdigit():
                break
    family = stem[:suffix_start].rstrip("_-")
    return family or "numeric"
