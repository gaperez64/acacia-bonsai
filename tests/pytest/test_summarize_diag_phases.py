from __future__ import annotations

import importlib.util
import pathlib


SCRIPT = pathlib.Path(__file__).resolve().parents[2] / "benchmarking/summarize-diag-phases.py"


def load_module():
    spec = importlib.util.spec_from_file_location("summarize_diag_phases", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def test_classify_target_uses_deepest_checkpoint():
    module = load_module()
    rows = [
        {"checkpoint": "after-rsimp", "total_ms": "100"},
        {"checkpoint": "after-preprocessing", "total_ms": "10"},
        {"checkpoint": "solve-loop", "loops": "1024", "total_ms": "20"},
    ]

    phase, representative = module.classify_target(rows)

    assert phase == "fixpoint-bound"
    assert representative["checkpoint"] == "solve-loop"


def test_classify_target_distinguishes_translation_and_action():
    module = load_module()

    assert module.classify_target([{"checkpoint": "after-decomposition"}])[0] == "translation-bound"
    assert module.classify_target([{"checkpoint": "after-spot-fast"}])[0] == "action-construction-bound"
    assert module.classify_target([{"checkpoint": "before-solve"}])[0] == "action-construction-bound"
    assert module.classify_target([{"checkpoint": "after-action-construction"}])[0] == "fixpoint-bound"


def test_classify_target_uses_modal_child_phase():
    module = load_module()
    rows = [
        {"pid": "1", "checkpoint": "after-decomposition", "total_ms": "120000"},
        {"pid": "2", "checkpoint": "after-translation", "total_ms": "120000"},
        {"pid": "2", "checkpoint": "before-solve", "total_ms": "120000"},
        {"pid": "3", "checkpoint": "after-decomposition", "total_ms": "120000"},
        {"pid": "4", "checkpoint": "after-action-construction", "total_ms": "120000"},
    ]

    phase, representative = module.classify_target(rows)

    assert phase == "translation-bound"
    assert representative["pid"] in {"1", "3"}


def test_fixpoint_bucket_uses_twenty_percent_mixed_band():
    module = load_module()

    assert module.fixpoint_bucket(120.0, 100.0) == "mixed"
    assert module.fixpoint_bucket(121.0, 100.0) == "letter-loop-bound"
    assert module.fixpoint_bucket(100.0, 121.0) == "downset-bound"
    assert module.fixpoint_bucket(0.0, 0.0) == "mixed"


def test_summarize_fixpoint_children_aggregates_only_fixpoint_workers():
    module = load_module()
    rows = [
        {
            "pid": "1",
            "checkpoint": "solve-loop",
            "apply_ms": "80.5",
            "downset_ms": "20.0",
            "cpre_ms": "110.0",
            "picker_ms": "3.0",
            "actions_seen": "10",
            "meets_computed": "40",
            "max_f_size": "7",
        },
        {
            "pid": "2",
            "checkpoint": "after-action-construction",
            "apply_ms": "39.5",
            "downset_ms": "30.0",
            "cpre_ms": "80.0",
            "picker_ms": "2.0",
            "actions_seen": "5",
            "meets_computed": "20",
            "max_f_size": "9",
        },
        {
            "pid": "3",
            "checkpoint": "after-translation",
            "apply_ms": "999",
            "downset_ms": "999",
        },
    ]

    bucket, detail = module.summarize_fixpoint_children(rows)

    assert bucket == "letter-loop-bound"
    assert detail == {
        "cpre_ms": 190.0,
        "picker_ms": 5.0,
        "apply_ms": 120.0,
        "downset_ms": 50.0,
        "actions_seen": 15,
        "meets_computed": 60,
        "max_f_size": 9,
    }


def test_unfinished_intersection_is_downset_bound():
    module = load_module()
    rows = [
        {
            "pid": "1",
            "checkpoint": "cpre-before-intersection",
            "apply_ms": "100",
            "downset_ms": "1",
        }
    ]

    bucket, _ = module.summarize_fixpoint_children(rows)

    assert bucket == "downset-bound"


def test_expensive_worker_outweighs_two_shallow_workers():
    module = load_module()
    rows = [
        {
            "pid": "1",
            "checkpoint": "classic-after-picker",
            "apply_ms": "900",
            "downset_ms": "54000",
        },
        {
            "pid": "2",
            "checkpoint": "classic-after-cpre",
            "apply_ms": "2",
            "downset_ms": "2",
        },
        {
            "pid": "3",
            "checkpoint": "classic-after-cpre",
            "apply_ms": "17",
            "downset_ms": "17",
        },
    ]

    bucket, _ = module.summarize_fixpoint_children(rows)

    assert bucket == "downset-bound"
