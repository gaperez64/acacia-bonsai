"""Scope reconciliation tests; every systemctl call is simulated."""

import importlib.util
import os
import pathlib
import subprocess
import sys

import pytest


BENCHMARKING = pathlib.Path(__file__).resolve().parents[2] / "benchmarking"
sys.path.insert(0, str(BENCHMARKING))
try:
    import benchlib
finally:
    sys.path.pop(0)


RUNNING = "acacia-bench-123.scope loaded active running Benchmark solver\n"
FAILED = "acacia-old-456.scope loaded failed failed Old campaign\n"
DEAD = "LoadState=not-found\nActiveState=inactive\nSubState=dead\n"
LIVE = "LoadState=loaded\nActiveState=active\nSubState=running\n"
REAL_RUN = subprocess.run


@pytest.fixture(autouse=True)
def scope_environment(monkeypatch, isolate_campaign_scope_guards):
    monkeypatch.delenv(benchlib.CAMPAIGN_SCOPE_GUARD_ENV)
    monkeypatch.delenv("ACACIA_ALLOW_STRAY_SCOPES", raising=False)

    def unexpected_command(*args, **kwargs):
        pytest.fail(f"unexpected subprocess call: {args}")

    monkeypatch.setattr(benchlib.subprocess, "run", unexpected_command)


def systemctl(monkeypatch, listings, *, stop_code=0, states=(), reset_code=0):
    """Install a strict fake with ordered listings and scope state responses."""
    listings = iter(listings)
    states = iter(states)
    calls = []

    def run(command, **kwargs):
        assert command[:2] == ["systemctl", "--user"]
        assert 0 < kwargs["timeout"] <= 2
        assert kwargs["check"] is False
        calls.append(command)
        action = command[2]
        if action == "list-units":
            assert command[3:] == ["--type=scope", "--all", "--plain", "--no-legend", "acacia-*"]
            return subprocess.CompletedProcess(command, 0, next(listings))
        if action == "show":
            return subprocess.CompletedProcess(command, 0, next(states))
        if action == "stop":
            return subprocess.CompletedProcess(command, stop_code)
        if action == "reset-failed":
            return subprocess.CompletedProcess(command, reset_code)
        assert action == "kill"
        return subprocess.CompletedProcess(command, 0)

    monkeypatch.setattr(benchlib.subprocess, "run", run)
    return calls


@pytest.mark.parametrize(
    ("listing", "expected"),
    [
        ("", []),
        (RUNNING, [("acacia-bench-123.scope", "active", "running", "running")]),
        (FAILED, [("acacia-old-456.scope", "failed", "failed", "failed")]),
        ("broken line\nacacia-truncated.scope loaded active\n", []),
        ("other.scope loaded active running Other\n"
         "acacia-dead.scope loaded inactive dead Finished\n"
         "acacia-bad.scope loaded nonsense state Malformed\n", []),
        ("acacia-stopping.scope loaded deactivating stop-sigterm Stopping\n",
         [("acacia-stopping.scope", "deactivating", "stop-sigterm", "running")]),
        (RUNNING + FAILED + "malformed\n",
         [("acacia-bench-123.scope", "active", "running", "running"),
          ("acacia-old-456.scope", "failed", "failed", "failed")]),
    ],
)
def test_sweep_parses_unit_active_and_sub(monkeypatch, listing, expected):
    calls = systemctl(monkeypatch, [listing])
    scopes = benchlib.sweep_acacia_scopes()
    assert [(scope.unit, scope.active, scope.sub, scope.state) for scope in scopes] == expected
    assert len(calls) == 1


@pytest.mark.parametrize("failure", ["missing", "nonzero", "timeout"])
@pytest.mark.parametrize("stop", [False, True])
def test_sweep_without_a_user_manager_is_empty(monkeypatch, failure, stop):
    def run(command, **kwargs):
        if failure == "missing":
            raise FileNotFoundError("systemctl")
        if failure == "timeout":
            raise subprocess.TimeoutExpired(command, 2)
        return subprocess.CompletedProcess(command, 1, RUNNING)

    monkeypatch.setattr(benchlib.subprocess, "run", run)
    assert benchlib.sweep_acacia_scopes(stop=stop) == []


