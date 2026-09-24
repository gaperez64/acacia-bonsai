"""The exact route needs an independent checker result before deciding."""

from __future__ import annotations

import pathlib
import subprocess
import sys
import time
from unittest import mock

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

from acacia_lift import direct  # noqa: E402
from acacia_lift.tools import configuration_defaults  # noqa: E402


def test_checker_zero_without_verified_payload_declines(tmp_path: pathlib.Path) -> None:
    config = configuration_defaults()
    if not config.solver.is_file() or not config.monitor.is_file():
        pytest.skip("tlsf-tools build unavailable")
    source = tmp_path / "input.tlsf"
    source.write_text('INFO { TITLE: "case" SEMANTICS: Mealy TARGET: Mealy }\n'
                      'MAIN { INPUTS { a; } OUTPUTS { b; } GUARANTEES { G b; } }\n')
    original = direct.run_command

    def checker_without_proof(command: list[str], deadline: float, stage: str,
                              **kwargs: object) -> subprocess.CompletedProcess[str]:
        if stage == "target_check":
            json_out = pathlib.Path(command[command.index("--json-out") + 1])
            json_out.write_text('{"verdict":"REFUTED","methods":{}}')
            return subprocess.CompletedProcess(command, 0, "", "")
        return original(command, deadline, stage, **kwargs)

    with mock.patch.object(direct, "run_command", side_effect=checker_without_proof):
        with pytest.raises(direct.Decline, match="certificate_not_verified"):
            direct.run_exact_direct(source, tmp_path / "out", config,
                                    time.monotonic() + 3, 1.0)
