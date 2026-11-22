"""Synthesis rows retain failures; size comparisons use the same verified panel."""

import csv
from pathlib import Path
import sys

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "benchmarking"))
from benchlib import RunResult, load_tool, run_process_group  # noqa: E402

runner = load_tool("benchmarking/synthesis-size.py")


def executable(path, body):
    path.write_text("#!/usr/bin/env python3\n" + body)
    path.chmod(0o755)
    return path


@pytest.fixture
def tools(tmp_path):
    binary = executable(tmp_path / "solver", '''
import pathlib, sys
pathlib.Path(sys.argv[sys.argv.index('-s') + 1]).write_text(
    'aag 1 1 0 1 0\\n2\\n2\\ni0 request\\no0 grant\\nc\\n')
print('REALIZABLE')
''')
    checker = executable(tmp_path / "checker", "print('verified')\n")
    source = tmp_path / "input.tlsf"
    source.write_text("//STATUS : realizable\n//REF_SIZE : 23\n")
    return binary, checker, source


def measure(tmp_path, tools, launch=run_process_group):
    binary, checker, source = tools
    return runner.measure("input.ltl", source, "member", binary, "digest", checker,
                          tmp_path / "result", 1, 1, launch, "REALIZABLE", "status")


def test_fake_binary_row_and_summary(tmp_path, tools):
    row = measure(tmp_path, tools)
    assert row["verdict"] == "REALIZABLE"
    assert row["verification"] == "verified" and row["failure_kind"] == ""
    assert row["REF_SIZE"] == "23" and row["binary_sha256"] == "digest"
    assert [row[c] for c in ("M", "I", "L", "O", "A")] == [1, 1, 0, 1, 0]
    assert float(row["wall_seconds"]) >= 0
    assert runner.summarize([row], ["member"])[0] == dict(
        member="member", attempted=1, realizable=1, controllers_produced=1, verified=1,
        **dict.fromkeys(runner.SUMMARY_FAILURES, 0), commonly_verified=1,
        total_ands_latches=0, median_ands_latches=0)


@pytest.mark.parametrize("body,kind", [
    ("print('failed_verification'); raise SystemExit(1)", "failed_verification"),
    ("print('verify_unsupported'); raise SystemExit(3)", "verify_unsupported"),
    ("print('verify_unsupported')", "verify_error"),
    ("raise SystemExit(1)", "verify_error"),
    ("raise SystemExit(2)", "verify_error"),
    ("print('something else')", "verify_error"),
])
def test_verifier_failures(tmp_path, tools, body, kind):
    executable(tools[1], body)
    row = measure(tmp_path, tools)
    assert row["verification"] == row["failure_kind"] == kind
    summary = runner.summarize([row], ["member"])[0]
    assert summary["commonly_verified"] == 0
    assert summary[kind] == 1
    assert summary["failed_verification"] == int(kind == "failed_verification")


@pytest.mark.parametrize("timed_out,resource_limited,kind", [
    (True, False, "verify_timeout"), (False, True, "verify_resource_limit"),
])
def test_verification_limits_are_separate(tmp_path, tools, timed_out, resource_limited, kind):
    def launch(command, timeout):
        if command[0] == str(tools[0]):
            return run_process_group(command, timeout)
        return RunResult("", "", -9, 1, timed_out, resource_limited=resource_limited)

    row = measure(tmp_path, tools, launch)
    assert row["verification"] == kind
    summary = runner.summarize([row], ["member"])[0]
    assert summary["verify_timeout"] == int(timed_out)
    assert summary["verify_resource_limit"] == int(resource_limited)
    assert summary["failed_verification"] == 0


@pytest.mark.parametrize("body,kind", [
    ("print('REALIZABLE')", "invalid_controller"),
    ("print('REALIZABLE'); raise SystemExit(3)", "error"),
    ("print('UNREALIZABLE'); raise SystemExit(1)", "unrealizable"),
    ("print('UNKNOWN'); raise SystemExit(2)", "unknown"),
    ("import time; time.sleep(5)", "timeout"),
    ("import pathlib,sys; pathlib.Path(sys.argv[-1]).write_text('broken'); "
     "print('REALIZABLE')", "invalid_controller"),
])
def test_synthesis_failures(tmp_path, tools, body, kind):
    executable(tools[0], body)
    row = measure(tmp_path, tools)
    assert row["failure_kind"] == kind
    assert row["verification"] != "verified"
    assert row["A"] == ""
    summary = runner.summarize([row], ["member"])[0]
    assert summary["failed_verification"] == 0
    assert sum(summary[column] for column in runner.SUMMARY_FAILURES) == 1
    column = {"timeout": "synthesis_timeout", "unrealizable": "unrealizable",
              "invalid_controller": "invalid_controller"}.get(kind, f"synthesis_{kind}")
    assert summary[column] == 1
    if kind == "error":
        assert row["verdict"] == "UNKNOWN"


