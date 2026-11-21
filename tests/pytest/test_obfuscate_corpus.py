"""Corpus preparation needs no solver or C++ build products."""
from __future__ import annotations

import csv
import pathlib
import random
import sys
from types import SimpleNamespace

import pytest

BENCHMARKING = pathlib.Path(__file__).resolve().parents[2] / "benchmarking"
sys.path.insert(0, str(BENCHMARKING))
try:
    from benchlib import load_tool
    obfuscate = load_tool("benchmarking/obfuscate-corpus.py")
finally:
    sys.path.pop(0)
coverage = obfuscate.coverage


def read_tsv(path):
    with path.open(newline="") as stream:
        return list(csv.DictReader(stream, delimiter="\t"))


@pytest.fixture
def panel(tmp_path, monkeypatch):
    corpus = tmp_path / "original"
    corpus.mkdir()
    instances = ["z-family.ltl", "a-family.ltl", "m-family.ltl", "reject.ltl"]
    for name, text in zip(instances, [
        "//STATUS: realizable\n", "//STATUS: realizable\n", "//STATUS: unknown\n", "REJECT\n",
    ]):
        (corpus / name.replace(".ltl", ".tlsf")).write_text(text)
    instance_list = tmp_path / "all.list"
    instance_list.write_text("# deliberately unsorted\n\n" + "\n".join(instances) + "\n")
    mapping = tmp_path / "map.tsv"
    coverage.atomic_write_tsv(mapping, ["instance", "tlsf"], [
        {"instance": name, "tlsf": name.replace(".ltl", ".tlsf")} for name in instances
    ])
    exceptions = tmp_path / "exceptions.tsv"
    coverage.atomic_write_tsv(exceptions, obfuscate.EXCEPTION_COLUMNS, [{
        "instance": "a-family.tlsf", "annotated_status": "realizable",
        "corrected_status": "unrealizable", "evidence": "reviewed correction",
    }])
    binary = tmp_path / "tlsfobf"
    binary.write_text(f"#!{sys.executable}\n" + '''
import argparse
import os
import pathlib
import sys
import time
p = argparse.ArgumentParser()
p.add_argument("--mode")
p.add_argument("--seed")
p.add_argument("--map", type=pathlib.Path)
p.add_argument("--output", type=pathlib.Path)
p.add_argument("source", type=pathlib.Path)
a = p.parse_args()
assert os.environ["OBF_INHERITED"] == "yes"
with open(os.environ["OBF_CALLS"], "a") as stream:
    stream.write(a.source.name + "\\n")
a.output.write_text('INFO { TITLE: "neutral" }\\n' + a.mode + a.seed + "\\n")
a.map.write_text("original\\tobfuscated\\tkind\\nold\\tn0\\tsignal\\n")
text = a.source.read_text()
if "REJECT" in text:
    print("self-check failure", file=sys.stderr)
    sys.exit(3)
if "TIMEOUT" in text:
    time.sleep(30)
if "EMPTY" in text:
    a.output.write_text("")
''')
    binary.chmod(0o755)
    calls = tmp_path / "calls"
    monkeypatch.setenv("OBF_INHERITED", "yes")
    monkeypatch.setenv("OBF_CALLS", str(calls))
    cli = ["--tlsfobf", str(binary), "--tlsf-map", str(mapping),
           "--tlsf-corpus", str(corpus), "--list", str(instance_list),
           "--mode", "all", "--seed", "42", "--status-exceptions", str(exceptions)]
    return SimpleNamespace(corpus=corpus, instances=instances, instance_list=instance_list,
                           binary=binary, calls=calls, cli=cli, output=tmp_path / "out")


def run(panel, *extra, output=None):
    return obfuscate.main([*panel.cli, "--output", str(output or panel.output), *extra])


def snapshot(output):
    return {str(path.relative_to(output)): path.read_bytes()
            for path in output.rglob("*") if path.is_file()}


@pytest.mark.parametrize("mode", ["rename", "reorder", "all"])
def test_obfuscation_is_neutral_deterministic_and_resumable(panel, mode):
    assert run(panel, "--mode", mode) == 0
    manifest = list(obfuscate.read_manifest(panel.output / "manifest.tsv").values())
    assert [row["instance"] for row in manifest] == panel.instances
    shuffled = panel.instances.copy()
    random.Random(42).shuffle(shuffled)
    names = {name: f"n{index:06d}.tlsf" for index, name in enumerate(shuffled)}
    assert [row["neutral_tlsf"] for row in manifest] == [names[name] for name in panel.instances]
    assert [row["neutral_tlsf"] for row in manifest] != sorted(names.values())
    assert [row["exit_status"] for row in manifest] == ["0", "0", "0", "3"]
    assert manifest[-1]["reason"] == "self-check failure"
    assert manifest[-1]["neutral_sha256"] == ""
    mapped = read_tsv(panel.output / "tlsf-sources.tsv")
    assert [row["instance"] for row in mapped] == panel.instances[:-1]
    assert coverage.read_instance_list(panel.output / "instances.list") == panel.instances[:-1]
    assert {path.name for path in (panel.output / "corpus").iterdir()} == {
        names[name] for name in panel.instances[:-1]
    }
    for row in manifest[:-1]:
        target = panel.output / "corpus" / row["neutral_tlsf"]
        assert row["neutral_sha256"] == coverage.sha256_file(target)
        assert row["original_sha256"] == coverage.sha256_file(pathlib.Path(row["original_tlsf"]))
        assert row["tlsfobf_sha256"] == coverage.sha256_file(panel.binary)
        assert "STATUS" not in target.read_text()
    assert len(list((panel.output / "maps").glob("*.map"))) == 3
    before = snapshot(panel.output)
    assert run(panel, "--mode", mode) == 0
    assert len(panel.calls.read_text().splitlines()) == 4  # Cached failures as well as successes.
    assert snapshot(panel.output) == before
    other = panel.output.with_name("fresh")
    assert run(panel, "--mode", mode, output=other) == 0
    assert snapshot(other) == before


