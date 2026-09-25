"""Unseen parameterized TLSF exercises frontend-bound online lifting."""
from __future__ import annotations

import json
import os
import pathlib
import random
import subprocess
import sys
import tempfile
import time
from types import SimpleNamespace
from unittest import mock

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

from acacia_lift import runner  # noqa: E402
from acacia_lift.direct import Decline  # noqa: E402
from acacia_lift.lifting.schema import Goal, learn_certificate  # noqa: E402
from acacia_lift.lifting.source import lower  # noqa: E402
from acacia_lift.tools import (  # noqa: E402
    _probe_bindings, bindings_environment, configuration_defaults,
)
from _lift_requirements import require_buddy, require_lift_tools  # noqa: E402


def _spec(kind: str, size: int, seed: int, *, extra_parameter: bool = False) -> str:
    rng = random.Random(seed)
    req = f"p{rng.getrandbits(48):012x}"
    grant = f"q{rng.getrandbits(48):012x}"
    local = {
        "request": f"&&[0 <= i < n] G ({req}[i] -> F {grant}[i]);",
        "mutex": f"&&[0 <= i < n] G !({req}[i] && {grant}[i]);",
        "cardinality": (f"G (||[0 <= i < n] {grant}[i]); "
                        f"&&[0 <= i < n] G ({req}[i] -> {grant}[i]);"),
        "pairwise": (f"&&[0 <= i < n] G ({grant}[i] -> {req}[i]); "
                     f"&&[0 <= i < n] &&[i < j < n] "
                     f"G !({grant}[i] && {grant}[j]);"),
    }[kind]
    return ('INFO { TITLE: "generated" SEMANTICS: Mealy TARGET: Mealy }\n'
            f'GLOBAL {{ PARAMETERS {{ n = {size}; '
            f'{"spare = 7;" if extra_parameter else ""} }} }}\n'
            f'MAIN {{ INPUTS {{ {req}[n]; }} OUTPUTS {{ {grant}[n]; }} '
            f'GUARANTEES {{ {local} }} }}\n')


def _run(tmp_path: pathlib.Path, kind: str, size: int, seed: int,
         *, extra_parameter: bool = False,
         spec_text: str | None = None) -> tuple[dict, pathlib.Path]:
    tmp_path.mkdir(parents=True, exist_ok=True)
    config = configuration_defaults()
    require_lift_tools(config)
    source = tmp_path / f"{random.Random(seed ^ 991).getrandbits(128):032x}.tlsf"
    source.write_text(spec_text if spec_text is not None else
                      _spec(kind, size, seed, extra_parameter=extra_parameter),
                      encoding="utf-8")
    output = tmp_path / "output"
    evidence_path = tmp_path / "evidence.json"
    proc = subprocess.run([sys.executable, "-m", "acacia_lift.runner",
                           "-T", str(source), "--budget", "15",
                           "--eligibility-budget-seconds", "1",
                           "--output-dir", str(output),
                           "--evidence-out", str(evidence_path)],
                          env={**os.environ, "PYTHONPATH": str(ROOT / "scripts")},
                          capture_output=True, text=True, timeout=17, check=False)
    assert proc.returncode in (0, 1, 2), proc.stderr
    return json.loads(evidence_path.read_text()), output


def _run_bindings_case(config, case: str, path: pathlib.Path) -> None:
    environment = bindings_environment(config)
    environment["PYTHONPATH"] = str(ROOT / "scripts") + os.pathsep + environment["PYTHONPATH"]
    proc = subprocess.run(
        [str(config.bindings_python), "-s",
         str(ROOT / "tests/pytest/_lift_bindings_cases.py"), case, str(path)],
        env=environment, capture_output=True, text=True, timeout=18, check=False,
    )
    assert proc.returncode == 0, proc.stdout + proc.stderr


