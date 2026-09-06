"""Pure-helper and mocked streaming tests; never launch a systemd campaign."""

import csv
import importlib.util
import pathlib
import sys

import pytest

SCRIPT = pathlib.Path(__file__).resolve().parents[2] / "benchmarking/spot-demand-campaign.py"


def load_module():
    spec = importlib.util.spec_from_file_location("spot_demand_campaign", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def worker(**overrides):
    return {
        "pid": "12", "path": "real:small:forward", "diag_kind": "final",
        "checkpoint": "final", "result": "solved", "total_ms": "20",
        "forward_backend": "1", "support_graph_ready": "1",
        "support_graph_states": "10", "support_graph_edges": "30",
        "support_union_rows": "4", "support_union_edges": "18",
        "support_search_rows": "3", "support_verification_rows": "2",
        "support_verification_only_rows": "1", "support_search_applications": "9",
        "support_verification_applications": "2", "support_k_union_rows": "2",
        **overrides,
    }


def test_instance_lists_and_suite_pairs(tmp_path):
    module = load_module()
    path = tmp_path / "instances.list"
    path.write_text("# header\na.ltl\n\nb.ltl # control\n")
    assert module.read_instance_list(path) == ["a.ltl", "b.ltl"]
    assert module.parse_pairs(["s=a=b"], "--suite") == {"s": pathlib.Path("a=b")}
    for invalid in ["s", "=path", "s="]:
        with pytest.raises(ValueError, match="SUITE=PATH|nonempty"):
            module.parse_pairs([invalid], "--suite")


def test_selected_cohort_retains_family_and_deduplicates_neighbours(tmp_path):
    module = load_module()
    path = tmp_path / "targets.tsv"
    path.write_text("instance\tfamily_key\tneighbour_instance\n"
                    "hard.ltl\ta\teasy.ltl\nharder.ltl\ta\teasy.ltl\n")
    assert module.target_cohort(path, True) == {
        "hard.ltl": "a", "easy.ltl": "a", "harder.ltl": "a",
    }
    assert module.target_cohort(path, False) == {"hard.ltl": "a", "harder.ltl": "a"}


def test_ratios_use_union_across_k_and_verification_is_separate():
    row = load_module().demand_row(worker(), "REALIZABLE")
    assert row["rho_q"] == "0.4"
    assert row["rho_e"] == "0.6"
    assert row["verification_only_rows"] == "1"
    assert row["k_union_rows"] == "2"
    assert row["status"] == "complete"
    assert row["backend"] == "forward"


@pytest.mark.parametrize("terminal", [False, True])
@pytest.mark.parametrize("result", ["TIMEOUT", "RESOURCE_LIMIT", "UNKNOWN", "ERROR"])
def test_translation_failure_never_has_zero_numerators_or_ratios(terminal, result):
    module = load_module()
    diag = worker(support_graph_ready="0", support_phase="translation",
                  aut_states="0", support_graph_states="0", support_graph_edges="0",
                  support_union_rows="0", support_union_edges="0", result="unknown",
                  diag_kind="final" if terminal else "progress",
                  checkpoint="final" if terminal else "support-before-translation")
    row = module.demand_row(diag, result)
    assert row["status"] == "translation-failure-" + result.lower()
    assert all(row[column] == "" for column in module.DEMAND_COLUMNS)


def test_preprocessing_timeout_and_search_timeout_are_separable():
    module = load_module()
    diag = worker(diag_kind="progress", result="unknown", aut_states="10",
                  support_phase="preprocessing", support_graph_ready="0")
    row = module.demand_row(diag, "TIMEOUT")
    assert row["status"] == "preprocessing-timeout"
    assert row["union_rows"] == row["rho_q"] == ""
    diag.update(support_graph_ready="1", support_phase="search")
    row = module.demand_row(diag, "TIMEOUT")
    assert row["status"] == "timeout"
    assert row["rho_q"] == "0.4"


def test_zero_edges_and_absent_support_samples_are_not_ratios_or_percentiles():
    row = load_module().demand_row(worker(
        support_graph_edges="0", support_union_edges="0", support_union_rows="0",
        support_search_applications="0", support_verification_applications="0",
        support_median="0", support_p95="0", support_max="0"), "REALIZABLE")
    assert row["rho_e"] == ""
    assert row["rho_q"] == "0"
    assert row["support_median"] == row["support_p95"] == row["support_max"] == ""


def test_final_worker_beats_discarded_speculative_progress_and_keeps_killed_child():
    module = load_module()
    final = worker()
    speculative = worker(diag_kind="progress", total_ms="200", support_union_rows="9")
    killed = worker(pid="13", diag_kind="progress", total_ms="99")
    older = worker(pid="13", diag_kind="progress", total_ms="12")
    assert module.latest_workers([speculative, killed, older, final]) == [final, killed]


def test_missing_diagnostics_are_retained_without_fabricated_measurements():
    module = load_module()
    row = module.demand_row({}, "TIMEOUT")
    assert row["worker"] == "none"
    assert row["status"] == "no-diagnostics-timeout"
    assert all(row[column] == "" for column in module.DEMAND_COLUMNS)


def test_hash_and_output_contract(tmp_path):
    module = load_module()
    source = tmp_path / "formula.ltl"
    source.write_bytes(b"abc")
    assert module.sha256_file(source) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
    assert "Example:" in module.__doc__
    assert module.COLUMNS[-5:] == ["acacia_sha", "binary_sha256", "preset", "timestamp_utc", "status"]
    assert len(set(module.COLUMNS)) == len(module.COLUMNS)


def test_mocked_campaign_streams_header_and_failure_rows(tmp_path, monkeypatch, capsys):
    module = load_module()
    from benchlib import RunResult

    binary = tmp_path / "build/src/acacia-bonsai"
    binary.parent.mkdir(parents=True)
    binary.write_text("fake binary")
    listing = tmp_path / "cohort.list"
    listing.write_text("a.ltl\nb.ltl\nc.ltl\n")
    formula = tmp_path / "formula.ltl"
    formula.write_text("G(o)")
    formula.with_suffix(".part").write_text(".inputs i\n.outputs o\n")
    out = tmp_path / "demand.tsv"
    monkeypatch.setattr(module, "load_source_map", lambda _: {"a.ltl": formula, "b.ltl": formula})
    monkeypatch.setattr(module, "provenance", lambda *_: {
        "acacia_sha": "revision", "binary_sha256": "binary-hash", "preset": "test",
    })
    calls = []
    def fake_run(command, timeout, memory, swap, **kwargs):
        # A killed driver must already have a readable TSV on disk.
        with out.open() as source:
            rows = list(csv.DictReader(source, delimiter="\t"))
        assert len(rows) == len(calls)
        assert kwargs["unit_prefix"] == "acacia-spot-demand"
        assert kwargs["env"]["ACACIA_DIAG_SUPPORT_DEMAND"] == "1"
        calls.append(command)
        kwargs["capture_consumer"](
            "ACACIA_DIAG pid=10 path=real diag_kind=progress "
            "checkpoint=support-before-translation support_phase=translation "
            "support_graph_ready=0 aut_states=0 total_ms=0")
        return RunResult("", "", -15, 25.0, True)
    monkeypatch.setattr(module, "run_systemd_scope", fake_run)
    monkeypatch.setattr(sys, "argv", [str(SCRIPT), "--build", str(binary.parents[1]),
                                     "--suite", f"s={listing}", "--source-map", "s=fake",
                                     "--out", str(out)])
    assert module.main.__wrapped__() == 0
    with out.open() as source:
        rows = list(csv.DictReader(source, delimiter="\t"))
    assert [row["status"] for row in rows] == [
        "translation-failure-timeout", "translation-failure-timeout", "missing-source",
    ]
    assert all(row["rho_q"] == row["union_rows"] == "" for row in rows)
    assert all(row["acacia_sha"] == "revision" and row["timestamp_utc"].endswith("Z") for row in rows)
    assert f"# wrote 3 rows to {out}" in capsys.readouterr().err