@pytest.mark.parametrize("prefix", ["syntcomp26-coverage", "bench", "acacia", ""])
def test_run_scope_rejects_unrecognizable_prefix(monkeypatch, prefix):
    monkeypatch.setattr(benchlib.subprocess, "Popen", lambda *a, **kw: pytest.fail("started a process"))
    with pytest.raises(ValueError, match="acacia-"):
        benchlib.run_systemd_scope(["solver"], 17, "8G", unit_prefix=prefix)


def test_sweep_stops_running_and_resets_failed(monkeypatch, capsys):
    calls = systemctl(monkeypatch, [RUNNING + FAILED], states=[DEAD])
    scopes = benchlib.sweep_acacia_scopes(stop=True)
    assert [scope.state for scope in scopes] == ["running", "failed"]
    assert [command[2] for command in calls] == ["list-units", "stop", "show", "reset-failed"]
    assert calls[1][-1] == "acacia-bench-123.scope"
    assert calls[-1][-1] == "acacia-old-456.scope"
    output = capsys.readouterr().err
    assert "surviving running scope acacia-bench-123.scope" in output
    assert "resetting failed scope residue acacia-old-456.scope" in output


def test_guard_refuses_contention_without_stopping_it(monkeypatch, capsys):
    calls = systemctl(monkeypatch, [RUNNING + FAILED])
    with pytest.raises(SystemExit) as error:
        with benchlib.campaign_scope_guard("campaign"):
            pytest.fail("campaign started under contention")
    assert error.value.code != 0
    assert [command[2] for command in calls] == ["list-units", "reset-failed"]
    assert benchlib.CAMPAIGN_SCOPE_GUARD_ENV not in os.environ
    output = capsys.readouterr().err
    assert "acacia-bench-123.scope" in output
    assert "refusing to start" in output
    assert "benchmarking/sweep-acacia-scopes.py --stop" in output
    assert "ACACIA_ALLOW_STRAY_SCOPES=1" in output


def test_guard_honors_deliberate_override(monkeypatch, capsys):
    monkeypatch.setenv("ACACIA_ALLOW_STRAY_SCOPES", "1")
    calls = systemctl(monkeypatch, [RUNNING, RUNNING])
    with benchlib.campaign_scope_guard("campaign"):
        assert os.environ[benchlib.CAMPAIGN_SCOPE_GUARD_ENV].startswith("campaign:")
        assert [command[2] for command in calls] == ["list-units"]
    assert benchlib.CAMPAIGN_SCOPE_GUARD_ENV not in os.environ
    assert "continuing with ACACIA_ALLOW_STRAY_SCOPES=1" in capsys.readouterr().err
    # The scope was already running when the campaign started, so it belongs to
    # somebody else.  Stopping it on the way out would kill a concurrent
    # campaign's solver -- a worse bug than the leak this guard is here for.
    assert [command[2] for command in calls] == ["list-units", "list-units"]


def test_exit_sweep_cleans_only_scopes_this_campaign_started(monkeypatch, capsys):
    monkeypatch.setenv("ACACIA_ALLOW_STRAY_SCOPES", "1")
    foreign = RUNNING
    ours = "acacia-subset-999.scope loaded active running Our solver\n"
    calls = systemctl(monkeypatch, [foreign, foreign + ours], states=[DEAD])
    with benchlib.campaign_scope_guard("campaign"):
        pass
    output = capsys.readouterr().err
    assert "acacia-subset-999.scope" in output
    assert "cleaned up 1 scope(s) that outlived this campaign" in output
    assert "acacia-bench-123.scope (active/running); stopping" not in output
    stopped = [command[3] for command in calls if command[2] == "stop"]
    assert stopped == ["acacia-subset-999.scope"]


