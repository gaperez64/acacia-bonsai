"""Unseen parameterized TLSF exercises frontend-bound online lifting."""
from __future__ import annotations

import json
import os
import pathlib
import random
import subprocess
import sys
import time
from unittest import mock

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

from acacia_lift.artifact import Aag, AagBuilder  # noqa: E402
from acacia_lift import runner  # noqa: E402
from acacia_lift.bdd_kernel import VarInfo  # noqa: E402
from acacia_lift.direct import Decline  # noqa: E402
from acacia_lift.lifting.schema import Bdds, GameInstance, learn_predicate  # noqa: E402
from acacia_lift.lifting.schema import learn_certificate, prepare  # noqa: E402
from acacia_lift.lifting.proof import prove  # noqa: E402
from acacia_lift.lifting.provenance import discover  # noqa: E402
from acacia_lift.lifting.source import lower, solve_seed  # noqa: E402
from acacia_lift.tools import configuration_defaults  # noqa: E402


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
         *, extra_parameter: bool = False) -> tuple[dict, pathlib.Path]:
    tmp_path.mkdir(parents=True, exist_ok=True)
    config = configuration_defaults()
    if not all(path.is_file() for path in
               (config.solver, config.checker, config.tlsf2tlsf, config.monitor)):
        pytest.skip("tlsf-tools build unavailable")
    source = tmp_path / f"{random.Random(seed ^ 991).getrandbits(128):032x}.tlsf"
    source.write_text(_spec(kind, size, seed,
                            extra_parameter=extra_parameter), encoding="utf-8")
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


def test_wrapper_accepts_only_checked_lifted_result(tmp_path: pathlib.Path) -> None:
    config = configuration_defaults()
    if not config.solver.is_file():
        pytest.skip("tlsf-tools build unavailable")
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
    if not config.solver.is_file():
        pytest.skip("tlsf-tools build unavailable")
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
    if not config.solver.is_file():
        pytest.skip("tlsf-tools build unavailable")
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
    if not config.monitor.is_file():
        pytest.skip("tlsf-tools build unavailable")
    tlsf = tmp_path / "strict.tlsf"
    tlsf.write_text(_spec("request", 4, 887), encoding="utf-8")
    deadline = time.monotonic() + 10
    target = lower(tlsf, tmp_path / "target", config, deadline,
                   semantics="strict")
    window = discover(tlsf, target, tmp_path / "seeds", config, deadline)
    certificates = [solve_seed(instance, tmp_path / f"solve_{i}", config, deadline)
                    for i, instance in enumerate(window.instances)]
    seeds, target_instance = prepare(window, target, certificates)
    bdds = Bdds(config, max(len(item.variables) for item in [*seeds, target_instance]))
    predicates, depths, _arities = learn_certificate(bdds, seeds, target_instance,
                                                      deadline)
    result = prove(bdds, target_instance, predicates, depths, tmp_path, config,
                   deadline)
    assert result.reduction_semantics == "strict"
    assert result.proof_method in {"certificate", "gr1-region-v1"}
    region_output = tmp_path / "region"
    region_output.mkdir()
    with mock.patch("acacia_lift.lifting.proof.emit_policy",
                    side_effect=Decline("policy", "budget_exhausted")):
        region = prove(bdds, target_instance, predicates, depths, region_output,
                       config, deadline)
    assert region.proof_method == "gr1-region-v1"
    assert region.policy is None


def test_k_equals_n_predicate_declines_without_larger_witness(tmp_path: pathlib.Path) -> None:
    config = configuration_defaults()
    if not config.bindings_site.is_dir():
        pytest.skip("BuDDy bindings unavailable")
    seeds = []
    for n in (2, 3):
        builder = AagBuilder([f"bit_{i}" for i in range(n)])
        expression = 0
        for i in range(n):
            expression = builder.lor(expression, 2 * (i + 1))
        path = tmp_path / f"or_{n}.aag"
        path.write_text(builder.render([("inv", expression)], "synthetic predicate"))
        circuit = Aag.read(path)
        seeds.append(GameInstance(
            None, tuple(range(n)), circuit, circuit, None,
            [VarInfo(i, ("letter", "input", "input:1", (i,)), frozenset({i}))
             for i in range(n)], [], {i: () for i in range(n)}))
    bdds = Bdds(config, 3)
    with pytest.raises(Decline, match="no_bounded_exact_template"):
        learn_predicate(bdds, seeds, ["inv", "inv"], [None, None],
                        time.monotonic() + 5)
