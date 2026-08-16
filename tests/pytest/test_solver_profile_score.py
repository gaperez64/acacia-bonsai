import importlib.util
import pathlib
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "benchmarking" / "solver-profile-score.py"
SPEC = importlib.util.spec_from_file_location("solver_profile_score", SCRIPT)
solver_profile_score = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = solver_profile_score
SPEC.loader.exec_module(solver_profile_score)


def medians(pairs):
    result = {}
    for index, (baseline, candidate) in enumerate(pairs):
        result[("baseline", "suite", f"target-{index}")] = baseline
        result[("candidate", "suite", f"target-{index}")] = candidate
    return result


def test_geometric_mean_passes_without_regressions():
    passed, messages = solver_profile_score.score(
        medians([(100, 90), (200, 180)]), 5.0, 6.0
    )
    assert passed
    assert any("decision=adoption-candidate" in message for message in messages)


def test_single_large_win_passes_to_g3_when_geometric_mean_is_small():
    passed, messages = solver_profile_score.score(
        medians([(100, 75), (100, 104), (100, 104), (100, 104)]), 5.0, 6.0
    )
    assert passed
    assert any("decision=proxy-pass-to-G3" in message for message in messages)


def test_any_target_over_regression_ceiling_fails():
    passed, messages = solver_profile_score.score(
        medians([(100, 80), (100, 107)]), 5.0, 6.0
    )
    assert not passed
    assert any("regressed" in message for message in messages)


def test_small_uniform_improvement_fails():
    passed, messages = solver_profile_score.score(
        medians([(100, 98), (100, 98)]), 5.0, 6.0
    )
    assert not passed
    assert any("below 5.00%" in message for message in messages)
