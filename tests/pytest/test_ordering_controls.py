"""Declaration-only controls must preserve complete input structure."""

import importlib.util
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "ordering_controls", ROOT / "benchmarking/prepare-ordering-controls.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def test_declarations_only_and_involution():
    text = ('INFO { TITLE: "INPUTS { decoy; }" }\n'
            'MAIN { INPUTS { a[n]; // owner\n b[m]; /* ; } */ c; }\n'
            'OUTPUTS { enum_t first; second[width(n)]; }\n'
            'GUARANTEE { G (first -> a[0]); } }\n')
    shuffled = MODULE.shuffle_declarations(text)
    assert 'INPUTS { /* ; } */ c; // owner\n b[m]; a[n]; }' in shuffled
    assert 'OUTPUTS { second[width(n)]; enum_t first; }' in shuffled
    assert shuffled.split('GUARANTEE')[1] == text.split('GUARANTEE')[1]
    assert shuffled.split('MAIN')[0] == text.split('MAIN')[0]
    assert MODULE.shuffle_declarations(shuffled) == text


@pytest.mark.parametrize("text", ["MAIN { INPUTS a; }", 
                                 "MAIN { INPUTS { a; ", "INFO { TITLE: \"INPUTS\" }"])
def test_decline_malformed_controls(text):
    with pytest.raises(ValueError):
        MODULE.shuffle_declarations(text)


def test_optional_final_separator():
    text = "MAIN { INPUTS { a; b; c } OUTPUTS { x } GUARANTEE { G x; } }"
    shuffled = MODULE.shuffle_declarations(text)
    assert "INPUTS { c; b; a }" in shuffled
    assert MODULE.shuffle_declarations(shuffled) == text
