from __future__ import annotations

import importlib.util
import json
import pathlib
import sys

import pytest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "benchmarking" / "benchlib.py"
OPTION = "acacia_tlsf_corpus_dir"


@pytest.fixture
def benchlib(tmp_path, monkeypatch):
    spec = importlib.util.spec_from_file_location("benchlib_tlsf_tests", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    monkeypatch.setitem(sys.modules, spec.name, module)
    spec.loader.exec_module(module)
    monkeypatch.setattr(module, "ROOT", tmp_path)
    return module


@pytest.fixture
def build_dir(tmp_path):
    build = tmp_path / "build"
    (build / "meson-info").mkdir(parents=True)
    return build


def made(path: pathlib.Path) -> pathlib.Path:
    """A candidate directory only counts if it is really there."""
    path.mkdir(parents=True, exist_ok=True)
    return path


def write_option(build_dir, value):
    (build_dir / "meson-info" / "intro-buildoptions.json").write_text(
        json.dumps([{"name": OPTION, "value": value}]), encoding="utf-8"
    )


@pytest.fixture
def recorded_corpus(benchlib, tmp_path):
    corpus = tmp_path / "recorded corpus"
    corpus.mkdir()
    (corpus / ".acacia-tlsf-corpus").write_text('{"entries": 1}\n')
    (benchlib.ROOT / ".acacia-tlsf-corpus-path").write_text(str(corpus) + "\n")
    return corpus


def test_explicit_beats_env(benchlib, build_dir, recorded_corpus, tmp_path):
    write_option(build_dir, str(made(tmp_path / "configured")))
    explicit = made(tmp_path / "explicit")
    assert benchlib.tlsf_corpus_dir(
        explicit=explicit,
        env={"ACACIA_TLSF_CORPUS": str(made(tmp_path / "environment"))},
        build_dir=build_dir,
    ) == explicit


def test_env_beats_build_option(benchlib, build_dir, recorded_corpus, tmp_path):
    write_option(build_dir, str(made(tmp_path / "configured")))
    environment = made(tmp_path / "environment")
    assert benchlib.tlsf_corpus_dir(
        env={"ACACIA_TLSF_CORPUS": str(environment)}, build_dir=build_dir
    ) == environment


def test_build_option_beats_pointer(benchlib, build_dir, recorded_corpus, tmp_path):
    configured = made(tmp_path / "configured")
    write_option(build_dir, str(configured))
    assert benchlib.tlsf_corpus_dir(env={}, build_dir=build_dir) == configured


def test_recorded_pointer_is_last_fallback(benchlib, recorded_corpus, tmp_path, monkeypatch):
    elsewhere = tmp_path / "elsewhere"
    elsewhere.mkdir()
    monkeypatch.chdir(elsewhere)
    assert benchlib.tlsf_corpus_dir(env={}) == recorded_corpus


@pytest.mark.parametrize("blank", [None, "", " \t\n"])
@pytest.mark.parametrize("level", ["explicit", "env", "build", "pointer"])
def test_blank_values_are_skipped(
    benchlib, build_dir, recorded_corpus, tmp_path, blank, level
):
    configured = made(tmp_path / "configured")
    environment = made(tmp_path / "environment")
    write_option(build_dir, str(configured))
    if level == "explicit":
        assert benchlib.tlsf_corpus_dir(
            explicit=blank,
            env={"ACACIA_TLSF_CORPUS": str(environment)},
            build_dir=build_dir,
        ) == environment
    elif level == "env":
        assert benchlib.tlsf_corpus_dir(
            env={"ACACIA_TLSF_CORPUS": blank}, build_dir=build_dir
        ) == configured
    elif level == "build":
        write_option(build_dir, blank)
        assert benchlib.tlsf_corpus_dir(env={}, build_dir=build_dir) == recorded_corpus
    else:
        pointer = benchlib.ROOT / ".acacia-tlsf-corpus-path"
        if blank is None:
            pointer.unlink()
        else:
            pointer.write_text(blank)
        assert benchlib.tlsf_corpus_dir(env={}) is None


def test_environment_defaults_to_os_environ(benchlib, tmp_path, monkeypatch):
    corpus = made(tmp_path / "environment")
    monkeypatch.setenv("ACACIA_TLSF_CORPUS", str(corpus))
    assert benchlib.tlsf_corpus_dir() == corpus
    assert benchlib.tlsf_corpus_dir(env={}) is None


@pytest.mark.parametrize("level", ["explicit", "env", "build", "pointer"])
def test_paths_expand_home_and_resolve(
    benchlib, build_dir, recorded_corpus, tmp_path, monkeypatch, level
):
    monkeypatch.setenv("HOME", str(tmp_path))
    value = "~/child/../recorded corpus"
    arguments = {"env": {}}
    if level == "explicit":
        arguments["explicit"] = value
    elif level == "env":
        arguments["env"] = {"ACACIA_TLSF_CORPUS": value}
    elif level == "build":
        write_option(build_dir, value)
        arguments["build_dir"] = build_dir
    else:
        (benchlib.ROOT / ".acacia-tlsf-corpus-path").write_text(value + "\n")
    assert benchlib.tlsf_corpus_dir(**arguments) == recorded_corpus


def test_relative_path_is_resolved(benchlib, tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    made(tmp_path / "corpus")
    assert benchlib.tlsf_corpus_dir(explicit="corpus", env={}) == tmp_path / "corpus"


def test_build_with_no_meson_info(benchlib, tmp_path):
    build = tmp_path / "unconfigured"
    build.mkdir()
    assert benchlib.build_option(build, OPTION) is None
    assert benchlib.tlsf_corpus_dir(build_dir=build, env={}) is None


@pytest.mark.parametrize(
    "contents", [b"{broken", b"\xff", b"{}", b"null", b"42", b"[]", b'[{}, null, 7]']
)
def test_invalid_or_missing_option_returns_none(benchlib, build_dir, contents):
    (build_dir / "meson-info" / "intro-buildoptions.json").write_bytes(contents)
    assert benchlib.build_option(build_dir, OPTION) is None
    assert benchlib.tlsf_corpus_dir(build_dir=build_dir, env={}) is None


def test_malformed_options_fall_back_to_pointer(benchlib, build_dir, recorded_corpus):
    (build_dir / "meson-info" / "intro-buildoptions.json").write_text("not JSON")
    assert benchlib.tlsf_corpus_dir(build_dir=build_dir, env={}) == recorded_corpus


@pytest.mark.parametrize("value", ["some/path", "", " \t", False, 0, ["a", "b"], None])
def test_build_option_returns_the_value_unchanged(benchlib, build_dir, value):
    write_option(build_dir, value)
    assert benchlib.build_option(str(build_dir), OPTION) == value
    assert benchlib.build_option(build_dir, "absent") is None


def test_build_option_ignores_unrelated_rows(benchlib, build_dir):
    (build_dir / "meson-info" / "intro-buildoptions.json").write_text(
        json.dumps([None, 7, {}, {"name": "other", "value": 1}, {"name": OPTION, "value": "chosen"}])
    )
    assert benchlib.build_option(build_dir, OPTION) == "chosen"
    assert benchlib.build_option(None, OPTION) is None


def test_deleted_corpus_pointer_is_ignored(benchlib, recorded_corpus):
    (recorded_corpus / ".acacia-tlsf-corpus").unlink()
    recorded_corpus.rmdir()
    assert benchlib.tlsf_corpus_dir(env={}) is None


def test_pointer_without_marker_is_ignored(benchlib, recorded_corpus):
    # Only the pointer needs the marker: it is a leftover record rather than
    # something the operator named for this run.
    (recorded_corpus / ".acacia-tlsf-corpus").unlink()
    assert benchlib.tlsf_corpus_dir(env={}) is None
    assert benchlib.tlsf_corpus_dir(explicit=recorded_corpus, env={}) == recorded_corpus


def test_failure_names_materialize_and_both_remedies(benchlib):
    message = benchlib.tlsf_failure(("syntcomp25", "one.ltl"), "no TLSF corpus directory is set")
    assert message.startswith("GATE FAIL: syntcomp25/one.ltl needs its TLSF source, but ")
    assert "python3 benchmarking/syntcomp-corpus.py materialize --out DIR" in message
    assert "export ACACIA_TLSF_CORPUS=DIR" in message
    assert "-Dacacia_tlsf_corpus_dir=DIR" in message


def test_a_setting_naming_a_missing_directory_is_skipped(benchlib, build_dir, tmp_path):
    # The recorded p5 builds carry an acacia_tlsf_corpus_dir pointing into /tmp
    # at a directory that has since been deleted.  Returning it would turn one
    # legible failure into forty "TLSF source is absent" ones.
    write_option(build_dir, str(tmp_path / "deleted"))
    live = made(tmp_path / "live")
    assert benchlib.tlsf_corpus_dir(build_dir=build_dir, env={}) is None
    assert benchlib.tlsf_corpus_dir(
        build_dir=build_dir, env={"ACACIA_TLSF_CORPUS": str(live)}
    ) == live


def test_diagnosis_names_every_mechanism_and_why_it_failed(benchlib, build_dir, tmp_path):
    write_option(build_dir, str(tmp_path / "deleted"))
    diagnosis = benchlib.tlsf_corpus_diagnosis(build_dir=build_dir, env={})

    assert "--tlsf-corpus is unset" in diagnosis
    assert "ACACIA_TLSF_CORPUS is unset" in diagnosis
    assert f"acacia_tlsf_corpus_dir names {tmp_path / 'deleted'}" in diagnosis
    assert "which is not a directory" in diagnosis
    assert ".acacia-tlsf-corpus-path is unset" in diagnosis


def test_diagnosis_is_empty_once_something_resolves(benchlib, tmp_path):
    live = made(tmp_path / "live")
    assert benchlib.tlsf_corpus_diagnosis(explicit=live, env={}) == ""
