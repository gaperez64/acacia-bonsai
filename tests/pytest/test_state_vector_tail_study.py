import importlib.util
import pathlib
import sys
from types import SimpleNamespace


SCRIPT = pathlib.Path(__file__).parents[2] / "benchmarking/state_vector_tail_study.py"
sys.path.insert(0, str(SCRIPT.parent))
SPEC = importlib.util.spec_from_file_location("state_vector_tail_study", SCRIPT)
STUDY = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = STUDY
SPEC.loader.exec_module(STUDY)


def sample(variant, seconds, cycles, verdict="REALIZABLE", repetition="1"):
    return {
        "label": "case",
        "repetition": repetition,
        "variant": variant,
        "verdict": verdict,
        "seconds": str(seconds),
        "cycles": str(cycles),
        "instructions": str(cycles * 2),
        "llc_load_misses": str(cycles * 3),
        "branch_misses": str(cycles * 4),
    }


def test_paired_rows_times_only_solved_but_counters_matching_verdicts():
    pairs = STUDY.paired_rows([sample("zero", 2, 20), sample("bare", 3, 30)])
    assert pairs[0]["verdict_match"] == "true"
    assert float(pairs[0]["bare_over_zero_seconds"]) == 1.5
    assert float(pairs[0]["bare_over_zero_cycles"]) == 1.5

    timeout = STUDY.paired_rows(
        [sample("zero", 20, 200, "TIMEOUT"), sample("bare", 20, 200, "TIMEOUT")]
    )
    assert timeout[0]["bare_over_zero_seconds"] == ""
    assert float(timeout[0]["bare_over_zero_cycles"]) == 1.0
    assert float(timeout[0]["bare_over_zero_instructions"]) == 1.0


def test_summary_uses_within_pair_ratios():
    samples = [
        sample("zero", 2, 20, repetition="1"),
        sample("bare", 3, 30, repetition="1"),
        sample("zero", 4, 40, repetition="2"),
        sample("bare", 2, 20, repetition="2"),
    ]
    summary = STUDY.summary_rows(samples, STUDY.paired_rows(samples))[0]
    assert float(summary["paired_median_time_ratio"]) == 1.0
    assert float(summary["paired_median_cycle_ratio"]) == 1.0


def test_inner_timeout_is_not_reported_as_an_error():
    run = SimpleNamespace(
        returncode=124,
        resource_limited=False,
        timed_out=False,
        stdout="",
        stderr="",
    )
    assert STUDY.classify_sample(run) == "TIMEOUT"
