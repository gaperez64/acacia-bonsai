from __future__ import annotations

import importlib.util
import pathlib
import sys


BENCHMARKING = pathlib.Path(__file__).resolve().parents[2] / "benchmarking"
SCRIPT = BENCHMARKING / "make-panel.py"


def load_make_panel():
    sys.path.insert(0, str(BENCHMARKING))
    try:
        spec = importlib.util.spec_from_file_location("make_panel", SCRIPT)
        module = importlib.util.module_from_spec(spec)
        assert spec.loader is not None
        sys.modules[spec.name] = module
        spec.loader.exec_module(module)
        return module
    finally:
        sys.path.pop(0)


def test_standalone_verdict_rejects_harness_prose_and_unknown():
    module = load_make_panel()

    assert module.standalone_verdict("formula is realizable.\nPASS.\n") is None
    assert module.standalone_verdict("UNKNOWN\nPASS.\n") is None
    assert module.standalone_verdict("[x.ltl] REALIZABLE\n[x.ltl] PASS.\n") == "REALIZABLE"
    assert module.standalone_verdict("UNREALIZABLE\n") == "UNREALIZABLE"


def test_family_round_robin_is_deterministic_and_balanced():
    module = load_make_panel()
    pool = []
    for family, count in (("large", 20), ("medium", 5), ("small", 3)):
        for index in range(count):
            pool.append(
                module.Candidate(
                    f"{family}{index}.ltl",
                    "easy",
                    0.1,
                    0.1,
                    "REALIZABLE",
                    family,
                    "reference",
                )
            )

    first = module.family_round_robin(pool, 9, 7)
    second = module.family_round_robin(pool, 9, 7)
    assert first == second
    counts = {family: sum(row.family == family for row in first) for family in ("large", "medium", "small")}
    assert counts == {"large": 3, "medium": 3, "small": 3}
