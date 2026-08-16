from __future__ import annotations

import importlib.util
import pathlib
import sys


BENCHMARKING = pathlib.Path(__file__).resolve().parents[2] / "benchmarking"
SCRIPT = BENCHMARKING / "rank_bm_logs.py"


def load_rank_bm_logs():
    sys.path.insert(0, str(BENCHMARKING))
    try:
        spec = importlib.util.spec_from_file_location("rank_bm_logs", SCRIPT)
        module = importlib.util.module_from_spec(spec)
        assert spec.loader is not None
        spec.loader.exec_module(module)
        return module
    finally:
        sys.path.pop(0)


def test_outcome_splits_non_answers():
    module = load_rank_bm_logs()

    assert module.outcome({"result": "OK", "stdout": "REALIZABLE"}) == "ok"
    assert module.outcome({"result": "TIMEOUT", "stdout": ""}) == "timeout"
    assert module.outcome(
        {"result": "FAIL", "stdout": "FAILED: NO VERDICT: UNKNOWN (return 2)"}
    ) == "unknown"
    assert module.outcome(
        {"result": "FAIL", "stdout": "RESOURCE LIMIT: cgroup killed the solver"}
    ) == "unknown"
    assert module.outcome({"result": "FAIL", "stdout": "FAILED: ERROR: RETURNED 3"}) == "error"