def test_guard_resets_failed_only_residue_and_runs(monkeypatch, capsys):
    calls = systemctl(monkeypatch, [FAILED, ""])
    with benchlib.campaign_scope_guard("campaign"):
        assert calls[-1][2] == "reset-failed"
    output = capsys.readouterr().err
    assert "resetting failed scope residue" in output
    assert "refusing" not in output


def test_nested_guard_stands_down(monkeypatch):
    monkeypatch.setenv(benchlib.CAMPAIGN_SCOPE_GUARD_ENV, "parent:123")
    with benchlib.campaign_scope_guard("inner"):
        assert os.environ[benchlib.CAMPAIGN_SCOPE_GUARD_ENV] == "parent:123"
    assert os.environ[benchlib.CAMPAIGN_SCOPE_GUARD_ENV] == "parent:123"


def test_nesting_marker_is_inherited_by_subprocesses(monkeypatch):
    calls = systemctl(monkeypatch, ["", ""])
    with benchlib.campaign_scope_guard("outer"):
        result = REAL_RUN(
            [sys.executable, "-c", """
import benchlib
def unexpected_sweep(**kwargs):
    raise AssertionError('nested guard swept its parent scopes')
benchlib.sweep_acacia_scopes = unexpected_sweep
with benchlib.campaign_scope_guard('child'):
    pass
"""], cwd=BENCHMARKING, capture_output=True, text=True, check=False,
        )
        assert result.returncode == 0, result.stderr
    assert len(calls) == 2
    assert benchlib.CAMPAIGN_SCOPE_GUARD_ENV not in os.environ


@pytest.mark.parametrize("failure", [None, RuntimeError("campaign failed"), SystemExit(7)])
def test_exit_sweep_reports_survivors_even_on_exception(monkeypatch, capsys, failure):
    calls = systemctl(monkeypatch, ["", RUNNING + FAILED], states=[DEAD])
    try:
        with benchlib.campaign_scope_guard("campaign"):
            assert capsys.readouterr().err == ""
            if failure is not None:
                raise failure
    except (RuntimeError, SystemExit) as error:
        assert error is failure
    assert benchlib.CAMPAIGN_SCOPE_GUARD_ENV not in os.environ
    output = capsys.readouterr().err
    assert "campaign exit: found surviving running scope acacia-bench-123.scope" in output
    assert "campaign exit: resetting failed scope residue acacia-old-456.scope" in output
    assert [command[2] for command in calls] == ["list-units", "list-units", "stop", "show", "reset-failed"]


@pytest.mark.parametrize("stop_code", [0, 1])
@pytest.mark.parametrize("final_state", [DEAD, LIVE])
def test_stop_escalates_if_scope_survives(monkeypatch, capsys, stop_code, final_state):
    calls = systemctl(monkeypatch, [], stop_code=stop_code, states=[LIVE, final_state])
    benchlib._stop_user_scope("acacia-bench-123")
    assert [command[2] for command in calls] == ["stop", "show", "kill", "show"]
    assert calls[2] == ["systemctl", "--user", "kill", "--signal=SIGKILL", "acacia-bench-123.scope"]
    output = capsys.readouterr().err
    assert "escalating to SIGKILL" in output
    if stop_code:
        assert "stop exited 1" in output
    if final_state == LIVE:
        assert "still running after SIGKILL" in output
    else:
        assert "no longer running" in output


def test_stop_does_not_escalate_a_gone_or_failed_scope(monkeypatch, capsys):
    calls = systemctl(monkeypatch, [], stop_code=5, states=[
        DEAD, "LoadState=loaded\nActiveState=failed\nSubState=failed\n",
    ])
    benchlib._stop_user_scope("acacia-gone.scope")
    benchlib._stop_user_scope("acacia-failed.scope")
    assert [command[2] for command in calls] == ["stop", "show", "stop", "show"]
    assert capsys.readouterr().err == ""


