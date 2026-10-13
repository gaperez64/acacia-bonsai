import csv
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

import pytest


SCRIPT = Path(__file__).resolve().parents[1] / "event-univ-census-summary.py"


@pytest.fixture
def summary_tool():
    spec = importlib.util.spec_from_file_location("event_univ_census_summary", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@pytest.fixture
def census_rows(summary_tool):
    return [{"source_row": ordinal, "status": "ok", "frame": frame, "component": component,
             "guard_fires": int(ordinal == 2), "distinct_promises": 65 if ordinal == 2 else 0,
             "width": 64, "expected_components": 2 if frame in summary_tool.SHIPPING else 1}
            for ordinal in (1, 2) for frame in sorted(summary_tool.FRAMES)
            for component in range(1, 3 if frame in summary_tool.SHIPPING else 2)]


def write_census(path, rows):
    with path.open("w") as stream:
        writer = csv.DictWriter(stream, fieldnames=("source_row", "status", "frame", "component",
                                "expected_components", "distinct_promises", "width",
                                "guard_fires"), delimiter="\t")
        writer.writeheader()
        writer.writerows(rows)
    return path


def test_all_components_complete(tmp_path, summary_tool, census_rows):
    census = write_census(tmp_path / "census.tsv", census_rows)
    summary, union = summary_tool.summarize(["one", "two"], census)
    assert summary["complete"] is True
    assert summary["unknown_shipping_inputs"] == summary["unknown_inputs"] == 0
    assert summary["shipping_union"]["unchanged"] == 1
    assert summary["shipping_union"]["guard_fires"] == 1
    assert summary["shipping_union"]["guard_input_ordinals"] == [2]
    assert union == {1: False, 2: True}


def test_guard_in_only_one_component_counts_input_once(tmp_path, summary_tool, census_rows):
    for row in census_rows:
        guard = row["source_row"] == 2 and row["frame"] == "real-components" and row["component"] == 2
        row["guard_fires"] = int(guard)
        row["distinct_promises"] = 65 if guard else 64
    census = write_census(tmp_path / "census.tsv", census_rows)
    summary, union = summary_tool.summarize(["one", "two"], census)
    assert summary["shipping_union"]["guard_fires"] == 1
    assert summary["frames"]["real-components"]["guard_fires"] == 1
    assert summary["frames"]["formula-unreal"]["guard_fires"] == 0
    assert union[2] is True


@pytest.mark.parametrize("frame", ["automaton-unreal", "raw-positive"])
def test_interrupted_component_is_unknown(tmp_path, summary_tool, census_rows, frame):
    rows = [row for row in census_rows if not (
        row["source_row"] == 2 and row["frame"] == frame and
        row["component"] == row["expected_components"])]
    census = write_census(tmp_path / "census.tsv", rows)
    with pytest.raises(ValueError, match="every input, frame and component"):
        summary_tool.summarize(["one", "two"], census)
    summary, union = summary_tool.summarize(["one", "two"], census, allow_incomplete=True)
    assert summary["complete"] is False
    assert summary["unknown_shipping_inputs"] == summary["unknown_inputs"] == 1
    assert summary["unknown_input_ordinals"] == [2]
    assert summary["shipping_union"]["guard_fires"] == 0
    assert union == {1: False}
    assert all(frame["inputs"] == 1 for frame in summary["frames"].values())


@pytest.mark.parametrize("damage", ["truncate", "missing_count", "duplicate", "count_mismatch",
                                   "bad_component", "bad_guard", "bad_width", "bad_frame"])
def test_component_integrity(tmp_path, summary_tool, census_rows, damage):
    if damage == "missing_count":
        for row in census_rows:
            if row["source_row"] == 2:
                row["expected_components"] = ""
    elif damage == "duplicate":
        census_rows.append(dict(census_rows[-1]))
    elif damage == "count_mismatch":
        census_rows[-1]["expected_components"] += 1
    elif damage == "bad_component":
        census_rows[-1]["component"] = 0
    elif damage == "bad_guard":
        census_rows[-1]["guard_fires"] = 0
    elif damage == "bad_width":
        census_rows[-1]["width"] = 0
    elif damage == "bad_frame":
        census_rows[-1]["frame"] = "missing-frame"
    census = write_census(tmp_path / "census.tsv", census_rows)
    if damage == "truncate":
        text = census.read_text()
        census.write_text(text[:text.rfind("\t")] + "\n")
    with pytest.raises(ValueError):
        summary_tool.summarize(["one", "two"], census)
    if damage in {"truncate", "missing_count"}:
        summary, _ = summary_tool.summarize(["one", "two"], census, allow_incomplete=True)
        assert summary["unknown_input_ordinals"] == [2]
        assert summary["truncated_rows"] > 0


@pytest.mark.parametrize("status", ["ok", "exhausted", "error_1", "error_137", None])
def test_per_input_status_controls_completion(tmp_path, summary_tool, census_rows, status):
    census = write_census(tmp_path / "census.tsv", census_rows)
    statuses = tmp_path / "input-status.tsv"
    statuses.write_text("1\tok\t0.25\n" + (f"2\t{status}\t120.00\n" if status else ""))
    summary, union = summary_tool.summarize(["one", "two"], census, statuses, True)
    assert summary["complete"] is (status == "ok")
    assert summary["unknown_input_ordinals"] == ([] if status == "ok" else [2])
    assert summary["shipping_union"]["inputs"] == (2 if status == "ok" else 1)
    assert summary["input_status_counts"]["missing"] == int(status is None)
    assert summary["input_status_seconds"] == (120.25 if status else 0.25)
    assert summary["exhausted_input_ordinals"] == ([2] if status == "exhausted" else [])
    assert (2 in union) is (status == "ok")
    if status != "ok":
        with pytest.raises(ValueError, match="successfully"):
            summary_tool.summarize(["one", "two"], census, statuses)


def test_ok_status_does_not_rescue_missing_component(tmp_path, summary_tool, census_rows):
    rows = [row for row in census_rows if not (
        row["source_row"] == 2 and row["frame"] == "automaton-unreal" and row["component"] == 2)]
    census = write_census(tmp_path / "census.tsv", rows)
    statuses = tmp_path / "input-status.tsv"
    statuses.write_text("1\tok\t0.1\n2\tok\t0.2\n")
    summary, _ = summary_tool.summarize(["one", "two"], census, statuses, True)
    assert summary["unknown_input_ordinals"] == [2]


@pytest.mark.parametrize("row", ["1\tok\tnan", "1\tok\tinf", "1\tok\t-1", "1\tunknown\t1",
                                 "1\terror_0\t1", "3\tok\t1", "1\tok", "1\tok\t1\n1\tok\t2"])
def test_invalid_input_status_rejected(tmp_path, summary_tool, row):
    statuses = tmp_path / "input-status.tsv"
    statuses.write_text(row + "\n")
    with pytest.raises(ValueError):
        summary_tool.read_input_status(statuses, 2)


def test_cli_binds_status_and_omits_exhausted_input(
        tmp_path, summary_tool, census_rows, monkeypatch, capsys):
    source_map = tmp_path / "sources.tsv"
    source_map.write_text("instance\ttlsf\none\tone.tlsf\ntwo\ttwo.tlsf\n")
    census = write_census(tmp_path / "census.tsv", census_rows)
    statuses = tmp_path / "input-status.tsv"
    statuses.write_text("1\tok\t0.1\n2\texhausted\t120\n")
    build = tmp_path / "build"
    (build / "tests").mkdir(parents=True)
    (build / "meson-info").mkdir()
    tool = build / "tests/event-univ-census"
    tool.write_bytes(b"test census executable")
    for name in ("intro-buildoptions.json", "intro-compilers.json"):
        (build / "meson-info" / name).write_text("{}")
    monkeypatch.setattr(sys, "argv", [str(SCRIPT), str(source_map), str(census), str(tool),
                                     "--input-status", str(statuses), "--allow-incomplete"])
    monkeypatch.setattr(summary_tool, "git", lambda *args: "test identity")
    monkeypatch.setattr(summary_tool.subprocess, "check_output",
                        lambda *args, **kwargs: "test libraries" if kwargs.get("text") else b"")
    summary_tool.main()
    summary = json.loads((tmp_path / "summary.json").read_text())
    assert json.loads(capsys.readouterr().out) == summary
    assert summary["complete"] is False and summary["unknown_input_ordinals"] == [2]
    assert summary["input_status_counts"]["exhausted"] == 1
    assert "two\n" not in (tmp_path / "guard-firing.list").read_text()
    identity = json.loads((tmp_path / "identity.json").read_text())
    assert identity["input_status"] == str(statuses.resolve())
    assert identity["input_status_sha256"] == hashlib.sha256(statuses.read_bytes()).hexdigest()
