"""The frozen source route switch declines early and preserves B fallback."""

from __future__ import annotations

import json
import os
import pathlib
import subprocess
import sys
from types import SimpleNamespace
from unittest import mock

import pytest


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

from acacia_lift import capabilities as binding, runner  # noqa: E402
from acacia_lift.capabilities import CAPABILITIES, DATA_FILE, load_capabilities  # noqa: E402


DISABLED = {
    "collector_v1", "abcg_arbiter", "simple_arbiter_with_hints",
    "amba_case_study_unreal",
}
BUILD = pathlib.Path("/home/gperez/GIT-repos/tlsf-tools/build-P5-9212e2b")


def _marker_script(path: pathlib.Path, marker: pathlib.Path) -> None:
    path.write_text(
        "#!/usr/bin/env python3\n"
        "import pathlib\n"
        f"pathlib.Path({str(marker)!r}).write_text('started')\n",
        encoding="utf-8",
    )
    path.chmod(0o755)


def test_route_switch_and_schema(tmp_path: pathlib.Path) -> None:
    assert len(CAPABILITIES) == 14
    assert {family for family, spec in CAPABILITIES.items()
            if not spec.route_enabled} == DISABLED
    payload = json.loads(DATA_FILE.read_text(encoding="utf-8"))
    path = tmp_path / "capabilities.json"
    del payload["capabilities"][0]["route_enabled"]
    path.write_text(json.dumps(payload), encoding="utf-8")
    with pytest.raises(ValueError, match="invalid capability fields"):
        load_capabilities(path)
    for value in (None, 0, 1, "false"):
        payload["capabilities"][0]["route_enabled"] = value
        path.write_text(json.dumps(payload), encoding="utf-8")
        with pytest.raises(ValueError, match="invalid route_enabled"):
            load_capabilities(path)


@pytest.mark.parametrize("family", sorted(DISABLED))
def test_disabled_source_declines_before_lowering_or_solver(
    tmp_path: pathlib.Path, family: str,
) -> None:
    if not (BUILD / "tlsfinfo").is_file():
        pytest.skip("pinned tlsf-tools build is unavailable")
    evidence_path = tmp_path / "evidence.json"
    generalizer = tmp_path / "generalizer"
    solver = tmp_path / "solver"
    generalizer_marker = tmp_path / "generalizer-started"
    solver_marker = tmp_path / "solver-started"
    _marker_script(generalizer, generalizer_marker)
    _marker_script(solver, solver_marker)
    result = subprocess.run(
        [sys.executable, "-m", "acacia_lift.runner", "--request-mode", "source",
         "-T", str(CAPABILITIES[family].source_path),
         "--tlsf-tools-build", str(BUILD), "--budget", "5",
         "--generalizer", str(generalizer), "--solver", str(solver),
         "--output-dir", str(tmp_path), "--evidence-out", str(evidence_path)],
        env={**os.environ, "PYTHONPATH": str(ROOT / "scripts")},
        capture_output=True, text=True, timeout=5, check=False,
    )
    assert result.returncode == 2, result.stderr
    evidence = json.loads(evidence_path.read_text(encoding="utf-8"))
    assert evidence["result"]["reason"] == "capability_route_disabled"
    assert evidence["source_binding"]["capability"]["family"] == family
    assert evidence["source_binding"]["capability"]["route_enabled"] is False
    expected_match = (
        "content-verified-template-instantiation"
        if family == "simple_arbiter_with_hints" else "structural-prefilter"
    )
    assert evidence["source_binding"]["match"]["how"] == expected_match
    assert not evidence["stages"]
    assert not generalizer_marker.exists()
    assert not solver_marker.exists()


def test_unique_structural_matches_decline_without_lowering() -> None:
    if not (BUILD / "tlsfinfo").is_file():
        pytest.skip("pinned tlsf-tools build is unavailable")
    tools = binding.LoweringTools(*(BUILD / name for name in
                                    ("tlsf2tlsf", "tlsf2ltl", "tlsfinfo")))
    original = binding._run_tool
    for family in DISABLED - {"simple_arbiter_with_hints"}:
        calls: list[str] = []

        def recorded(command, *args, **kwargs):
            calls.append(pathlib.Path(command[0]).name)
            return original(command, *args, **kwargs)

        with mock.patch.object(binding, "_run_tool", recorded):
            with pytest.raises(binding.BindingDeclined, match="capability_route_disabled"):
                binding.bind_source_request(CAPABILITIES[family].source_path,
                                            tools, "exact")
        assert calls
        assert set(calls) == {"tlsfinfo"}


def test_enabled_capabilities_and_historical_reproducer_still_bind() -> None:
    if not (BUILD / "tlsfinfo").is_file():
        pytest.skip("pinned tlsf-tools build is unavailable")
    tools = binding.LoweringTools(*(BUILD / name for name in
                                    ("tlsf2tlsf", "tlsf2ltl", "tlsfinfo")))
    for family, capability in CAPABILITIES.items():
        if capability.route_enabled:
            request = binding.bind_source_request(capability.source_path,
                                                  tools, "exact")
            assert request.family == family
    args = SimpleNamespace(
        tlsf=None, family="collector_v1", target=11, seeds=None, semantics="exact",
        instances=runner.M0_INSTANCES, census=runner.M0_CENSUS,
    )
    historical = runner._resolve_reproducer_request(args)
    assert historical.family == "collector_v1"
    assert historical.request_mode == "reproducer"


def test_wrapper_records_decline_and_execs_b(tmp_path: pathlib.Path) -> None:
    if not (BUILD / "tlsfinfo").is_file():
        pytest.skip("pinned tlsf-tools build is unavailable")
    fallback = tmp_path / "fallback"
    marker = tmp_path / "fallback-started"
    _marker_script(fallback, marker)
    route = tmp_path / "route.json"
    source = CAPABILITIES["collector_v1"].source_path
    result = subprocess.run(
        [sys.executable, str(ROOT / "scripts/acacia-lift-portfolio.py"),
         "--cap", "10", "--tlsf-tools-build", str(BUILD),
         "--route-record", str(route), "--", str(fallback), "-T", str(source)],
        env={**os.environ, "PYTHONPATH": str(ROOT / "scripts")},
        capture_output=True, text=True, timeout=5, check=False,
    )
    assert result.returncode == 0, result.stderr
    assert marker.read_text(encoding="utf-8") == "started"
    record = json.loads(route.read_text(encoding="utf-8"))
    assert record["binding_reason"] == "capability_route_disabled"
    assert record["capability"]["family"] == "collector_v1"
    assert record["winner"] == "fallback-pending"
    evidence = json.loads(pathlib.Path(record["evidence_path"]).read_text(encoding="utf-8"))
    assert evidence["result"]["reason"] == "capability_route_disabled"
