from __future__ import annotations

import os
import pathlib
import shlex
import subprocess
import sys

import pytest


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "benchmarking"))
from suite_paths import resolve_instance  # noqa: E402


def run_fake(
    tmp_path,
    script,
    *,
    instance="lift4.ltl",
    suite="syntcomp24",
    resource_unknown=False,
    cgroup_unavailable=False,
    selectors=("-a",),
    extra_opts=(),
):
    fake_acacia = tmp_path / "fake-acacia"
    fake_acacia.write_text(f"#!/bin/sh\n{script}\n")
    fake_acacia.chmod(0o755)

    template = (ROOT / "tests/check-real-correct.sh.in").read_text()
    wrapper = tmp_path / "check-real-correct.sh"
    wrapper.write_text(
        template.replace("@ACABONSAI@", str(fake_acacia)).replace(
            "@LTLSYNT@", str(fake_acacia) if "-l" in selectors else "/does/not/exist/ltlsynt"
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
            *selectors,
            "-F",
            str(
                resolve_instance(
                    ROOT / f"tests/suites/benchmarks/{suite}/sources.tsv",
                    instance,
                )
            ),
            *extra_opts,
        ],
        capture_output=True,
        text=True,
        check=False,
        env=env,
    )


@pytest.mark.parametrize("selector", ["-s", "-+", "-o", "-e"])
def test_obsolete_options_are_rejected_before_launch(tmp_path, selector):
    launched = tmp_path / "launched"
    result = run_fake(
        tmp_path,
        f"touch {shlex.quote(str(launched))}\nexit 0",
        selectors=("-a", selector),
    )

    assert result.returncode != 0
    assert "Usage:" in result.stdout
    assert "PASS." not in result.stdout
    assert not launched.exists()


@pytest.mark.parametrize("selector", ["-a", "-l"])
def test_supported_backend_launches_once_and_preserves_extra_argv(tmp_path, selector):
    argv = tmp_path / "argv"
    result = run_fake(
        tmp_path,
        f"printf '%s\\0' \"$@\" >> {shlex.quote(str(argv))}\nexit 1",
        selectors=(selector,),
        extra_opts=("--", "--example", "one argument"),
    )

    source = resolve_instance(
        ROOT / "tests/suites/benchmarks/syntcomp24/sources.tsv", "lift4.ltl"
    )
    part = source.with_suffix(".part").read_text().splitlines()
    inputs = ",".join(next(line for line in part if line.startswith(".inputs")).split()[1:])
    outputs = ",".join(next(line for line in part if line.startswith(".outputs")).split()[1:])
    expected = (
        ["-F", str(source), "-i", inputs, "-o", outputs]
        if selector == "-a"
        else ["--realizability", "-F", str(source), f"--ins={inputs}", f"--outs={outputs}"]
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert argv.read_bytes().split(b"\0") == [
        item.encode() for item in [*expected, "--example", "one argument", ""]
    ]


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


def test_declared_empty_outputs_are_passed_as_an_empty_argument(tmp_path):
    result = run_fake(
        tmp_path,
        """\
while [ "$#" -gt 0 ]; do
    if [ "$1" = -o ]; then
        shift
        if [ "$1" = "" ]; then
            printf 'EMPTY OUTPUTS PRESERVED\\n'
            exit 1
        fi
        printf 'unexpected output: <%s>\\n' "$1"
        exit 9
    fi
    shift
done
exit 9""",
        instance="CheckRoom_06e9cad4.ltl",
        suite="syntcomp24",
    )

    assert result.returncode == 0
    assert "EMPTY OUTPUTS PRESERVED" in result.stdout
    assert "PASS." in result.stdout
