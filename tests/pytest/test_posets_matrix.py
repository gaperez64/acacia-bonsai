"""Integrity checks for the offline Posets matrix; never launch a benchmark."""

import importlib.util
from contextlib import nullcontext
import json
from pathlib import Path
import sys

import pytest

BENCHMARKING = Path(__file__).resolve().parents[2] / "benchmarking"
sys.path.insert(0, str(BENCHMARKING))
SPEC = importlib.util.spec_from_file_location("posets_matrix", BENCHMARKING / "posets-matrix.py")
M = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(M)


def capture(tmp_path):
    snapshot = tmp_path / "snapshots"
    directory = snapshot / "aut-1-0"
    directory.mkdir(parents=True)
    (directory / "meta.tsv").write_text(
        "schema_version\tstates\tbool_threshold\tinit_state\tworker\tinstance\n"
        "3\t2\t0\t0\tfixture\tfixture\n")
    (directory / "automaton.hoa").write_text("HOA fixture: provenance only\n")
    (directory / "cpre-1.skipped").write_text("actions=5000 cap=4096\n")
    (directory / "contains-1.tsv.partial").write_text("# incomplete")
    trace = directory / "contains-0.tsv"
    trace.write_text("# schema_version=1 k=2 loop=1 maxima=1 expected=1 bin=0 ordinal=1 hits=1 origin=picker\n"
                     "[query]\n0\t-1\n[generators]\n0\t0\n[end]\n")
    manifest = tmp_path / "capture.json"
    def file(name, content):
        path = tmp_path / name
        path.write_text(content)
        return {"path": str(path), "sha256": M.sha(path)}

    binary = file("binary", "trusted fixture; never executed\n")
    tlsf = file("input.tlsf", "fixture\n")
    proof = file("provenance.json", json.dumps({"binary_sha256": binary["sha256"], "source_revision": "a" * 40,
                                               "meson_options": [{"name": "acacia_enable_diagnostics", "value": True}]}))
    library = file("libfixture.so", "fixture library\n")
    implementation = {str(tmp_path / name): file(name, name)["sha256"]
                      for name in ("posets-capture.py", "posets-matrix.py", "benchlib.py", "scope_memory.py")}
    stdout, stderr = file("stdout", "UNKNOWN\n"), file("stderr", "")
    document = {"schema_version": 1, "kind": "posets-operation-capture", "instance": "fixture",
                "input": tlsf, "binary": binary, "frozen_provenance": proof,
                "implementation_sha256": implementation,
                "implementation_source_paths": {Path(path).name: path for path in implementation},
                "runtime_libraries": [{**library, "realpath": library["path"]}],
                "snapshot_directory": str(snapshot),
                "command": [binary["path"], "--arms", "real:small:backward", "--spot-fast", "off",
                            "--r-prepass", "off", "--equivariance", "off", "-T", tlsf["path"]],
                "environment": {"ACACIA_DIAG": "1", "ACACIA_DIAG_INSTANCE": "fixture", "ACACIA_DIAG_PROGRESS_EVERY": "0",
                                "ACACIA_ANTICHAIN_SNAPSHOT_DIR": str(snapshot), "ACACIA_ANTICHAIN_SNAPSHOT_CPRE": "1",
                                "ACACIA_ANTICHAIN_SNAPSHOT_CPRE_FRONTIERS": "1", "ACACIA_ANTICHAIN_SNAPSHOT_CONTAINS": "1",
                                "ACACIA_ANTICHAIN_SNAPSHOT_CPRE_MAX": "8", "ACACIA_ANTICHAIN_SNAPSHOT_CPRE_MAX_ACTIONS": "4096",
                                "ACACIA_ANTICHAIN_SNAPSHOT_CONTAINS_MAX": "16"},
                "result": {"returncode": 2, "seconds": 0, "timed_out": False, "resource_limited": False},
                "classification": "UNKNOWN",
                "treatment": {"timeout": 17, "memory_max": "8G", "memory_swap_max": "0", "serial": True},
                "logs_sha256": {"stdout_sha256": stdout["sha256"], "stderr_sha256": stderr["sha256"]}}
    manifest.write_text(json.dumps(document))
    refresh_manifest(manifest)
    plan = tmp_path / "plan.json"
    entries = [{"instance": "fixture", "family": "fixture", "split": "train",
                "path": str(trace), "capture_manifest": str(manifest)}]
    plan.write_text(json.dumps({"schema_version": 1, "traces": entries}))
    return plan, trace, entries


def refresh_manifest(manifest):
    document = json.loads(manifest.read_text())
    snapshot = Path(document["snapshot_directory"])
    document["snapshot_files_sha256"] = {str(path.relative_to(snapshot)): M.sha(path)
                                           for path in snapshot.rglob("*") if path.is_file()}
    manifest.write_text(json.dumps(document))


