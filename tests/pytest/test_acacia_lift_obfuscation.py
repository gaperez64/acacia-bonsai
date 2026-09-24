"""Production decisions survive basename randomization and signal alpha renaming."""

from __future__ import annotations

import importlib.util
import json
import os
import pathlib
import random
import subprocess
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
WRAPPER = ROOT / "scripts/acacia-lift-portfolio.py"
OBFUSCATOR = ROOT / "benchmarking/gr1-par2-20260923/obfuscate-tlsf.py"
BUILD = ROOT / "subprojects/tlsf-tools/build-oxidd"
VERIFY = ROOT / "benchmarking/gr1-par2-20260923/verify-obfuscation.py"

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


def _run(source: pathlib.Path, route: pathlib.Path, fallback: pathlib.Path) -> tuple[str, str]:
    command = [sys.executable, str(WRAPPER), "--cap", "6",
               "--lift-budget-seconds", "3", "--route-record", str(route),
               "--", str(fallback), "-T", str(source)]
    completed = subprocess.run(command, capture_output=True, text=True, timeout=7,
                               env={**os.environ, "PYTHONPATH": str(ROOT / "scripts")},
                               check=False)
    assert completed.returncode in (0, 1, 2), completed.stderr
    record = json.loads(route.read_text(encoding="utf-8"))
    return record["winner"], completed.stdout.strip()


def test_ten_obfuscated_pairs_keep_direct_and_decline_decisions(tmp_path: pathlib.Path) -> None:
    if not all((BUILD / name).is_file() for name in
               ("tlsfsolve", "tlsfcertcheck", "tlsf2ltl")):
        pytest.skip("tlsf-tools build unavailable")
    fallback = tmp_path / "fallback"
    fallback.write_text("#!/usr/bin/env python3\nprint('UNKNOWN')\nraise SystemExit(2)\n")
    fallback.chmod(0o755)
    obfuscator = _obfuscator()
    decisions = set()
    for index, (formula, semantics) in enumerate(CASES):
        source = tmp_path / f"original_{index}.tlsf"
        source.write_text(
            f'INFO {{ TITLE: "case" SEMANTICS: {semantics} TARGET: Mealy }}\n'
            f'MAIN {{ INPUTS {{ inp; }} OUTPUTS {{ out; }} GUARANTEES {{ {formula} }} }}\n')
        target, sidecar = obfuscator.write_obfuscated(source, tmp_path / f"changed_{index}",
                                                     20260924 + index)
        assert json.loads(sidecar.read_text())["signals"].keys() == {"inp", "out"}
        original = _run(source, tmp_path / f"original_{index}.json", fallback)
        changed = _run(target, tmp_path / f"changed_{index}.json", fallback)
        assert original == changed, index
        decisions.add(original)
    assert ("lifting", "REALIZABLE") in decisions
    assert ("lifting", "UNREALIZABLE") in decisions
    assert ("fallback-pending", "UNKNOWN") in decisions


@pytest.mark.parametrize(("input_name", "output_name", "formula"), [
    pytest.param("monitor_0_state_0", "assumption_safety_violated",
                 "G (monitor_0_state_0 -> assumption_safety_violated);",
                 marks=pytest.mark.xfail(
                     strict=True, reason="pending tlsf-tools disjoint monitor/latch symbols")),
    ("monitor_1_state_0", "controllable_b",
     "G (monitor_1_state_0 -> controllable_b);"),
    pytest.param("controllable_a", "b", "G !controllable_a;",
                 marks=pytest.mark.xfail(
                     strict=True, reason="pending tlsf-tools injective ownership encoding")),
])
def test_adversarial_signal_names(tmp_path: pathlib.Path, input_name: str,
                                  output_name: str, formula: str) -> None:
    if not all((BUILD / name).is_file() for name in
               ("tlsfsolve", "tlsfcertcheck", "tlsf2ltl")):
        pytest.skip("tlsf-tools build unavailable")
    source = tmp_path / "unseen_name.tlsf"
    source.write_text(
        'INFO { TITLE: "adversarial" SEMANTICS: Mealy TARGET: Mealy }\n'
        f'MAIN {{ INPUTS {{ {input_name}; }} OUTPUTS {{ {output_name}; }} '
        f'GUARANTEES {{ {formula} }} }}\n')
    target, sidecar = _obfuscator().write_obfuscated(source, tmp_path / "changed", 914)
    assert set(json.loads(sidecar.read_text())["signals"]) == {input_name, output_name}
    fallback = tmp_path / "fallback"
    fallback.write_text("#!/usr/bin/env python3\nprint('UNKNOWN')\nraise SystemExit(2)\n")
    fallback.chmod(0o755)
    original = _run(source, tmp_path / "original.json", fallback)
    assert original == _run(target, tmp_path / "changed.json", fallback)
    assert original[0] == "lifting"


