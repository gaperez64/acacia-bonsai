"""BuDDy integration checks run under the configured bindings interpreter."""

from __future__ import annotations

import pathlib
import sys
import time
from unittest import mock

from acacia_lift.artifact import Aag, AagBuilder
from acacia_lift.bdd_kernel import VarInfo
from acacia_lift.direct import Decline
from acacia_lift.lifting.proof import prove
from acacia_lift.lifting.provenance import discover
from acacia_lift.lifting.schema import (Bdds, GameInstance, learn_certificate,
                                        learn_predicate, prepare)
from acacia_lift.lifting.source import lower, solve_seed
from acacia_lift.tools import configuration_defaults


def strict(tlsf: pathlib.Path) -> None:
    config = configuration_defaults()
    output = tlsf.parent
    deadline = time.monotonic() + 10
    target = lower(tlsf, output / "target", config, deadline, semantics="strict")
    window = discover(tlsf, target, output / "seeds", config, deadline)
    certificates = [solve_seed(instance, output / f"solve_{i}", config, deadline)
                    for i, instance in enumerate(window.instances)]
    seeds, target_instance = prepare(window, target, certificates)
    bdds = Bdds(config, max(len(item.variables) for item in [*seeds, target_instance]))
    predicates, depths, _arities = learn_certificate(bdds, seeds, target_instance,
                                                      deadline)
    result = prove(bdds, target_instance, predicates, depths, output, config,
                   deadline)
    assert result.reduction_semantics == "strict"
    assert result.proof_method in {"certificate", "gr1-region-v1"}
    region_output = output / "region"
    region_output.mkdir()
    with mock.patch("acacia_lift.lifting.proof.emit_policy",
                    side_effect=Decline("policy", "budget_exhausted")):
        region = prove(bdds, target_instance, predicates, depths, region_output,
                       config, deadline)
    assert region.proof_method == "gr1-region-v1"
    assert region.policy is None


def k_equals_n(output: pathlib.Path) -> None:
    config = configuration_defaults()
    seeds = []
    for n in (2, 3):
        builder = AagBuilder([f"bit_{i}" for i in range(n)])
        expression = 0
        for i in range(n):
            expression = builder.lor(expression, 2 * (i + 1))
        path = output / f"or_{n}.aag"
        path.write_text(builder.render([("inv", expression)], "synthetic predicate"))
        circuit = Aag.read(path)
        seeds.append(GameInstance(
            None, tuple(range(n)), circuit, circuit, None,
            [VarInfo(i, ("letter", "input", "input:1", (i,)), frozenset({i}))
             for i in range(n)], [], {i: () for i in range(n)}))
    bdds = Bdds(config, 3)
    try:
        learn_predicate(bdds, seeds, ["inv", "inv"], [None, None],
                        time.monotonic() + 5)
    except Decline as error:
        assert "no_bounded_exact_template" in str(error)
    else:
        raise AssertionError("expected a bounded-template decline")


if __name__ == "__main__":
    case, path = sys.argv[1:]
    {"strict": strict, "k_equals_n": k_equals_n}[case](pathlib.Path(path))
