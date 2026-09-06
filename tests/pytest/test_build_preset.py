import importlib.util
import json
import pathlib
import sys

import pytest


@pytest.fixture
def benchlib(monkeypatch):
    script = pathlib.Path(__file__).resolve().parents[2] / "benchmarking" / "benchlib.py"
    spec = importlib.util.spec_from_file_location("benchlib_preset_tests", script)
    module = importlib.util.module_from_spec(spec)
    monkeypatch.setitem(sys.modules, spec.name, module)
    spec.loader.exec_module(module)
    return module


@pytest.mark.parametrize("value,expected", [
    ("best_decomp_rank_bucketed_semantic_mona", "best_decomp_rank_bucketed_semantic_mona"),
    ("", None), (None, None), (False, None), (42, None),
])
def test_build_preset_reads_recorded_option(benchlib, tmp_path, value, expected):
    info = tmp_path / "meson-info"
    info.mkdir()
    (info / "intro-buildoptions.json").write_text(json.dumps([
        {"name": "acacia_local_certificate", "value": True},
        {"name": "acacia_preset", "value": value},
    ]))
    assert benchlib.build_preset(tmp_path) == expected


@pytest.mark.parametrize("contents", [
    None, "not json", "{}", "[]",
    '[{"name": "acacia_local_certificate", "value": true}]',
    '[{"name": "acacia_preset"}]',
])
def test_build_preset_does_not_infer_a_name(benchlib, tmp_path, contents):
    if contents is not None:
        info = tmp_path / "meson-info"
        info.mkdir()
        (info / "intro-buildoptions.json").write_text(contents)
    assert benchlib.build_preset(tmp_path) is None