def test_generated_unseen_specs_and_random_basenames(tmp_path: pathlib.Path) -> None:
    if not all((BUILD / name).is_file() for name in
               ("tlsfsolve", "tlsfcertcheck", "tlsf2ltl")):
        pytest.skip("tlsf-tools build unavailable")
    rng = random.Random(20260924)
    fallback = tmp_path / "fallback"
    fallback.write_text("#!/usr/bin/env python3\nprint('UNKNOWN')\nraise SystemExit(2)\n")
    fallback.chmod(0o755)
    for index in range(3):
        basename = f"case_{rng.getrandbits(128):032x}.tlsf"
        source = tmp_path / basename
        n = rng.choice((2, 3))
        inputs = [f"u_{index}_{bit}" for bit in range(n)]
        outputs = [f"v_{index}_{bit}" for bit in range(n)]
        selected = rng.randrange(n)
        source.write_text(
            'INFO { TITLE: "generated" SEMANTICS: Mealy TARGET: Mealy }\n'
            f'MAIN {{ INPUTS {{ {"; ".join(inputs)}; }} '
            f'OUTPUTS {{ {"; ".join(outputs)}; }} GUARANTEES {{ '
            f'G ({inputs[selected]} -> {outputs[selected]}); '
            f'G F {outputs[(selected + 1) % n]}; }} }}\n')
        target, sidecar = _obfuscator().write_obfuscated(source, tmp_path / f"set_{index}",
                                                          rng.getrandbits(64))
        assert set(json.loads(sidecar.read_text())["signals"]) == set(inputs + outputs)
        assert target.name != source.name
        original = _run(source, tmp_path / f"original_{index}.json", fallback)
        assert original == ("lifting", "REALIZABLE")
        assert original == _run(target, tmp_path / f"changed_{index}.json", fallback)


def test_random_basename_only_keeps_verdict(tmp_path: pathlib.Path) -> None:
    if not all((BUILD / name).is_file() for name in
               ("tlsfsolve", "tlsfcertcheck", "tlsf2ltl")):
        pytest.skip("tlsf-tools build unavailable")
    fallback = tmp_path / "fallback"
    fallback.write_text("#!/usr/bin/env python3\nprint('UNKNOWN')\nraise SystemExit(2)\n")
    fallback.chmod(0o755)
    source = tmp_path / "seed.tlsf"
    source.write_text(
        'INFO { TITLE: "basename" SEMANTICS: Mealy TARGET: Mealy }\n'
        'MAIN { INPUTS { a; } OUTPUTS { b; } GUARANTEES { G b; } }\n')
    baseline = _run(source, tmp_path / "baseline.json", fallback)
    assert baseline == ("lifting", "REALIZABLE")
    rng = random.Random(7391)
    for index in range(4):
        renamed = tmp_path / f"{rng.getrandbits(128):032x}.tlsf"
        renamed.write_bytes(source.read_bytes())
        assert _run(renamed, tmp_path / f"basename_{index}.json", fallback) == baseline