@pytest.mark.parametrize("kind", ["request", "mutex", "cardinality"])
def test_generated_unseen_families_lift_and_verify(tmp_path: pathlib.Path,
                                                    kind: str) -> None:
    evidence, _output = _run(tmp_path, kind, 4, 342 + len(kind))
    assert evidence["route"] == "lifted-certified"
    assert evidence["target_verified"] is True
    assert evidence["result"]["verdict"] == "REALIZABLE"
    assert evidence["result"]["proof_method"] in {"certificate", "gr1-region-v1"}
    assert evidence["provenance_format_version"] == 1
    assert all(vector["n"] < 4 for vector in evidence["seeds"])
    assert evidence["predicate_arities"]
    assert evidence["move_source"] == "target_transition"
    assert not any(key.startswith("move_") for key in evidence["predicate_arities"])
    assert evidence["global_knobs"]["learn_move_schemas"] is False


def test_user_site_decoy_does_not_shadow_configured_buddy(
    tmp_path: pathlib.Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    config = configuration_defaults()
    require_lift_tools(config)
    scratch = ROOT / "build_scratch" / "usersite-fix"
    scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=scratch) as temporary:
        userbase = pathlib.Path(temporary)
        unprotected = dict(os.environ)
        unprotected["PYTHONUSERBASE"] = str(userbase)
        unprotected.pop("PYTHONNOUSERSITE", None)
        unprotected.pop("PYTHONPATH", None)
        site = subprocess.run(
            [str(config.bindings_python), "-c",
             "import site; print(site.getusersitepackages())"],
            env=unprotected, capture_output=True, text=True, timeout=5, check=True,
        )
        decoy = pathlib.Path(site.stdout.strip()) / "buddy.py"
        decoy.parent.mkdir(parents=True)
        decoy.write_text('raise RuntimeError("decoy BuDDy imported")\n',
                         encoding="utf-8")
        origin = subprocess.run(
            [str(config.bindings_python), "-c",
             "import importlib.util; print(importlib.util.find_spec('buddy').origin)"],
            env=unprotected, capture_output=True, text=True, timeout=5, check=True,
        )
        assert pathlib.Path(origin.stdout.strip()).resolve() == decoy.resolve()

        monkeypatch.setenv("PYTHONUSERBASE", str(userbase))
        probe = _probe_bindings(config)
        assert pathlib.Path(probe["binding_module"]).resolve() == (
            config.bindings_site / "buddy.py").resolve()
        evidence, _ = _run(tmp_path, "request", 4, 342 + len("request"))
        assert evidence["route"] == "lifted-certified"
        assert evidence["target_verified"] is True


@pytest.mark.parametrize("failure", ["learning", "instantiation"])
def test_enabled_move_schema_failure_declines(failure: str) -> None:
    goal = Goal(0, ("structural_goal",), None)
    seed = SimpleNamespace(goals=[goal], levels=lambda _goal: 0)
    target = SimpleNamespace(goals=[goal], game=SimpleNamespace(justice=[[1]]),
                             fairness=0)
    bdds = mock.Mock()
    bdds.game_literal.return_value = object()
    def learn(_bdds, _seeds, names, _goals, _deadline):
        if names[0].startswith("move_") and failure == "learning":
            raise Decline("schema_capacity", "subset_count_limit")
        return {}, 0
    calls = []
    def instantiate(_bdds, _target, _templates, _arity, _goal, _deadline):
        calls.append(True)
        if len(calls) == 2 and failure == "instantiation":
            raise Decline("instantiate", "missing_target_variable")
        return object()
    with (mock.patch.object(runner.settings, "LEARN_MOVE_SCHEMAS", True),
          mock.patch.object(runner.schema, "learn_predicate", side_effect=learn),
          mock.patch.object(runner.schema, "instantiate", side_effect=instantiate)):
        with pytest.raises(Decline, match=("subset_count_limit" if failure == "learning"
                                           else "missing_target_variable")):
            learn_certificate(bdds, [seed, seed], target, time.monotonic() + 1)


