from __future__ import annotations

import csv
import json
import pathlib
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]
NATIVE_CONVERTER = ROOT / "benchmarking" / "convert-tlsf-corpus-native.py"
RESULT_CONVERTER = ROOT / "benchmarking" / "syntcomp-results-reference.py"


def test_native_converter_handles_empty_signal_class_and_removes_stale_failure(
    tmp_path,
):
    source = tmp_path / "tlsf"
    output = tmp_path / "ltl"
    source.mkdir()
    output.mkdir()
    (source / "good.tlsf").write_text("good\n")
    (source / "bad.tlsf").write_text("bad\n")
    inspector = tmp_path / "inspect"
    inspector.write_text(
        "#!/usr/bin/env python3\n"
        "import pathlib, sys\n"
        "if pathlib.Path(sys.argv[1]).stem == 'bad':\n"
        "    raise SystemExit(1)\n"
        "sys.stdout.buffer.write(b'G i\\0i\\0\\0Mealy\\0Mealy\\0Mealy\\0')\n"
    )
    inspector.chmod(0o755)

    good_selection = tmp_path / "good.list"
    good_selection.write_text("# selected\ngood.tlsf\n")
    result = subprocess.run(
        [
            sys.executable,
            str(NATIVE_CONVERTER),
            str(source),
            str(output),
            "--native-inspect",
            str(inspector),
            "--selection",
            str(good_selection),
        ],
        check=False,
    )
    assert result.returncode == 0
    assert (output / "good.ltl").read_text() == "G i\n"
    assert (output / "good.part").read_text() == ".inputs i\n.outputs\n"

    (output / "bad.ltl").write_text("stale\n")
    (output / "bad.part").write_text("stale\n")
    bad_selection = tmp_path / "bad.list"
    bad_selection.write_text("bad.tlsf\n")
    result = subprocess.run(
        [
            sys.executable,
            str(NATIVE_CONVERTER),
            str(source),
            str(output),
            "--native-inspect",
            str(inspector),
            "--selection",
            str(bad_selection),
        ],
        check=False,
    )
    assert result.returncode == 1
    assert not (output / "bad.ltl").exists()
    assert not (output / "bad.part").exists()


def test_official_results_reference_applies_cap_and_materializes_missing_rows(
    tmp_path,
):
    results = tmp_path / "results.csv"
    fields = [
        "id",
        "inst",
        "timeSolveWall",
        "timeSolveCPU",
        "memSolve",
        "statusSolve",
        "resultSolve",
        "realStatus",
        "Error",
    ]
    rows = [
        ["2", "a", "0.5", "", "", "ok", "0", "REALIZABLE", ""],
        ["2", "b", "17", "", "", "ok", "1", "UNREALIZABLE", ""],
        ["6", "a", "0.4", "", "", "ok", "0", "REALIZABLE", ""],
        ["6", "c", "1.0", "", "", "out", "1", "UNREALIZABLE", ""],
    ]
    with results.open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(fields)
        writer.writerows(rows)
    tlsf = tmp_path / "tlsf"
    corpus = tmp_path / "corpus"
    reference = tmp_path / "reference"
    tlsf.mkdir()
    corpus.mkdir()
    for name in ("a", "b", "c"):
        (tlsf / f"{name}.tlsf").write_text("INFO {}\n")
        (corpus / f"{name}.ltl").write_text("1\n")

    result = subprocess.run(
        [
            sys.executable,
            str(RESULT_CONVERTER),
            str(results),
            "--tlsf-dir",
            str(tlsf),
            "--corpus",
            str(corpus),
            "--reference",
            str(reference),
            "--selection",
            str(tmp_path / "selection.list"),
            "--series",
            "acacia=2",
            "--series",
            "ltlsynt=6",
            "--expected",
            "3",
            "--cap",
            "17",
        ],
        check=False,
    )
    assert result.returncode == 0
    acacia = [
        json.loads(line)
        for line in (reference / "acacia.json").read_text().splitlines()
    ]
    assert [row["result"] for row in acacia] == ["OK", "TIMEOUT", "TIMEOUT"]
    assert [row["duration"] for row in acacia] == [0.5, 17.0, 17.0]
    ltlsynt = [
        json.loads(line)
        for line in (reference / "ltlsynt.json").read_text().splitlines()
    ]
    assert [row["result"] for row in ltlsynt] == ["OK", "TIMEOUT", "TIMEOUT"]
