"""Source eligibility is exact, bounded, and kills timed-out tools."""

from __future__ import annotations

import csv
import json
import os
import pathlib
import subprocess
import sys
import time
from types import SimpleNamespace

import pytest


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

from acacia_lift import runner  # noqa: E402
from acacia_lift.capabilities import (  # noqa: E402
    CAPABILITIES, BindingDeclined, LoweringTools, _inferred_n, bind_source_request,
)


def test_literal_n_inference_ignores_comments_and_uncertain_expressions() -> None:
    assert _inferred_n("// n = 9;\nGLOBAL { PARAMETERS { n = 3; /* n = 8; */ } }") == 3
    assert _inferred_n("GLOBAL { PARAMETERS { n = 1 + 2; } }") is None
    assert _inferred_n("GLOBAL { PARAMETERS { n = 3; m = 4; } }") is None


def test_all_verified_and_guarded_members_keep_their_decisions() -> None:
    build = pathlib.Path("/home/gperez/GIT-repos/tlsf-tools/build-P5-9212e2b")
    tools = LoweringTools(*(build / name for name in ("tlsf2tlsf", "tlsf2ltl", "tlsfinfo")))
    if not all(path.is_file() for path in vars(tools).values()):
        pytest.skip("pinned tlsf-tools build is unavailable")
    campaign = ROOT / "benchmarking/gr1-par2-20260923/campaign"
    with (campaign / "eligibility.tsv").open(newline="", encoding="utf-8") as stream:
        members = [row for row in csv.DictReader(stream, delimiter="\t")
                   if row["decision"] == "eligible" or
                   row["decline_reason"] == "target_must_exceed_every_seed"]
    with (ROOT / "tests/suites/benchmarks/syntcomp26/tlsf-sources.tsv").open(
        newline="", encoding="utf-8"
    ) as stream:
        sources = {row["instance"]: row["tlsf"] for row in csv.DictReader(stream, delimiter="\t")}
    assert len(members) == 124
    for row in members:
        source = ROOT / "tlsf-corpus" / sources[row["id"]]
        config = SimpleNamespace(tlsf2tlsf=tools.tlsf2tlsf,
                                 tlsf2ltl=tools.tlsf2ltl, tlsfinfo=tools.tlsfinfo)
        args = SimpleNamespace(tlsf=source, semantics="exact", family=None,
                               target=None, seeds=None, eligibility_budget_seconds=1.0)
        disabled = any(
            row["id"].startswith(f"{family}_pb_")
            for family, capability in CAPABILITIES.items() if not capability.route_enabled
        )
        if disabled:
            with pytest.raises(runner.PipelineFailure) as error:
                runner._resolve_source_request(args, config, runner.Deadline.start(10))
            assert error.value.reason == "capability_route_disabled", row["id"]
        elif row["decision"] == "eligible":
            request = runner._resolve_source_request(args, config, runner.Deadline.start(10))
            assert request.family == row["capability"], row["id"]
        else:
            with pytest.raises(runner.PipelineFailure) as error:
                runner._resolve_source_request(args, config, runner.Deadline.start(10))
            assert error.value.reason == "target_must_exceed_every_seed", row["id"]


def test_slow_lowering_is_cut_off_and_its_child_stops(tmp_path: pathlib.Path) -> None:
    heartbeat = tmp_path / "heartbeat"
    child_pid = tmp_path / "child-pid"
    info = tmp_path / "tlsfinfo"
    info.write_text(
        "#!/usr/bin/env python3\n"
        "import sys\n"
        "print({'--parameters':'n', '--semantics':'Mealy', '--target':'Mealy',"
        " '--expanded-ins':'r_0,r_1', '--expanded-outs':'g_0,g_1'}[sys.argv[1]])\n",
        encoding="utf-8",
    )
    info.chmod(0o755)
    slow = tmp_path / "tlsf2tlsf"
    slow.write_text(
        "#!/usr/bin/env python3\n"
        "import pathlib, subprocess, sys, time\n"
        f"heartbeat = {str(heartbeat)!r}\n"
        f"pid_file = {str(child_pid)!r}\n"
        "child = subprocess.Popen([sys.executable, '-c',"
        " 'import pathlib,time; p=pathlib.Path(' + repr(heartbeat) + ');'"
        " '[(p.open(\"ab\").write(b\"x\"),time.sleep(.01)) for _ in range(3000)]'])\n"
        "pathlib.Path(pid_file).write_text(str(child.pid))\n"
        "time.sleep(30)\n",
        encoding="utf-8",
    )
    slow.chmod(0o755)
    source = tmp_path / "source.tlsf"
    source.write_text("n = 2;", encoding="utf-8")
    started = time.monotonic()
    with pytest.raises(BindingDeclined) as error:
        bind_source_request(source, LoweringTools(slow, slow, info), "exact",
                            eligibility_budget_seconds=0.35)
    assert error.value.code == "eligibility_budget_exhausted"
    assert time.monotonic() - started < 0.8
    assert child_pid.exists()
    pid = int(child_pid.read_text())
    try:
        before = heartbeat.stat().st_size if heartbeat.exists() else 0
        time.sleep(0.08)
        after = heartbeat.stat().st_size if heartbeat.exists() else 0
        assert before == after
        proc = pathlib.Path(f"/proc/{pid}/stat")
        assert not proc.exists() or proc.read_text().split()[2] == "Z"
    finally:
        try:
            os.kill(pid, 9)
        except ProcessLookupError:
            pass


def test_runner_evidence_names_eligibility_budget_decline(tmp_path: pathlib.Path) -> None:
    build = pathlib.Path("/home/gperez/GIT-repos/tlsf-tools/build-P5-9212e2b")
    if not (build / "tlsfinfo").is_file():
        pytest.skip("pinned tlsf-tools build is unavailable")
    evidence_path = tmp_path / "evidence.json"
    source = ROOT / "scripts/acacia_lift/data/templates/tlsf/arbiters_zoo/parametric/arbiter.tlsf"
    result = subprocess.run(
        [sys.executable, "-m", "acacia_lift.runner", "--request-mode", "source",
         "-T", str(source), "--tlsf-tools-build", str(build), "--budget", "5",
         "--eligibility-budget-seconds", "0.000001", "--output-dir", str(tmp_path),
         "--evidence-out", str(evidence_path)],
        env={**os.environ, "PYTHONPATH": str(ROOT / "scripts")},
        capture_output=True, text=True, timeout=5, check=False,
    )
    assert result.returncode == 2
    evidence = json.loads(evidence_path.read_text())
    assert evidence["eligibility_budget_s"] == 0.000001
    assert evidence["result"]["reason"] == "eligibility_budget_exhausted"