def test_default_moves_skip_learning() -> None:
    goal = Goal(0, ("structural_goal",), None)
    seed = SimpleNamespace(goals=[goal], levels=lambda _goal: 0)
    target = SimpleNamespace(goals=[goal], game=SimpleNamespace(justice=[[1]]),
                             fairness=0)
    bdds = mock.Mock()
    with (mock.patch.object(runner.settings, "LEARN_MOVE_SCHEMAS", False),
          mock.patch.object(runner.schema, "learn_predicate", return_value=({}, 0)) as learn,
          mock.patch.object(runner.schema, "instantiate", return_value=object())):
        predicates, _depths, arities = learn_certificate(
            bdds, [seed, seed], target, time.monotonic() + 1)
    assert set(predicates) == {"inv", "goal_0"}
    assert arities == {"inv": 0}
    assert learn.call_count == 1


def test_smallest_and_hidden_pairwise_do_not_lift(tmp_path: pathlib.Path) -> None:
    smallest, _ = _run(tmp_path / "smallest", "request", 1, 4)
    assert smallest["route"] != "lifted-certified"
    pairwise, _ = _run(tmp_path / "pairwise", "pairwise", 4, 8)
    assert pairwise["route"] != "lifted-certified"
    assert pairwise["lifting_failure"]["stage"] == "seed_window"


def test_alpha_renamed_and_random_basename_twins_agree(tmp_path: pathlib.Path) -> None:
    first, _ = _run(tmp_path / "first", "request", 4, 749)
    second, _ = _run(tmp_path / "second", "request", 4, 823)
    assert (first["route"], first["result"]["verdict"],
            first["predicate_arities"]) == (
                second["route"], second["result"]["verdict"],
                second["predicate_arities"])


def test_name_size_and_formula_format_metamorphs(tmp_path: pathlib.Path) -> None:
    base = _spec("request", 5, 145)
    req = f"p{random.Random(145).getrandbits(48):012x}"
    rng = random.Random(145)
    rng.getrandbits(48)
    grant = f"q{rng.getrandbits(48):012x}"
    named = base.replace(req, "g").replace(grant, "r")
    spaced = base.replace("G (", "G (   ").replace(" -> F ", "  ->  F  ")
    variants = [
        _run(tmp_path / "base", "request", 5, 145, spec_text=base)[0],
        _run(tmp_path / "names", "request", 5, 145, spec_text=named)[0],
        _run(tmp_path / "formula", "request", 5, 145, spec_text=spaced)[0],
        _run(tmp_path / "size", "request", 7, 145)[0],
    ]
    assert {(item["route"], item["result"]["verdict"], item.get("move_source"))
            for item in variants} == {("lifted-certified", "REALIZABLE",
                                      "target_transition")}


def test_wrapper_accepts_only_checked_lifted_result(tmp_path: pathlib.Path) -> None:
    config = configuration_defaults()
    require_lift_tools(config)
    source = tmp_path / "parametric.tlsf"
    source.write_text(_spec("request", 4, 701), encoding="utf-8")
    fallback = tmp_path / "fallback"
    fallback.write_text("#!/usr/bin/env python3\nprint('UNKNOWN')\nraise SystemExit(2)\n")
    fallback.chmod(0o755)
    record_path = tmp_path / "route.json"
    proc = subprocess.run([sys.executable, str(ROOT / "scripts/acacia-lift-portfolio.py"),
                           "--cap", "10", "--lift-budget-seconds", "8",
                           "--route-record", str(record_path), "--",
                           str(fallback), "-T", str(source)],
                          env={**os.environ, "PYTHONPATH": str(ROOT / "scripts")},
                          capture_output=True, text=True, timeout=12, check=False)
    assert (proc.returncode, proc.stdout.strip()) == (0, "REALIZABLE")
    record = json.loads(record_path.read_text())
    assert (record["route"], record["winner"]) == ("lifted-certified", "lifting")


def test_other_parameter_stays_at_target_value(tmp_path: pathlib.Path) -> None:
    evidence, _ = _run(tmp_path, "request", 4, 4101, extra_parameter=True)
    assert evidence["route"] == "lifted-certified"
    assert all(vector["spare"] == 7 and vector["n"] < 4
               for vector in evidence["seeds"])


