"""The Docker wrapper must forward the runtime memory policy verbatim."""
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


@pytest.mark.parametrize("policy", ["equal", "alone", "invalid"])
@pytest.mark.parametrize("attached", [False, True])
@pytest.mark.parametrize("tlsf", [False, True])
def test_wrapper_forwards_obligation_memory_policy(tmp_path, policy, attached, tlsf):
    (tmp_path / "scripts").mkdir()
    (tmp_path / "config").mkdir()
    launcher = tmp_path / "scripts/acacia-bonsai.sh"
    shutil.copy2(ROOT / "scripts/acacia-bonsai.sh", launcher)
    names = (ROOT / "config/docker-default.list").read_text()
    (tmp_path / "config/docker-default.list").write_text(names)
    binary = tmp_path / f"build_{names.splitlines()[0]}" / "src/acacia-bonsai"
    binary.parent.mkdir(parents=True)
    binary.write_text("#!/bin/sh\nprintf '%s\\n' \"$@\"\n")
    binary.chmod(0o755)
    flags = (["--obligation-memory-share=" + policy] if attached
             else ["--obligation-memory-share", policy])
    args = [*flags, "-f", "G o"]
    result = subprocess.run(["bash", str(launcher), *(["--tlsf"] if tlsf else []), *args],
                            text=True, capture_output=True, check=True)
    assert result.stdout.splitlines() == args + (["-T", "/dev/stdin"] if tlsf else [])