def test_all_boolean_is_not_zero_dimension_and_failed_samples_are_bound(tmp_path):
    plan, trace, _ = capture(tmp_path)
    rows, sources = M.load_traces(plan)
    assert rows[0]["features"] == {"dimension": 2, "numeric_prefix": 0, "boolean_tail": 2,
                                   "maxima": 1, "bottom_fraction": 0, "unique_numeric_prefixes": 1,
                                   "unique_boolean_suffixes": 1, "recorded_answer": 1}
    directory = sources["directories"][str(trace.parent)]
    assert directory["complete_operation_counts"] is None
    assert {Path(path).suffix for path in directory["files_sha256"]} >= {".partial", ".skipped"}


@pytest.mark.parametrize("mutation", ["duplicate", "family_split", "instance_split", "partial", "symlink"])
def test_invalid_trace_plan_rejected(tmp_path, mutation):
    plan, trace, entries = capture(tmp_path)
    if mutation == "duplicate":
        entries.append(dict(entries[0]))
    elif mutation in ("family_split", "instance_split"):
        other = trace.with_name("contains-2.tsv")
        other.write_bytes(trace.read_bytes())
        entries.append({**entries[0], "path": str(other), "split": "heldout"})
        if mutation == "instance_split":
            entries[-1]["family"] = "another-family"
    elif mutation == "partial":
        entries[0]["path"] += ".partial"
        Path(entries[0]["path"]).write_bytes(trace.read_bytes())
    else:
        copy = trace.with_suffix(".copy")
        trace.rename(copy)
        trace.symlink_to(copy)
    refresh_manifest(Path(entries[0]["capture_manifest"]))
    plan.write_text(json.dumps({"schema_version": 1, "traces": entries}))
    expected = {"duplicate": "Repeated trace", "family_split": "family crosses", "instance_split": "instance crosses",
                "partial": "finalized operation", "symlink": "nonregular"}[mutation]
    with pytest.raises(ValueError, match=expected):
        M.load_traces(plan)


@pytest.mark.parametrize("row", ["1\t4\t2\t2\t8\n", "1\t4\t2\t1\t8\n", "1\t4\t2\t2\t8"])
def test_operation_counts_require_complete_consistent_table(tmp_path, row):
    plan, trace, _ = capture(tmp_path)
    (trace.parent / "operation-counts.tsv").write_text("complete\tcontains\thits\tmisses\tcpre_calls\n" + row)
    refresh_manifest(tmp_path / "capture.json")
    if row == "1\t4\t2\t2\t8\n":
        _, source = M.load_traces(plan)
        assert source["directories"][str(trace.parent)]["complete_operation_counts"]["contains"] == 4
    else:
        with pytest.raises(ValueError):
            M.load_traces(plan)


@pytest.mark.parametrize("mutation", ["input", "binary", "runtime", "command", "bool_exit", "classification", "nan_seconds", "extra_snapshot", "snapshot_bytes"])
def test_capture_identity_must_bind_invocation_and_all_bytes(tmp_path, mutation):
    plan, trace, _ = capture(tmp_path)
    manifest = tmp_path / "capture.json"
    document = json.loads(manifest.read_text())
    if mutation in ("input", "binary"):
        Path(document[mutation]["path"]).write_text("changed bytes")
    elif mutation == "runtime":
        Path(document["runtime_libraries"][0]["path"]).write_text("changed library")
    elif mutation == "command":
        document["command"][2] = "other-arm"
        manifest.write_text(json.dumps(document))
    elif mutation == "bool_exit":
        document["result"]["returncode"] = False
        manifest.write_text(json.dumps(document))
    elif mutation in ("classification", "nan_seconds"):
        if mutation == "classification":
            document["classification"] = "TIMEOUT"
        else:
            document["result"]["seconds"] = float("nan")
        manifest.write_text(json.dumps(document))
    elif mutation == "extra_snapshot":
        (trace.parent / "new-file").write_text("unbound")
    else:
        trace.write_text("changed operation")
    with pytest.raises(ValueError):
        M.load_traces(plan)


def test_exact_output_identity_and_metrics():
    line = "[--] running tests for posets::downsets::vector_backed<posets::vectors::simd_vector_backed<test_value_type>>\n"
    assert M.output_identity(line, "vector_backed", "simd_vector_backed")
    assert not M.output_identity(line + line.replace("::vector_backed<", "::vector_backed_bin<"), "vector_backed", "simd_vector_backed")
    assert not M.output_identity(line, "vector_backed_bin", "simd_vector_backed")
    assert M.metrics("  - cpre: 0.001\n") == {"cpre": 0.001}
    for malformed in ("", "  - cpre: nan\n", "  - cpre: -1\n", "  - cpre: 1\n  - cpre: 2\n"):
        with pytest.raises(ValueError):
            M.metrics(malformed)


