"""Build reuse must reject stale requests before trusting a compiled marker."""

import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

import pytest


ROOT = Path(__file__).resolve().parents[2]
COMPILER_SEARCH_ENV = (
    "CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH", "OBJC_INCLUDE_PATH",
    "OBJCPLUS_INCLUDE_PATH", "LIBRARY_PATH", "COMPILER_PATH", "GCC_EXEC_PREFIX",
    "SDKROOT", "DEVELOPER_DIR", "INCLUDE", "LIB", "LIBPATH",
    "CCC_OVERRIDE_OPTIONS", "CCC_ADD_ARGS",
)


def load_build_helper():
    spec = importlib.util.spec_from_file_location("acacia_build", ROOT / "scripts/acacia-build.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_shared_profiles_keep_compiler_and_assertion_settings():
    profiles = load_build_helper().PROFILES
    assert profiles["debug"] == []
    assert profiles["normal"] == [
        "--buildtype=release", "-Doptimization=3", "-Db_lto=true", "-Ddebug=false",
        "-Db_ndebug=true", "-Dacacia_compiler_profile=release",
    ]
    assert "-Db_ndebug=true" not in profiles["checked"]
    assert "-Dacacia_compiler_profile=debug" in profiles["checked"]
    assert "-Doptimization=0" in profiles["lowmem"]
    assert "-Db_lto=false" in profiles["lowmem"]


def test_identity_uses_the_wrappers_config_helper_override(tmp_path, monkeypatch):
    helper = tmp_path / "custom registry.py"
    helper.write_text('print(\'{"_preset": "fixture", "custom": true}\')\n')
    monkeypatch.setenv("ACACIA_CONFIG_HELPER", str(helper))
    info = tmp_path / "meson-info"
    info.mkdir()
    (info / "intro-compilers.json").write_text('{"host": {}}')
    (info / "intro-buildoptions.json").write_text('[]')
    identity = load_build_helper().build_identity("fixture", "normal", tmp_path)
    assert identity["acacia"] == {"_preset": "fixture", "custom": True}


@pytest.fixture
def driver(tmp_path):
    if not shutil.which("zsh"):
        pytest.skip("zsh required for the build wrapper")
    bin_dir = tmp_path / "bin"
    bin_dir.mkdir()
    linker = bin_dir / "ld"
    linker.write_text("#!/bin/sh\necho linker-v1\n")
    linker.chmod(0o755)
    compiler = bin_dir / "compiler"
    compiler.write_text('#!/bin/sh\nif [ "$1" = "-print-prog-name=ld" ]; then\n'
                        f'  echo "{linker}"\nelse\n  echo compiler-v1\nfi\n')
    compiler.chmod(0o755)
    meson = bin_dir / "meson"
    meson.write_text(f"#!{sys.executable}\n" + '''
import json
import os
from pathlib import Path
import sys

with Path(os.environ["CALLS"]).open("a") as calls:
    calls.write(json.dumps(sys.argv[1:]) + "\\n")
if sys.argv[1] == "setup":
    build = Path(sys.argv[2])
    info = build / "meson-info"
    info.mkdir(parents=True)
    (info / "intro-buildoptions.json").write_text(json.dumps([
        {"name": "build_tests", "value": True}]))
    (info / "intro-compilers.json").write_text(json.dumps({"host": {
        "cpp": {"id": "gcc", "exelist": [os.environ["COMPILER"]],
                "linker_exelist": [os.environ["COMPILER"]], "version": "1"}}}))
    binary = build / "src/acacia-bonsai"
    binary.parent.mkdir()
    binary.write_text("#!/bin/sh\\nexit 0\\n")
    binary.chmod(0o755)
''')
    meson.chmod(0o755)
    native = tmp_path / "native file.ini"
    native.write_text("[built-in options]\nc_args = ['-DFIRST']\n")
    names = subprocess.check_output(
        [sys.executable, str(ROOT / "scripts/acacia-config.py"), "list-presets", "--unique"],
        text=True).splitlines()
    name = names[0]
    env = dict(os.environ, PATH=f"{bin_dir}:{os.environ['PATH']}",
               BENCHMARK_BUILD_ROOT=str(tmp_path / "builds"),
               BENCHMARK_LOG_ROOT=str(tmp_path / "logs"),
               BENCHMARK_COMPILE_PROFILE="normal", BENCHMARK_COMPILE_JOBS="1",
               CALLS=str(tmp_path / "calls"), COMPILER=str(compiler))

    def run(*extra):
        return subprocess.run(
            ["zsh", str(ROOT / "self-benchmark.sh"), "-R", "-c", name,
             "-n", str(native), *extra], cwd=ROOT, env=env, capture_output=True, text=True)

    first = run()
    assert first.returncode == 0, first.stdout + first.stderr
    build = tmp_path / "builds" / f"build_{name}"
    assert (build / "compiled").exists()
    return run, env, native, compiler, build, tmp_path / "calls"


def test_matching_build_is_reused(driver):
    run, _, _, _, _, calls = driver
    before = calls.read_bytes()
    result = run()
    assert result.returncode == 0, result.stdout + result.stderr
    assert calls.read_bytes() == before


@pytest.mark.parametrize("variable", COMPILER_SEARCH_ENV)
@pytest.mark.parametrize("skip_setup", [False, True])
def test_compiler_search_environment_rejects_compiled_marker(driver, variable, skip_setup):
    run, env, _, _, build, calls = driver
    env[variable] = "/changed/compiler/search/path"
    before = calls.read_bytes()
    result = run(*(["-B"] if skip_setup else []))
    assert result.returncode != 0, result.stdout + result.stderr
    assert "stale" in result.stdout
    assert (build / "compiled").exists()
    assert calls.read_bytes() == before


@pytest.mark.parametrize("skip_setup", [False, True])
def test_effective_linker_change_rejects_compiled_marker(driver, skip_setup):
    run, _, native, compiler, build, calls = driver
    native_before, compiler_before = native.read_bytes(), compiler.read_bytes()
    linker = compiler.parent / "ld"
    linker.write_text("#!/bin/sh\necho linker-v2\n")
    before = calls.read_bytes()
    result = run(*(["-B"] if skip_setup else []))
    assert result.returncode != 0, result.stdout + result.stderr
    assert "stale" in result.stdout
    assert (build / "compiled").exists()
    assert calls.read_bytes() == before
    assert native.read_bytes() == native_before
    assert compiler.read_bytes() == compiler_before


@pytest.mark.parametrize("driver_name", ["g++", "clang++"])
@pytest.mark.parametrize("relative_prefix", [False, True])
@pytest.mark.parametrize("selection", ["default", "fuse", "native-override"])
def test_linker_bytes_behind_unchanged_driver_prefix(
        tmp_path, driver_name, relative_prefix, selection):
    driver_path = shutil.which(driver_name)
    if not driver_path:
        pytest.skip(f"{driver_name} required for linker selection regression")
    bin_dir = tmp_path / "linker bin"
    bin_dir.mkdir()
    linker = bin_dir / ("ld" if selection == "default" else "ld.bfd")
    linker.write_text("#!/bin/sh\necho linker-v1\n")
    linker.chmod(0o755)
    command = [driver_path, f"-B{bin_dir.relative_to(tmp_path) if relative_prefix else bin_dir}/"]
    info = tmp_path / "meson-info"
    info.mkdir()
    (info / "intro-compilers.json").write_text(json.dumps({"host": {"cpp": {
        "id": "gcc" if driver_name == "g++" else "clang",
        "exelist": command, "linker_exelist": command, "linker_id": "ld.bfd"}}}))
    (info / "intro-buildoptions.json").write_text('[]')
    if selection != "default":
        (info / "intro-targets.json").write_text(json.dumps([{"target_sources": [{
            "linker": command, "parameters": [
                "-fuse-ld=bfd" if selection == "fuse" else f"-fuse-ld={linker}"]}]}]))
    native = tmp_path / "native.ini"
    native.write_text(f"[binaries]\ncpp = {command!r}\n" + (
        f"cpp_ld = '{linker}'\n" if selection == "native-override" else ""))
    module = load_build_helper()
    first = module.build_identity("base", "debug", tmp_path, native)
    assert module.build_identity("base", "debug", tmp_path, native) == first
    linker.write_text("#!/bin/sh\necho linker-v2\n")
    second = module.build_identity("base", "debug", tmp_path, native)
    assert first["native_file"] == second["native_file"]
    assert first["toolchains"]["host"]["cpp"]["linker_exelist"] == second[
        "toolchains"]["host"]["cpp"]["linker_exelist"]
    assert first != second
    assert second["toolchains"]["host"]["cpp"]["effective_linkers"][0]["path"] == str(linker)


def test_effective_linker_uses_build_directory_for_relative_path(tmp_path, monkeypatch):
    driver_path = shutil.which("g++")
    if not driver_path:
        pytest.skip("g++ required for PATH linker selection regression")
    bin_dir = tmp_path / "bin"
    bin_dir.mkdir()
    linker = bin_dir / "ld"
    linker.write_text("#!/bin/sh\necho linker-v1\n")
    linker.chmod(0o755)
    monkeypatch.setenv("PATH", f"bin:{os.environ['PATH']}")
    compiler = {"exelist": [driver_path], "linker_exelist": [driver_path]}
    module = load_build_helper()
    first = module.effective_linker_identity(compiler, tmp_path)
    assert first["path"] == str(linker)
    linker.write_text("#!/bin/sh\necho linker-v2\n")
    assert module.effective_linker_identity(compiler, tmp_path) != first


@pytest.mark.parametrize("mutation", ["profile", "native", "compiler", "environment",
                                     "options", "legacy", "missing"])
@pytest.mark.parametrize("skip_setup", [False, True])
def test_stale_build_is_rejected_even_with_compiled_marker(driver, mutation, skip_setup):
    run, env, native, compiler, build, calls = driver
    if mutation == "profile":
        env["BENCHMARK_COMPILE_PROFILE"] = "checked"
    elif mutation == "native":
        native.write_text("[built-in options]\nc_args = ['-DSECOND']\n")
    elif mutation == "compiler":
        compiler.write_text(compiler.read_text().replace("compiler-v1", "compiler-v2"))
    elif mutation == "environment":
        env["CXXFLAGS"] = "-DCHANGED"
    elif mutation == "options":
        (build / "meson-info/intro-buildoptions.json").write_text(
            '[{"name": "build_tests", "value": false}]')
    elif mutation == "legacy":
        (build / ".acacia-config.json").write_text('{"_preset": "old"}\n')
    else:
        (build / ".acacia-config.json").unlink()
    before = calls.read_bytes()
    result = run(*(["-B"] if skip_setup else []))
    assert result.returncode != 0, result.stdout + result.stderr
    assert "stale" in result.stdout or "without" in result.stdout
    assert calls.read_bytes() == before
