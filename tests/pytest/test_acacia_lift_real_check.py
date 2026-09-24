"""Proof methods follow verified capability data unless explicitly overridden."""

from __future__ import annotations

import argparse
import importlib.util
import json
import pathlib
import sys

import pytest


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

from acacia_lift.capabilities import CAPABILITIES, DATA_FILE, load_capabilities  # noqa: E402
from acacia_lift.runner import Request, _parser, select_real_check  # noqa: E402


def _request(family: str, mode: str) -> Request:
    spec = CAPABILITIES[family]
    return Request(family, 8, None, None, None, spec, spec.default_seeds, mode)


def test_source_proof_method_comes_from_capability() -> None:
    args = _parser().parse_args(["--request-mode", "source"])
    assert args.real_check is None
    for family, capability in CAPABILITIES.items():
        method, reason = select_real_check(args, _request(family, "source"))
        assert method == capability.real_check
        assert reason == f"source-verified capability {family}"
    assert {family for family, capability in CAPABILITIES.items()
            if capability.real_check == "region"} == {
                "arbiter_with_buffer", "amba_decomposed_lock"}


def test_explicit_flag_wins_and_reproducer_keeps_policy_default() -> None:
    source = _request("arbiter_with_buffer", "source")
    assert select_real_check(argparse.Namespace(real_check="policy"), source) == (
        "policy", "explicit --real-check")
    reproducer = _request("arbiter_with_buffer", "reproducer")
    assert select_real_check(argparse.Namespace(real_check=None), reproducer) == (
        "policy", "historical reproducer default")


def test_every_capability_requires_valid_real_check(tmp_path: pathlib.Path) -> None:
    payload = json.loads(DATA_FILE.read_text(encoding="utf-8"))
    path = tmp_path / "capabilities.json"
    del payload["capabilities"][0]["real_check"]
    path.write_text(json.dumps(payload), encoding="utf-8")
    with pytest.raises(ValueError, match="invalid capability fields"):
        load_capabilities(path)
    payload["capabilities"][0]["real_check"] = "unverified"
    path.write_text(json.dumps(payload), encoding="utf-8")
    with pytest.raises(ValueError, match="invalid real_check"):
        load_capabilities(path)


def test_wrapper_forwards_only_explicit_override(tmp_path: pathlib.Path) -> None:
    path = ROOT / "scripts" / "acacia-lift-portfolio.py"
    spec = importlib.util.spec_from_file_location("acacia_lift_portfolio_real_check", path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    source = tmp_path / "source.tlsf"
    args = module.build_parser().parse_args([])
    command = module.lift_command(args, source, tmp_path / "evidence.json",
                                  tmp_path / "out", 1.0)
    assert command[:3] == [sys.executable, "-m", "acacia_lift.runner"]
    assert "--real-check" not in command
    args.real_check = "region"
    command = module.lift_command(args, source, tmp_path / "evidence.json",
                                  tmp_path / "out", 1.0)
    assert command[command.index("--real-check") + 1] == "region"
