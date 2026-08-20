from __future__ import annotations

import os
import pathlib
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "benchmarking"))
from suite_paths import resolve_instance


def run_fake(tmp_path, script, *, resource_unknown=False, cgroup_unavailable=False):
    fake_acacia = tmp_path / "fake-acacia"
    fake_acacia.write_text(f"#!/bin/sh\n{script}\n")
    fake_acacia.chmod(0o755)

    template = (ROOT / "tests/check-real-correct.sh.in").read_text()
    wrapper = tmp_path / "check-real-correct.sh"
    wrapper.write_text(
        template.replace("@ACABONSAI@", str(fake_acacia)).replace(
            "@LTLSYNT@", "/does/not/exist/ltlsynt"
        )
    )
    wrapper.chmod(0o755)

    env = None
    if resource_unknown or cgroup_unavailable:
        env = os.environ.copy()
    if resource_unknown:
        env["ACACIA_TEST_RESOURCE_UNKNOWN"] = "1"
    if cgroup_unavailable:
        fake_systemctl = tmp_path / "systemctl"
        fake_systemctl.write_text("#!/bin/sh\nexit 1\n")
        fake_systemctl.chmod(0o755)
        env["PATH"] = f"{tmp_path}:{env['PATH']}"
        env["ACACIA_TEST_CGROUP"] = "1"
        env["ACACIA_TEST_CGROUP_MEMORY_MAX"] = "8G"
    return subprocess.run(
        [
            "/bin/zsh",
            "-f",
            str(wrapper),
            "-a",
            "-F",
            str(
                resolve_instance(
                    ROOT / "tests/suites/benchmarks/syntcomp24/sources.tsv",
                    "lift4.ltl",
                )
            ),
        ],
        capture_output=True,
        text=True,
        check=False,
        env=env,
    )


def test_unlabelled_unknown_is_rejected(tmp_path):
    """A K-bound-style exit 2 must not pass an unlabelled benchmark."""
    result = run_fake(tmp_path, "printf 'UNKNOWN\\n'\nexit 2")

    assert result.returncode == 3
    assert "FAILED: NO VERDICT: UNKNOWN (return 2)" in result.stdout
    assert "PASS." not in result.stdout


def test_unlabelled_error_is_distinguished(tmp_path):
    result = run_fake(tmp_path, "printf 'ERROR\\n'\nexit 3")

    assert result.returncode == 3
    assert "FAILED: ERROR: RETURNED 3" in result.stdout
    assert "PASS." not in result.stdout


def test_resource_kill_is_mapped_to_distinguished_unknown(tmp_path):
    result = run_fake(tmp_path, "exit 137", resource_unknown=True)

    assert result.returncode == 3
    assert "RESOURCE LIMIT: cgroup killed the solver process" in result.stderr
    assert "FAILED: NO VERDICT: UNKNOWN (return 2)" in result.stdout
    assert "PASS." not in result.stdout


def test_cgroup_launcher_failure_cannot_pass_as_unrealizable(tmp_path):
    result = run_fake(
        tmp_path,
        "printf 'UNREALIZABLE\\n'\nexit 1",
        cgroup_unavailable=True,
    )

    assert result.returncode == 3
    assert "user systemd manager is unavailable" in result.stderr
    assert "FAILED: ERROR: RETURNED 127" in result.stdout
    assert "PASS." not in result.stdout
