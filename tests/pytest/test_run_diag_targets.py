from __future__ import annotations

import csv
import importlib.util
import io
import pathlib
import sys


BENCHMARKING = pathlib.Path(__file__).resolve().parents[2] / "benchmarking"
SCRIPT = BENCHMARKING / "run_diag_targets.py"
BENCHLIB = BENCHMARKING / "benchlib.py"


def load_run_diag_targets():
    sys.path.insert(0, str(BENCHMARKING))
    try:
        spec = importlib.util.spec_from_file_location("run_diag_targets", SCRIPT)
        module = importlib.util.module_from_spec(spec)
        assert spec.loader is not None
        spec.loader.exec_module(module)
        return module
    finally:
        sys.path.pop(0)


def load_benchlib():
    spec = importlib.util.spec_from_file_location("benchlib_under_test", BENCHLIB)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    try:
        spec.loader.exec_module(module)
        return module
    finally:
        sys.modules.pop(spec.name, None)


def test_compact_diagnostics_keeps_terminal_and_latest_progress():
    module = load_run_diag_targets()
    rows = [
        {"pid": "1", "diag_kind": "progress", "checkpoint": "solve-loop", "loops": "64"},
        {"pid": "1", "diag_kind": "progress", "checkpoint": "solve-loop", "loops": "128"},
        {"pid": "1", "diag_kind": "progress", "checkpoint": "before-solve", "loops": "0"},
        {"pid": "1", "diag_kind": "final", "checkpoint": "final", "result": "unknown"},
    ]

    compact = module.compact_diagnostics(rows)

    assert compact[0]["diag_kind"] == "final"
    assert len(compact) == 3
    assert next(row for row in compact if row["checkpoint"] == "solve-loop")["loops"] == "128"


def test_streaming_accumulator_compacts_before_rows_are_materialized():
    module = load_run_diag_targets()
    accumulator = module.DiagnosticAccumulator()
    for line in (
        "ACACIA_DIAG pid=1 diag_kind=progress checkpoint=solve-loop loops=64",
        "ACACIA_DIAG pid=1 diag_kind=progress checkpoint=solve-loop loops=128",
        "ordinary solver noise",
        "ACACIA_DIAG pid=1 diag_kind=final checkpoint=final result=unknown",
    ):
        accumulator.add_line(line)

    rows = accumulator.rows()

    assert len(rows) == 2
    assert rows[0]["diag_kind"] == "final"
    assert rows[1]["loops"] == "128"


def test_write_checkpoint_replaces_complete_csv(tmp_path):
    module = load_run_diag_targets()
    output = tmp_path / "nested" / "diagnostics.csv"

    module.write_checkpoint(output, [{"target": "first.ltl", "extra": "1"}], ["target"])
    module.write_checkpoint(output, [{"target": "second.ltl"}], ["target"])

    with output.open(newline="") as handle:
        assert list(csv.DictReader(handle)) == [{"target": "second.ltl"}]
    assert not (output.parent / f".{output.name}.tmp").exists()


def test_preprocessing_census_is_explicitly_opt_in():
    module = load_run_diag_targets()
    inherited = {"ACACIA_DIAG_PREPROCESSING_CENSUS": "only", "KEEP": "yes"}

    ordinary = module.diagnostic_environment(
        inherited,
        progress_every="64",
        memory_max="8G",
        memory_swap_max="0",
        preprocessing_census_only=False,
    )
    census = module.diagnostic_environment(
        inherited,
        progress_every="64",
        memory_max="8G",
        memory_swap_max="0",
        preprocessing_census_only=True,
    )

    assert "ACACIA_DIAG_PREPROCESSING_CENSUS" not in ordinary
    assert census["ACACIA_DIAG_PREPROCESSING_CENSUS"] == "only"
    assert ordinary["KEEP"] == census["KEEP"] == "yes"


def test_filter_stream_discards_raw_noise_without_losing_diagnostics():
    module = load_benchlib()
    raw = "noise\nACACIA_DIAG pid=1 checkpoint=solve-loop\nmore noise\n"

    retained, raw_size = module.filter_stream(
        io.StringIO(raw), lambda line: "ACACIA_DIAG" in line
    )

    assert retained == ["ACACIA_DIAG pid=1 checkpoint=solve-loop\n"]
    assert raw_size == len(raw)