@pytest.mark.parametrize("failure", ["missing", "timeout", "nonzero"])
def test_stop_failures_are_reported_and_nonfatal(monkeypatch, capsys, failure):
    calls = []

    def run(command, **kwargs):
        calls.append(command)
        if failure == "missing":
            raise FileNotFoundError("systemctl")
        if failure == "timeout":
            raise subprocess.TimeoutExpired(command, kwargs["timeout"])
        return subprocess.CompletedProcess(command, 1, "")

    monkeypatch.setattr(benchlib.subprocess, "run", run)
    benchlib._stop_user_scope("acacia-bench-123")
    assert [command[2] for command in calls] == ["stop", "show", "kill", "show"]
    output = capsys.readouterr().err
    assert "SIGKILL" in output
    assert "could not verify scope teardown" in output


def test_reset_failure_is_reported_without_refusing(monkeypatch, capsys):
    systemctl(monkeypatch, [FAILED, ""], reset_code=1)
    with benchlib.campaign_scope_guard("campaign"):
        pass
    assert "reset-failed acacia-old-456.scope exited 1" in capsys.readouterr().err


@pytest.mark.parametrize(("mode", "listing", "expected"), [
    ("--check", "", 0), ("--check", FAILED, 0),
    ("--check", RUNNING, 1), ("--stop", RUNNING + FAILED, 0),
])
def test_sweep_cli(monkeypatch, capsys, mode, listing, expected):
    spec = importlib.util.spec_from_file_location("sweep_acacia_scopes", BENCHMARKING / "sweep-acacia-scopes.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    systemctl(monkeypatch, [listing], states=[DEAD])
    # Explicit CLI checks still return nonzero; shell guards own the override.
    monkeypatch.setenv("ACACIA_ALLOW_STRAY_SCOPES", "1")
    assert module.main([mode]) == expected
    output = capsys.readouterr().err
    if listing:
        assert "acacia-" in output


def _sweep_cli():
    spec = importlib.util.spec_from_file_location(
        "sweep_acacia_scopes", BENCHMARKING / "sweep-acacia-scopes.py"
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


MINE = "acacia-subset-999.scope loaded active running Ours\n"


def test_sweep_cli_snapshot_records_what_was_already_running(monkeypatch, tmp_path):
    module = _sweep_cli()
    snapshot = tmp_path / "snapshot"
    systemctl(monkeypatch, [RUNNING + FAILED], states=[])
    monkeypatch.setenv("ACACIA_ALLOW_STRAY_SCOPES", "1")

    assert module.main(["--check", "--snapshot", str(snapshot)]) == 1
    # Only running scopes are inherited; failed residue is cleaned either way.
    assert snapshot.read_text().split() == ["acacia-bench-123.scope"]


def test_sweep_cli_stop_spares_the_snapshot(monkeypatch, capsys, tmp_path):
    module = _sweep_cli()
    snapshot = tmp_path / "snapshot"
    snapshot.write_text("acacia-bench-123.scope\n")
    calls = systemctl(monkeypatch, [RUNNING + MINE], states=[DEAD])

    assert module.main(["--stop", "--snapshot", str(snapshot)]) == 0

    stopped = [command[3] for command in calls if command[2] == "stop"]
    assert stopped == ["acacia-subset-999.scope"]
    assert "cleaned up 1 scope(s)" in capsys.readouterr().err


def test_sweep_cli_stop_without_a_snapshot_clears_everything(monkeypatch, tmp_path):
    # An operator running the CLI by hand is asking for the machine to be
    # cleared, not for one campaign's share of it.
    module = _sweep_cli()
    calls = systemctl(monkeypatch, [RUNNING + MINE], states=[DEAD, DEAD])

    assert module.main(["--stop"]) == 0

    stopped = [command[3] for command in calls if command[2] == "stop"]
    assert stopped == ["acacia-bench-123.scope", "acacia-subset-999.scope"]
