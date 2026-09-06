"""Pure offline helpers for the P6 one-arm-per-process measurement driver."""
import importlib.util
import pathlib

import pytest

SCRIPT = pathlib.Path(__file__).resolve().parents[2] / "benchmarking/spot-provider-replay.py"


def load_module():
    spec = importlib.util.spec_from_file_location("spot_provider_replay", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def records(module):
    common = {k: "same" for k in module.CONSTRUCTION}
    common.update(status="WIN_K", certificate="verified", total_status="complete",
                  total_wrapper_rows="10", total_underlying_rows="8")
    return (dict(common, arm="c4", worker_pid="101"),
            dict(common, arm="c5", worker_pid="102", eager_rows_generated="0",
                 search_rows_generated_cumulative="3", wrapper_rows_generated="4",
                 underlying_rows_generated="3"))


def test_generation_charges_eager_search_and_additional_verification():
    m = load_module()
    assert m.row_accounting(10, 10, 10) == dict(eager=10, search=0, verification_additional=0, union=10)
    assert m.row_accounting(0, 3, 5) == dict(eager=0, search=3, verification_additional=2, union=5)
    with pytest.raises(ValueError):
        m.row_accounting(4, 3, 5)
    with pytest.raises(ValueError):
        m.row_accounting(0, 5, 4)


def test_request_union_deduplicates_across_k_and_certificate_overlap():
    m = load_module()
    assert m.request_accounting([0, 1, 1, 2], [1, 2, 3, 3]) == dict(
        search=3, search_only=1, verification=3, verification_additional=1, union=4)


@pytest.mark.parametrize("status", ["censored", "unknown", "not_enumerated"])
@pytest.mark.parametrize("total", ["NA", "7", "0"])
def test_censored_total_never_creates_denominator_or_ratio(status, total):
    assert load_module().utilization("9", total, status) == dict(requested=9, total=None, ratio=None)


def test_complete_total_zero_and_unknown_counts():
    m = load_module()
    assert m.utilization(3, 10, "complete")["ratio"] == 0.3
    assert m.utilization(0, 0, "complete")["ratio"] is None
    assert m.utilization("NA", 10, "complete")["requested"] is None
    assert m.row_accounting(0, "NA", 4)["search"] is None
    with pytest.raises(ValueError):
        m.utilization(11, 10, "complete")
    for value in (-1, "-1", "2.5", "junk"):
        with pytest.raises(ValueError):
            m.count(value)


@pytest.mark.parametrize("status", ["WIN_K", "LOSE_K"])
def test_same_construction_exact_fixed_k_outcomes_ignore_local_ids(status):
    m = load_module()
    c4, c5 = records(m)
    c4["status"] = c5["status"] = status
    c4.update(initial_id="0", strategy_generators="4", rank_bytes="100")
    c5.update(initial_id="19", strategy_generators="2", rank_bytes="40")
    assert m.equivalence(c4, c5) == "agree"
    c5["status"] = "LOSE_K" if status == "WIN_K" else "WIN_K"
    assert m.equivalence(c4, c5) == "disagree"


@pytest.mark.parametrize("key", load_module().CONSTRUCTION)
def test_different_options_or_construction_are_incomparable(key):
    m = load_module()
    c4, c5 = records(m)
    c5[key] = "different"
    assert m.equivalence(c4, c5) == "incomparable"
    assert m.summarize(c4, c5)["wrapper_utilization"]["ratio"] is None
    del c5[key]
    assert m.equivalence(c4, c5) == "incomparable"


@pytest.mark.parametrize("status", ["UNKNOWN", "RESOURCE_LIMIT", "DECLINED"])
def test_unknown_outcomes_are_never_fixed_k_losses(status):
    m = load_module()
    c4, c5 = records(m)
    c5["status"] = status
    assert m.equivalence(c4, c5) == "inconclusive"


def test_unverified_and_shared_process_results_cannot_establish_equivalence():
    m = load_module()
    c4, c5 = records(m)
    c5["certificate"] = "unverified"
    assert m.equivalence(c4, c5) == "inconclusive"
    c5["worker_pid"] = c4["worker_pid"]
    assert m.equivalence(c4, c5) == "incomparable"
    c5["arm"] = "c4"
    with pytest.raises(ValueError):
        m.equivalence(c4, c5)


def test_censored_eager_run_preserves_absolute_lazy_counts():
    m = load_module()
    c4, c5 = records(m)
    c4.update(status="UNKNOWN", total_status="censored", total_wrapper_rows="2")
    summary = m.summarize(c4, c5)
    assert summary["equivalence"] == "inconclusive"
    assert summary["wrapper_utilization"] == dict(requested=4, total=None, ratio=None)
    assert summary["c5_generation"] == dict(eager=0, search=3, verification_additional=1, union=4)


def test_tsv_input_preserves_empty_partition_and_na(tmp_path):
    path = tmp_path / "arm.tsv"
    path.write_text("arm\tpartition\ttotal_wrapper_rows\nc5\t\tNA\n")
    assert load_module().read_rows(path) == [dict(arm="c5", partition="", total_wrapper_rows="NA")]