def test_carried_expectations_keep_corrections_and_unknowns(panel, monkeypatch):
    assert run(panel) == 0
    rows = obfuscate.read_manifest(panel.output / "manifest.tsv")
    assert [(row["expected"], row["expectation_source"]) for row in rows.values()] == [
        ("REALIZABLE", "status"), ("UNREALIZABLE", "exception"), ("", "none"), ("", "none"),
    ]
    exceptions = panel.output / "status-exceptions.tsv"
    assert coverage.read_status_exceptions(exceptions, panel.output / "corpus") == {
        rows["z-family.ltl"]["neutral_tlsf"]: "REALIZABLE",
        rows["a-family.ltl"]["neutral_tlsf"]: "UNREALIZABLE",
    }
    # Exercise the runner's real expectation/conflict path after removing every trailer.
    monkeypatch.setattr(coverage, "run_systemd_scope", lambda *a, **k: coverage.RunResult(
        stdout="REALIZABLE\n", stderr="", returncode=0, seconds=0.01, timed_out=False,
    ))
    args = coverage.build_parser().parse_args([
        "--bin", str(panel.binary), "--solver-label", "obfuscated",
        "--list", str(panel.output / "instances.list"),
        "--tlsf-map", str(panel.output / "tlsf-sources.tsv"),
        "--tlsf-corpus", str(panel.output / "corpus"), "--status-exceptions", str(exceptions),
        "--caps", "17", "--memory-max", "1500M", "--memory-swap-max", "0",
        "--output", str(panel.output / "screen.tsv"), "--conflict-policy", "collect",
    ])
    assert coverage.run(args) == 3
    conflicts = read_tsv(panel.output / "screen-conflicts.tsv")
    assert [(row["instance"], row["expected"], row["expectation_source"]) for row in conflicts] == [
        ("a-family.ltl", "UNREALIZABLE", "exception"),
    ]


@pytest.mark.parametrize("text,exit_status,reason", [
    ("TIMEOUT\n", "124", "timeout after 0.5 s"),
    ("EMPTY\n", "invalid-output", "without TLSF output"),
])
def test_failures_clean_partial_outputs_and_do_not_stop_run(panel, text, exit_status, reason):
    (panel.corpus / "m-family.tlsf").write_text(text)
    assert run(panel, "--timeout", "0.5") == 0
    rows = obfuscate.read_manifest(panel.output / "manifest.tsv")
    failed = rows["m-family.ltl"]
    assert failed["exit_status"] == exit_status
    assert reason in failed["reason"]
    assert not (panel.output / "corpus" / failed["neutral_tlsf"]).exists()
    assert len(panel.calls.read_text().splitlines()) == 4


def test_resume_repairs_corruption_and_restarts_after_partial_manifest(panel):
    assert run(panel) == 0
    original = snapshot(panel.output)
    rows = list(obfuscate.read_manifest(panel.output / "manifest.tsv").values())
    coverage.atomic_write_tsv(panel.output / "manifest.tsv", obfuscate.MANIFEST_COLUMNS, rows[:2])
    assert run(panel) == 0
    assert snapshot(panel.output) == original
    (panel.output / "maps" / pathlib.Path(rows[0]["neutral_tlsf"]).with_suffix(".map")).write_text("bad")
    (panel.output / "corpus" / rows[1]["neutral_tlsf"]).unlink()
    assert run(panel) == 0
    assert snapshot(panel.output) == original
    assert len(panel.calls.read_text().splitlines()) == 8


@pytest.mark.parametrize("changed", ["mode", "seed", "source", "binary", "list"])
def test_resume_refuses_changed_inputs_without_touching_outputs(panel, changed):
    assert run(panel) == 0
    before = snapshot(panel.output)
    extra = []
    if changed in {"mode", "seed"}:
        extra = [f"--{changed}", "rename" if changed == "mode" else "43"]
    elif changed == "source":
        with (panel.corpus / "m-family.tlsf").open("a") as stream:
            stream.write("// changed\n")
    elif changed == "binary":
        with panel.binary.open("a") as stream:
            stream.write("\n# changed\n")
    else:
        panel.instance_list.write_text("\n".join(reversed(panel.instances)))
    assert run(panel, *extra) == 2
    assert snapshot(panel.output) == before


def test_duplicate_list_rejected_before_running_tool(panel):
    panel.instance_list.write_text("z-family.ltl\nz-family.ltl\n")
    assert run(panel) == 2
    assert not panel.calls.exists()


def test_output_cannot_overwrite_original_corpus(panel):
    panel.output.mkdir()
    (panel.output / "corpus").symlink_to(panel.corpus, target_is_directory=True)
    before = snapshot(panel.corpus)
    assert run(panel) == 2
    assert snapshot(panel.corpus) == before
    assert not panel.calls.exists()


def test_different_seed_changes_neutral_assignment(panel):
    assert run(panel) == 0
    other = panel.output.with_name("seed43")
    assert run(panel, "--seed", "43", output=other) == 0
    assert read_tsv(panel.output / "tlsf-sources.tsv") != read_tsv(other / "tlsf-sources.tsv")