def test_without_parameters_uses_only_direct_route(tmp_path: pathlib.Path) -> None:
    config = configuration_defaults()
    require_lift_tools(config)
    source = tmp_path / "plain.tlsf"
    source.write_text('INFO { TITLE: "plain" SEMANTICS: Mealy TARGET: Mealy }\n'
                      'MAIN { INPUTS { ask; } OUTPUTS { answer; } '
                      'GUARANTEES { G (ask -> answer); } }\n')
    evidence_path = tmp_path / "evidence.json"
    proc = subprocess.run([sys.executable, "-m", "acacia_lift.runner",
                           "-T", str(source), "--budget", "5",
                           "--eligibility-budget-seconds", "1",
                           "--output-dir", str(tmp_path / "out"),
                           "--evidence-out", str(evidence_path)],
                          env={**os.environ, "PYTHONPATH": str(ROOT / "scripts")},
                          capture_output=True, text=True, timeout=7, check=False)
    assert proc.returncode == 0, proc.stderr
    evidence = json.loads(evidence_path.read_text())
    assert evidence["route"] == "direct-certified"
    assert "seeds" not in evidence


def test_source_mutation_after_binding_cannot_return_verdict(tmp_path: pathlib.Path) -> None:
    config = configuration_defaults()
    require_lift_tools(config)
    tlsf = tmp_path / "mutable.tlsf"
    tlsf.write_text(_spec("request", 4, 604), encoding="utf-8")
    original_lower = lower
    changed = False
    def mutate_once(*args, **kwargs):
        nonlocal changed
        result = original_lower(*args, **kwargs)
        if not changed:
            tlsf.write_text(tlsf.read_text() + "\n", encoding="utf-8")
            changed = True
        return result
    evidence_path = tmp_path / "mutation-evidence.json"
    with mock.patch.object(runner.source, "lower", side_effect=mutate_once):
        code = runner.main(["-T", str(tlsf), "--budget", "8",
                            "--eligibility-budget-seconds", "1",
                            "--output-dir", str(tmp_path / "out"),
                            "--evidence-out", str(evidence_path),
                            "--tlsf-tools-build", str(config.tlsf_tools_build)])
    evidence = json.loads(evidence_path.read_text())
    assert code == 2
    assert evidence["route"] == "attempted-declined"
    assert evidence["target_verified"] is False
    assert evidence["result"]["reason"] == "input_changed"


def test_mutated_lifted_certificate_is_rejected(tmp_path: pathlib.Path) -> None:
    evidence, output = _run(tmp_path, "request", 4, 991)
    assert evidence["route"] == "lifted-certified"
    config = configuration_defaults()
    certificate = pathlib.Path(evidence["result"]["certificate"])
    policy = pathlib.Path(evidence["result"]["policy"])
    lines = certificate.read_text().splitlines()
    header = [int(value) for value in lines[0].split()[1:]]
    first_output = 1 + header[1] + header[2]
    lines[first_output] = "0"
    certificate.write_text("\n".join(lines) + "\n")
    check = output / "mutation-check.json"
    proc = subprocess.run([str(config.checker), "--method", "certificate",
                           "--timeout", "5", "--json-out", str(check),
                           "--certificate", str(certificate), "--certificate-json",
                           str(certificate) + ".json",
                           str(output / "target/game.aag"), str(policy)],
                          capture_output=True, text=True, timeout=7, check=False)
    payload = json.loads(check.read_text()) if check.is_file() else {}
    assert proc.returncode != 0 or payload.get("verdict") != "VERIFIED"


def test_strict_reduction_candidate_is_independently_verified(tmp_path: pathlib.Path) -> None:
    config = configuration_defaults()
    require_lift_tools(config)
    tlsf = tmp_path / "strict.tlsf"
    tlsf.write_text(_spec("request", 4, 887), encoding="utf-8")
    _run_bindings_case(config, "strict", tlsf)


def test_k_equals_n_predicate_declines_without_larger_witness(tmp_path: pathlib.Path) -> None:
    config = configuration_defaults()
    require_buddy(config)
    _run_bindings_case(config, "k_equals_n", tmp_path)
