"""Regression tests for the wheel/CI configuration and compiler ISA guard."""

import importlib.util
import json
from pathlib import Path
import shlex
import subprocess
import sys

import pytest


ROOT = Path(__file__).resolve().parents[2]
CHECK = ROOT / "tests/check-python-preset.py"


@pytest.fixture
def guard():
    spec = importlib.util.spec_from_file_location("python_preset_guard", CHECK)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def write_commands(path, arguments, form):
    command = {"file": "source.c", "directory": str(path.parent)}
    command[form] = shlex.join(arguments) if form == "command" else arguments
    path.write_text(json.dumps([command]))


@pytest.mark.parametrize("form", ["command", "arguments"])
@pytest.mark.parametrize("flag", [
    "-march=native", "-march=x86-64-v2", "-march=armv8-a+simd",
    "-mcpu=native", "-mcpu=apple-m1", "-mtune=native",
    "-mavx", "-mavx2", "-mavxvnni", "-mfma", "-mfma4",
    "-msse4", "-msse4.1", "-msse4.2", "-mssse3", "-mbmi", "-mbmi2",
    "-mpopcnt", "-mlzcnt", "-mf16c", "-mavx512f", "-mavx512bw",
    "-mavx512vpopcntdq", "-maes", "-msha", "-mfpu=neon",
])
def test_portable_commands_reject_target_isa_flags(guard, tmp_path, form, flag):
    path = tmp_path / "compile_commands.json"
    write_commands(path, ["cc", "-O3", flag, "-c", "source.c"], form)
    with pytest.raises(SystemExit, match="target ISA flags") as error:
        guard.check_portable_commands(path)
    assert flag in str(error.value)


@pytest.mark.parametrize("form", ["command", "arguments"])
def test_portable_commands_allow_baseline_and_deployment_flags(guard, tmp_path, form):
    path = tmp_path / "compile_commands.json"
    write_commands(path, [
        "c++", "-O3", "-fPIC", "-m64", "-mtune=generic", "-mmacosx-version-min=15.0",
        '-DDESCRIPTION=literal -march=native text', "-I/path/with-mavx2", "-c", "source.cc",
    ], form)
    guard.check_portable_commands(path)


@pytest.fixture(scope="module")
def preset_options():
    helper = [sys.executable, str(ROOT / "scripts/acacia-config.py")]
    first = subprocess.check_output(
        [*helper, "list-group", "docker_default"], text=True,
    ).splitlines()[0]
    arguments = shlex.split(subprocess.check_output(
        [*helper, "meson-args", first], text=True,
    ))
    options = dict(argument[2:].split("=", 1) for argument in arguments)
    options.update(build_python=True, acacia_compiler_profile="debug")
    options["tlsf-tools:cpu"] = "baseline"
    return options


@pytest.fixture
def python_build(tmp_path, preset_options):
    info = tmp_path / "meson-info"
    info.mkdir()
    (info / "intro-buildoptions.json").write_text(json.dumps([
        {"name": name, "value": value} for name, value in preset_options.items()
    ]))
    write_commands(tmp_path / "compile_commands.json", ["cc", "-c", "source.c"], "command")
    return tmp_path


@pytest.mark.parametrize("portable", [False, True])
def test_python_build_accepts_baseline_configuration(python_build, portable):
    result = subprocess.run(
        [sys.executable, str(CHECK), str(python_build), *(["--portable"] if portable else [])],
        text=True, capture_output=True,
    )
    assert result.returncode == 0, result.stdout + result.stderr


@pytest.mark.parametrize("portable", [False, True])
@pytest.mark.parametrize("cpu", ["avx2", "neon", None])
def test_python_build_rejects_nonbaseline_tlsf_cpu(python_build, portable, cpu):
    path = python_build / "meson-info/intro-buildoptions.json"
    options = json.loads(path.read_text())
    options = [option for option in options if option["name"] != "tlsf-tools:cpu"]
    if cpu is not None:
        options.append({"name": "tlsf-tools:cpu", "value": cpu})
    path.write_text(json.dumps(options))
    if cpu == "avx2":
        write_commands(python_build / "compile_commands.json",
                       ["cc", "-mavx2", "-mfma", "-c", "source.c"], "command")
    result = subprocess.run(
        [sys.executable, str(CHECK), str(python_build), *(["--portable"] if portable else [])],
        text=True, capture_output=True,
    )
    assert result.returncode != 0
    assert "tlsf-tools:cpu: expected 'baseline'" in result.stderr


def test_python_build_rejects_isa_flags_with_baseline_tlsf_cpu(python_build):
    write_commands(python_build / "compile_commands.json",
                   ["cc", "-mavx2", "-mfma", "-c", "source.c"], "arguments")
    result = subprocess.run(
        [sys.executable, str(CHECK), str(python_build), "--portable"],
        text=True, capture_output=True,
    )
    assert result.returncode != 0
    assert "target ISA flags" in result.stderr
    assert "-mavx2" in result.stderr
    assert "-mfma" in result.stderr