@pytest.mark.parametrize("mutation", ["wrong_binary", "missing_revision", "disabled", "integer_true", "wrong_option"])
def test_frozen_provenance_binds_actual_binary_and_diagnostic_option(tmp_path, mutation):
    plan, _, _ = capture(tmp_path)
    proof = tmp_path / "provenance.json"
    document = json.loads(proof.read_text())
    if mutation == "wrong_binary":
        document["binary_sha256"] = "b" * 64
    elif mutation == "missing_revision":
        document["source_revision"] = ""
    elif mutation in ("disabled", "integer_true"):
        document["meson_options"][0]["value"] = False if mutation == "disabled" else 1
    else:
        document["meson_options"][0]["name"] = "acacia_diagnostics"
    proof.write_text(json.dumps(document))
    manifest = tmp_path / "capture.json"
    declared = json.loads(manifest.read_text())
    declared["frozen_provenance"]["sha256"] = M.sha(proof)
    manifest.write_text(json.dumps(declared))
    with pytest.raises(ValueError, match="Frozen"):
        M.load_traces(plan)


def test_planned_order_is_saved_and_source_timeout_never_qualifies(tmp_path, monkeypatch):
    from argparse import Namespace
    from benchlib import RunResult

    monkeypatch.setattr(M, "campaign_scope_guard", lambda _: nullcontext())
    mode = {"binary": "/trusted/fixture", "backends": ["vector_backed", "vector_backed_bin"]}
    metadata = {"modes": {"compatible": mode, "native": mode},
                "aliases": {"compatible": "compatible", "native": "native"}}
    args = Namespace(case_timeout=1, check_timeout=4, source_timeout=7, memory_max="8G", backends=mode["backends"], measure=True,
                     repetitions=3, rounds=1, detail=False, seed=0)
    trace = {"id": 0, "path": "/trace", "features": {"dimension": 2, "maxima": 1, "boolean_tail": 2}}
    measured = []

    def fake_run(command, cap, *_args, **_kwargs):
        if "--check=trace-source" in command:
            assert cap == 7
            return RunResult("[TRACE SOURCE OK]\n", "", 0, 0, False)
        backend, vector = command[-2:]
        output = f"[--] running tests for posets::downsets::{backend}<posets::vectors::{vector}>\n"
        if "--check=replay" not in command:
            assert cap == 1
            assert (tmp_path / "measurement-order.json").is_file()
            measured.append(command)
            output += "  - rounds: 1\n  - dimension: 2\n  - boolean_tail: 2\n  - before_maxima: 1\n  - cpre: 0.001\n"
        else:
            assert cap == 4
        return RunResult(output, "", 0, 0, False)

    monkeypatch.setattr(M, "run_systemd_scope", fake_run)
    rows = M.replay(args, metadata, [trace], tmp_path)
    assert len([row for row in rows if row["phase"] == "check"]) == 6  # scalar once, SIMD twice per backend
    assert len(measured) == 12  # SIMD treatments only, three repetitions
    plan = json.loads((tmp_path / "measurement-order.json").read_text())
    assert {row["vector"] for row in plan} == {"simd_vector_backed"}
    assert [(row["mode"], row["backend"]) for row in plan[:4]] != [(row["mode"], row["backend"]) for row in plan[4:8]]
    assert all(row["logs_sha256"] for row in rows if "command" in row)
    monkeypatch.setattr(M, "run_systemd_scope", lambda *_args, **_kwargs: RunResult("", "", 124, 1, True))
    measured.clear()
    rows = M.replay(args, metadata, [trace], tmp_path)
    assert rows[0]["status"] == "timeout"
    assert all(row["status"] == "source_unverified" for row in rows[1:])
    assert not measured and not json.loads((tmp_path / "measurement-order.json").read_text())


@pytest.mark.parametrize("mutation", ["none", "ndebug", "stdlib", "optimization", "lto", "target"])
def test_measurements_require_comparable_release_builds(mutation):
    flags = ["c++", "-O3", "-flto=auto", "-march=native", "-std=c++20", "-DNDEBUG"]
    modes = {name: {"ndebug": True, "stdlib_assertions": False, "compiler": "fixture",
                    "compiler_command": {"arguments": list(flags)}} for name in ("compatible", "native", "max")}
    native = modes["native"]
    if mutation == "ndebug":
        native["ndebug"] = False
    elif mutation == "stdlib":
        native["stdlib_assertions"] = True
    elif mutation == "optimization":
        native["compiler_command"]["arguments"].append("-O2")
    elif mutation == "lto":
        native["compiler_command"]["arguments"].remove("-flto=auto")
    elif mutation == "target":
        native["compiler_command"]["arguments"].append("-march=x86-64")
    if mutation == "none":
        M.measurement_build({"modes": modes})
    else:
        with pytest.raises(ValueError):
            M.measurement_build({"modes": modes})
