"""Production decisions survive basename randomization and signal alpha renaming."""

from __future__ import annotations

import importlib.util
import json
import os
import pathlib

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
OBFUSCATOR = ROOT / "benchmarking/tools/obfuscate-tlsf.py"
BUILD = pathlib.Path(os.environ.get(
    "ACACIA_TLSF_TOOLS_BUILD", ROOT / "build/subprojects/tlsf-tools"))
VERIFY = OBFUSCATOR

CASES = (
    ("G out;", "Mealy"),
    ("G (inp -> out);", "Mealy"),
    ("G (inp -> F out);", "Mealy"),
    ("G F out;", "Mealy"),
    ("G out; G !out;", "Mealy"),
    ("G out; F !out;", "Mealy"),
    ("G !out; F out;", "Mealy"),
    ("G out;", "Moore"),
    ("G (inp -> out);", "Moore"),
    ("G F out;", "Moore"),
)


def _obfuscator():
    spec = importlib.util.spec_from_file_location("obfuscate_tlsf", OBFUSCATOR)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _verifier():
    spec = importlib.util.spec_from_file_location("verify_obfuscation", VERIFY)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_declarations_without_trailing_semicolons() -> None:
    text = "MAIN { INPUTS { scalar } OUTPUTS { color mode } }"
    assert _obfuscator().signal_names(text) == ["scalar", "mode"]


@pytest.mark.parametrize(("declarations", "formula", "expected", "parameters"), [
    ("INPUTS { a; } OUTPUTS { b; }", "G (a -> b);", {"a", "b"}, False),
    ("INPUTS { req[n]; } OUTPUTS { grant[n]; }",
     "G (req[0] -> grant[0]);", {"req", "grant"}, True),
    ("INPUTS { color mode; } OUTPUTS { active; }",
     "G (mode[0] -> active);", {"mode", "active"}, False),
])
def test_declaration_fixtures_map_every_signal_and_preserve_parameters(
        tmp_path: pathlib.Path, declarations: str, formula: str,
        expected: set[str], parameters: bool) -> None:
    if not all((BUILD / name).is_file() for name in ("tlsfinfo", "tlsf2ltl")):
        pytest.skip("tlsf-tools build unavailable")
    global_block = "GLOBAL { "
    if parameters:
        global_block += "PARAMETERS { n = 2; } "
    global_block += "DEFINITIONS { enum color = RED: 00 BLUE: 01; } } "
    source = tmp_path / "fixture.tlsf"
    source.write_text(
        'INFO { TITLE: "fixture" SEMANTICS: Mealy TARGET: Mealy }\n'
        f'{global_block} MAIN {{ {declarations} GUARANTEES {{ {formula} }} }}\n')
    obfuscator = _obfuscator()
    original = source.read_text()
    target, sidecar = obfuscator.write_obfuscated(source, tmp_path / "renamed", 724)
    mapping = json.loads(sidecar.read_text())["signals"]
    assert set(mapping) == expected == set(obfuscator.signal_names(original))
    assert set(obfuscator.signal_names(target.read_text())) == set(mapping.values())
    assert "enum color" in target.read_text()
    if parameters:
        assert "n = 2" in target.read_text()
        assert "req[n]" not in target.read_text()
        assert "grant[n]" not in target.read_text()
    assert _verifier().check_pair(source, target, BUILD, 12)