def test_summary_intersects_verified_inputs():
    def row(instance, member, verified, ands, latches):
        return dict(instance=instance, member=member, verification=verified,
                    verdict="REALIZABLE", A=ands, L=latches,
                    failure_kind="" if verified == "verified" else verified)

    rows = [row("a", "one", "verified", 4, 1), row("b", "one", "verified", 100, 2),
            row("a", "two", "verified", 2, 1),
            row("b", "two", "failed_verification", 1, 0)]
    one, two = runner.summarize(rows, ["one", "two"])
    assert one["commonly_verified"] == two["commonly_verified"] == 1
    assert one["total_ands_latches"] == one["median_ands_latches"] == 5
    assert two["total_ands_latches"] == two["median_ands_latches"] == 3
    assert one["failed_verification"] == 0 and two["failed_verification"] == 1
    empty = runner.summarize(rows, ["one", "two", "absent"])
    assert all(s["total_ands_latches"] == s["median_ands_latches"] == "" for s in empty)


@pytest.mark.parametrize("budget", ["0", "-1", "4294967296", "invalid"])
def test_cli_rejects_invalid_state_budget(tmp_path, budget):
    with pytest.raises(SystemExit) as error:
        runner.main(["--bin", "member=unused", "--checker", "unused", "--list", "unused",
                     "--tlsf-map", "unused", "--output", str(tmp_path / "output"),
                     "--verify-max-states", budget])
    assert error.value.code == 2
    assert not (tmp_path / "output").exists()


@pytest.mark.parametrize("status,selected", [
    ("REALIZABLE", ["input.ltl", "second.ltl"]),
    ("UNREALIZABLE", ["third.ltl"]),
    ("UNKNOWN", ["fourth.ltl", "fifth.ltl"]),
    ("all", ["input.ltl", "second.ltl", "third.ltl", "fourth.ltl", "fifth.ltl"]),
])
def test_cli_status_selection_and_corrections(tmp_path, tools, monkeypatch, status, selected):
    binary, checker, source = tools
    second = tmp_path / "second.tlsf"
    second.write_text("//STATUS : unrealizable\n")
    third = tmp_path / "third.tlsf"
    third.write_text("//STATUS : unrealizable\n")
    (tmp_path / "fourth.tlsf").write_text("//STATUS : realizable\n")
    (tmp_path / "fifth.tlsf").write_text("//STATUS : unknown\n")
    instances = tmp_path / "inputs.list"
    instances.write_text("# panel\ninput.ltl\nsecond.ltl\nthird.ltl\nfourth.ltl\nfifth.ltl\n")
    mapping = tmp_path / "sources.tsv"
    mapping.write_text("instance\ttlsf\ninput.ltl\tinput.tlsf\n"
                       "second.ltl\tsecond.tlsf\nthird.ltl\tthird.tlsf\n"
                       "fourth.ltl\tfourth.tlsf\nfifth.ltl\tfifth.tlsf\n")
    exceptions = tmp_path / "exceptions.tsv"
    exceptions.write_text("instance\tannotated_status\tcorrected_status\tevidence\n"
                          "second.tlsf\tunrealizable\trealizable\treviewed\n"
                          "fourth.tlsf\trealizable\tnone\twithdrawn\n")
    output = tmp_path / "output"
    arguments = ["--bin", f"member={binary}", "--checker", str(checker),
                 "--list", str(instances), "--tlsf-map", str(mapping),
                 "--tlsf-corpus", str(source.parent), "--status-exceptions", str(exceptions),
                 "--output", str(output), "--verify-max-states", "17",
                 "--systemd-scope", "--memory-max", "5G", "--memory-swap-max", "0"]
    if status != "REALIZABLE":
        arguments.extend(["--expected-status", status])
    def scoped(command, timeout, **kwargs):
        assert kwargs == dict(memory_max="5G", memory_swap_max="0")
        if command[0] == str(checker):
            assert command[-2:] == ["--max-states", "17"]
        return run_process_group(command, timeout)

    monkeypatch.setattr(runner, "run_systemd_scope", scoped)
    assert runner.main(arguments) == 0
    with (output / "rows.tsv").open() as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    assert [r["instance"] for r in rows] == selected
    assert all(r["expectation_source"] == ("exception" if r["instance"] == "second.ltl"
                                          else "none" if r["instance"] in {"fourth.ltl", "fifth.ltl"}
                                          else "status") for r in rows)
    assert all(len(r["binary_sha256"]) == 64 for r in rows)
    assert (output / "summary.tsv").is_file()
    with pytest.raises(FileExistsError):
        runner.main(arguments)
