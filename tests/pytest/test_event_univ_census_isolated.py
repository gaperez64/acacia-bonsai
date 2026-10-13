import csv
import importlib.util
import json
import os
from pathlib import Path
import signal
import subprocess
import sys

import pytest


SCRIPT = Path(__file__).resolve().parents[1] / "event-univ-census-isolated.py"


@pytest.fixture
def driver():
    spec = importlib.util.spec_from_file_location("event_univ_census_isolated", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@pytest.fixture
def fake_tool(tmp_path, driver):
    tool = tmp_path / "census"
    tool.write_text(f"#!{sys.executable}\n" + '''
import csv
import os
from pathlib import Path
import signal
import sys
import time
with open(sys.argv[1]) as source:
    rows = list(csv.reader(source, delimiter="\\t"))
assert len(rows) == 2
name = rows[1][0]
with (Path(sys.argv[2]) / "trace").open("a") as trace:
    trace.write(f"{name}\\t{os.getpid()}\\n")
''' + f"print({chr(9).join(driver.summary_tool.FIELDS)!r}, flush=True)\n" + '''
if name == "error":
    sys.exit(7)
if name == "signal":
    os.kill(os.getpid(), signal.SIGKILL)
''' + f"frames = {sorted(driver.summary_tool.FRAMES)!r}\n" + '''
for frame in frames:
    if name == "partial" and frame == "automaton-unreal":
        continue
    print(f"1\\tok\\t{frame}\\t1\\t1\\t0\\t64\\t0", flush=True)
if name == "slow":
    time.sleep(30)
''')
    tool.chmod(0o755)
    return tool


def test_fresh_processes_ordinals_header_and_summary(tmp_path, driver, fake_tool, monkeypatch):
    sources = tmp_path / "sources.tsv"
    sources.write_text("instance\ttlsf\nfirst\ta.tlsf\nsecond\tb.tlsf\n")
    output = tmp_path / "output"
    calls = []

    def summarize(command, **kwargs):
        calls.append(command)
        report, _ = driver.summary_tool.summarize(
            ["first", "second"], output / "census.tsv", output / "input-status.tsv", True)
        (output / "summary.json").write_text(json.dumps(report))
        return subprocess.CompletedProcess(command, 0)

    monkeypatch.setattr(driver.subprocess, "run", summarize)
    monkeypatch.setattr(sys, "argv", [str(SCRIPT), str(sources), str(tmp_path), str(fake_tool),
                                     "--output", str(output)])
    assert driver.main() == 0
    traces = (tmp_path / "trace").read_text().splitlines()
    assert [row.split("\t")[0] for row in traces] == ["first", "second"]
    assert len({row.split("\t")[1] for row in traces}) == 2
    with (output / "census.tsv").open() as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    assert len(rows) == 14
    assert [row["source_row"] for row in rows] == ["1"] * 7 + ["2"] * 7
    assert (output / "census.tsv").read_text().count("source_row\t") == 1
    assert calls == [[sys.executable, str(driver.SUMMARY), str(sources),
                      str(output / "census.tsv"), str(fake_tool), "--input-status",
                      str(output / "input-status.tsv"), "--allow-incomplete"]]
    assert json.loads((output / "run.json").read_text())["timeout_seconds"] == 120
    with pytest.raises(FileExistsError):
        driver.main()


@pytest.mark.parametrize("name,status,code", [("slow", "exhausted", 0), ("error", "error_7", 1),
                                             ("signal", "error_137", 1), ("partial", "ok", 1)])
def test_failed_or_incomplete_input_is_unknown_and_next_input_runs(
        tmp_path, driver, fake_tool, monkeypatch, name, status, code):
    sources = tmp_path / "sources.tsv"
    sources.write_text(f"instance\ttlsf\n{name}\ta.tlsf\nafter\tb.tlsf\n")
    output = tmp_path / "output"

    def summarize(command, **kwargs):
        report, _ = driver.summary_tool.summarize(
            [name, "after"], output / "census.tsv", output / "input-status.tsv", True)
        (output / "summary.json").write_text(json.dumps(report))
        return subprocess.CompletedProcess(command, 0)

    monkeypatch.setattr(driver.subprocess, "run", summarize)
    monkeypatch.setattr(sys, "argv", [str(SCRIPT), str(sources), str(tmp_path), str(fake_tool),
                                     "--output", str(output), "--timeout", "0.3"])
    assert driver.main() == code
    statuses = driver.summary_tool.read_input_status(output / "input-status.tsv", 2)
    assert statuses[1][0] == status and statuses[2][0] == "ok"
    report = json.loads((output / "summary.json").read_text())
    assert report["unknown_input_ordinals"] == [1]
    assert report["shipping_union"]["inputs"] == 1
    pid = int((tmp_path / "trace").read_text().splitlines()[0].split("\t")[1])
    with pytest.raises(ProcessLookupError):
        os.kill(pid, 0)


@pytest.mark.parametrize("cleanup_failure", [False, True])
def test_scope_limits_and_timeout_cleanup(tmp_path, driver, monkeypatch, cleanup_failure):
    commands = []

    class Process:
        pid = 123456
        waits = 0

        def wait(self, timeout=None):
            self.waits += 1
            if self.waits == 1:
                raise subprocess.TimeoutExpired("test", timeout)
            return -signal.SIGKILL

    def launch(command, **kwargs):
        assert kwargs["start_new_session"] is True
        commands.append(command)
        return Process()

    def cleanup(command, **kwargs):
        commands.append(command)
        if cleanup_failure:
            raise subprocess.TimeoutExpired(command, 10)
        return subprocess.CompletedProcess(command, 0)

    killed = []
    monkeypatch.setattr(driver.subprocess, "Popen", launch)
    monkeypatch.setattr(driver.subprocess, "run", cleanup)
    monkeypatch.setattr(driver.os, "killpg", lambda pid, sig: killed.append((pid, sig)))
    status, _ = driver.run_input(["tool", "map", "root"], 0.1,
                                tmp_path / "out", tmp_path / "err", True)
    assert status == "exhausted"
    assert commands[0][:4] == ["systemd-run", "--user", "--scope", "--quiet"]
    assert commands[0][5:] == ["-p", "MemoryMax=8G", "-p", "MemorySwapMax=0",
                              "tool", "map", "root"]
    assert commands[1] == ["systemctl", "--user", "kill", "--signal=SIGKILL", "--kill-whom=all",
                           commands[0][4].removeprefix("--unit=")]
    assert killed == [(123456, signal.SIGKILL)]


@pytest.mark.parametrize("bound", ["0", "-1", "nan", "inf"])
def test_invalid_bound(tmp_path, driver, monkeypatch, bound):
    monkeypatch.setattr(sys, "argv", [str(SCRIPT), "map", "root", "tool", "--output",
                                     str(tmp_path / "out"), "--timeout", bound])
    with pytest.raises(SystemExit) as error:
        driver.main()
    assert error.value.code == 2


@pytest.fixture
def local_summary(driver, monkeypatch):
    def install(output, ids):
        def summarize(command, **kwargs):
            report, _ = driver.summary_tool.summarize(
                ids, output / "census.tsv", output / "input-status.tsv", True)
            (output / "summary.json").write_text(json.dumps(report))
            return subprocess.CompletedProcess(command, 0)

        monkeypatch.setattr(driver.subprocess, "run", summarize)

    return install


def test_interrupt_and_resume_reuses_finished_inputs_and_rebuilds_outputs(
        tmp_path, driver, fake_tool, monkeypatch, local_summary):
    sources = tmp_path / "sources.tsv"
    names = ["first", "error", "slow", "interrupted", "last"]
    sources.write_text("instance\ttlsf\n" + "".join(f"{name}\t{name}.tlsf\n" for name in names))
    output = tmp_path / "output"
    local_summary(output, names)
    command = [str(SCRIPT), str(sources), str(tmp_path), str(fake_tool),
               "--output", str(output), "--timeout", "0.3"]
    monkeypatch.setattr(sys, "argv", command)
    run_input = driver.run_input

    def interrupt(command, timeout, stdout, stderr, scope):
        if stdout.stem == "0004":
            stdout.write_text("\t".join(driver.summary_tool.FIELDS) +
                              "\n1\tok\tautomaton-unreal\t")
            stderr.write_text("interrupted\n")
            raise KeyboardInterrupt
        return run_input(command, timeout, stdout, stderr, scope)

    monkeypatch.setattr(driver, "run_input", interrupt)
    with pytest.raises(KeyboardInterrupt):
        driver.main()
    saved = driver.summary_tool.read_input_status(output / "input-status.tsv", len(names))
    assert {ordinal: status for ordinal, (status, _) in saved.items()} == {
        1: "ok", 2: "error_7", 3: "exhausted"}
    artifacts = {path: path.read_bytes() for ordinal in saved for path in (
        output / "inputs" / f"{ordinal:04d}.tsv",
        output / "inputs" / f"{ordinal:04d}.stderr")}
    census = output / "census.tsv"
    with census.open("a") as stream:
        stream.write(census.read_text())
    monkeypatch.setattr(driver, "run_input", run_input)
    monkeypatch.setattr(sys, "argv", [*command, "--resume"])
    assert driver.main() == 1
    statuses = driver.summary_tool.read_input_status(output / "input-status.tsv", len(names))
    assert {ordinal: statuses[ordinal] for ordinal in saved} == saved
    assert statuses[4][0] == statuses[5][0] == "ok"
    assert all(path.read_bytes() == data for path, data in artifacts.items())
    traces = (tmp_path / "trace").read_text().splitlines()
    assert [row.split("\t")[0] for row in traces] == names
    with census.open() as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    assert [row["source_row"] for row in rows] == ["1"] * 7 + ["3"] * 7 + ["4"] * 7 + ["5"] * 7
    assert len({(row["source_row"], row["frame"], row["component"]) for row in rows}) == 28
    assert census.read_text().count("source_row\t") == 1
    report = json.loads((output / "summary.json").read_text())
    assert report["unknown_input_ordinals"] == [2, 3]
    assert report["shipping_union"]["inputs"] == 3
    rebuilt = {path: path.read_bytes() for path in (
        census, output / "input-status.tsv", output / "summary.json")}
    assert driver.main() == 1
    assert (tmp_path / "trace").read_text().splitlines() == traces
    assert all(path.read_bytes() == data for path, data in rebuilt.items())


@pytest.mark.parametrize("change", ["source_map", "tool", "corpus_root", "timeout", "scope",
                                    "missing_run", "invalid_run", "invalid_status"])
def test_resume_rejects_changed_identity_or_settings_before_writing(
        tmp_path, driver, fake_tool, monkeypatch, local_summary, capsys, change):
    sources = tmp_path / "sources.tsv"
    sources.write_text("instance\ttlsf\nfirst\ta.tlsf\n")
    output = tmp_path / "output"
    local_summary(output, ["first"])
    command = [str(SCRIPT), str(sources), str(tmp_path), str(fake_tool), "--output", str(output)]
    monkeypatch.setattr(sys, "argv", command)
    assert driver.main() == 0
    if change == "source_map":
        sources.write_text("instance\ttlsf\nfirst\tb.tlsf\n")
    elif change == "tool":
        with fake_tool.open("a") as stream:
            stream.write("# changed tool\n")
    elif change == "corpus_root":
        root = tmp_path / "other-corpus"
        root.mkdir()
        command[2] = str(root)
    elif change == "timeout":
        command.extend(["--timeout", "121"])
    elif change == "scope":
        command.append("--systemd-scope")
    elif change == "missing_run":
        (output / "run.json").unlink()
    elif change == "invalid_run":
        (output / "run.json").write_text("{")
    elif change == "invalid_status":
        (output / "input-status.tsv").write_text("1\tok\t0\n1\tok\t0\n")
    artifacts = {path: path.read_bytes() for path in output.rglob("*") if path.is_file()}
    trace = (tmp_path / "trace").read_bytes()
    monkeypatch.setattr(sys, "argv", [*command, "--resume"])
    with pytest.raises(SystemExit) as error:
        driver.main()
    assert error.value.code == 2
    assert "cannot resume:" in capsys.readouterr().err
    assert (tmp_path / "trace").read_bytes() == trace
    assert {path: path.read_bytes() for path in output.rglob("*") if path.is_file()} == artifacts


@pytest.mark.parametrize("missing", ["status", "stdout", "stderr"])
def test_resume_reruns_inputs_with_missing_artifacts(
        tmp_path, driver, fake_tool, monkeypatch, local_summary, missing):
    sources = tmp_path / "sources.tsv"
    sources.write_text("instance\ttlsf\nfirst\ta.tlsf\nsecond\tb.tlsf\n")
    output = tmp_path / "output"
    local_summary(output, ["first", "second"])
    command = [str(SCRIPT), str(sources), str(tmp_path), str(fake_tool), "--output", str(output)]
    monkeypatch.setattr(sys, "argv", command)
    assert driver.main() == 0
    if missing == "status":
        path = output / "input-status.tsv"
        path.write_text(path.read_text().splitlines(keepends=True)[0])
    else:
        suffix = "tsv" if missing == "stdout" else "stderr"
        (output / "inputs" / f"0002.{suffix}").unlink()
    monkeypatch.setattr(sys, "argv", [*command, "--resume"])
    assert driver.main() == 0
    assert [row.split("\t")[0] for row in (tmp_path / "trace").read_text().splitlines()] == [
        "first", "second", "second"]
    assert len(driver.summary_tool.read_input_status(output / "input-status.tsv", 2)) == 2
    with (output / "census.tsv").open() as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    assert [row["source_row"] for row in rows] == ["1"] * 7 + ["2"] * 7
