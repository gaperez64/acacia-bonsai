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


def load_worker_module():
    import sys

    spec = importlib.util.spec_from_file_location(
        "summarize_worker_phases", SCRIPT.with_name("summarize-worker-phases.py"))
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def test_worker_legacy_label_is_not_search_or_terminal_evidence():
    module = load_worker_module()
    worker = module.Worker(pathlib.Path("legacy.json"), {
        "stage": "preprocessing", "status": "LOSE_K", "k": "5", "kmax": "5",
        "verification_ms": "42",
    }, {"timed_out": "true"}, "test", {"k": 5, "kmax": 5})
    assert not worker.reached_search
    assert not worker.terminal_attempt


def test_worker_cancellation_and_exception_are_not_returns():
    module = load_worker_module()
    for end in ("unobserved", "exception"):
        worker = module.Worker(pathlib.Path("new.json"), {
            "record_version": "2", "stage": "attempt-end", "attempt_end": "true",
            "search_started": "true", "worker_end": end, "status": "WIN_K",
        }, {"timed_out": "false", "result": "REALIZABLE"}, "test", {})
        assert worker.reached_search
        assert not worker.terminal_attempt
    worker.record["worker_end"] = "returned"
    assert worker.terminal_attempt


def test_worker_truncated_final_record_recovers_only_complete_prefix(tmp_path):
    import json

    import pytest

    module = load_worker_module()
    path = tmp_path / "worker.json"
    path.write_text('{"worker_end":')
    history = path.with_suffix(".history.jsonl")
    record = {"record_version": "2", "stage": "verification", "search_started": "true"}
    history.write_bytes(json.dumps(record).encode() + b'\n{"worker_end":"ret\xff')
    recovered = module.read_record(path)
    assert recovered["stage"] == "verification"
    assert recovered["worker_end"] == "unobserved"
    assert recovered["record_incomplete"] == "true"
    history.write_text('{broken}\n' + json.dumps(record) + '\n')
    with pytest.raises(ValueError, match="non-trailing"):
        module.read_record(path)


def test_worker_campaign_without_hard_coded_cohort_or_arm_count(tmp_path):
    import argparse
    import csv
    import json

    module = load_worker_module()

    def tsv(name, rows):
        with (tmp_path / name).open("w") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0]), delimiter="\t")
            writer.writeheader()
            writer.writerows(rows)

    tsv("test.tsv", [{"solver_label": "test", "instance": "arbitrary.ltl", "cap_s": "17",
                      "seconds": "17", "timed_out": "true", "result": "TIMEOUT"}])
    tsv("test-summary.tsv", [{"solver_label": "test", "instance": "arbitrary.ltl",
                              "max_cap_s": "17", "failure_kind_at_max_cap": "TIMEOUT"}])
    root = tmp_path / "wrec/test/17/arbitrary.ltl"
    root.mkdir(parents=True)
    record = {"instance": "arbitrary.ltl", "polarity": "real", "transform": "real",
              "requested_backend": "backward", "requested_provider": "frozen-graph",
              "stage": "search", "search_started": "true", "record_version": "2",
              "worker_end": "unobserved", "worker_id": "1-1", "attempt_id": "2"}
    (root / "1.json").write_text(json.dumps(record))
    args = argparse.Namespace(campaign_dir=tmp_path, campaign_tsv=None, summary_tsv=None,
                              label="test", cohort=None)
    workers = module.load_workers(args)
    assert len(workers) == 1
    assert "Sparse guarded arms: none" in module.make_report(workers, "test")
    for index, polarity in enumerate(("real", "unreal"), 2):
        record.update(polarity=polarity, requested_backend="spot-guarded-sparse",
                      worker_id=f"{index}-1", search_ms="12", verification_ms="3",
                      row_generation_ms="4", cumulative_search_ms="22")
        (root / f"{index}.json").write_text(json.dumps(record))
    workers = module.load_workers(args)
    assert len(workers) == 3
    report = module.make_report(workers, "test")
    assert "20.0%" in report  # 3/(12+3); row generation is nested, not added.
    assert "inclusive subcomponent" in report
    assert "cumulative_search_ms" in module.make_tsv(workers)
    assert "worker_end" in module.make_tsv(workers)
    # A requested sparse arm can finish in a backward fallback segment. Its
    # current counters must not be attributed to sparse search.
    record.update(backend="backward", provider="frozen-graph", fallback="true")
    (root / "3.json").write_text(json.dumps(record))
    workers = module.load_workers(args)
    assert sum(worker.backend == "spot-guarded-sparse" for worker in workers) == 1
    assert "fallback" in module.make_tsv(workers)
    record.pop("worker_id")
    record.pop("record_version")
    for name in ("legacy-1.json", "legacy-2.json"):
        (root / name).write_text(json.dumps(record))
    assert len(module.load_workers(args)) == 5
