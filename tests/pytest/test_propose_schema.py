"""Focused tests for the bounded automatic arbiter schema proposer."""

import importlib.util
import sys
from dataclasses import replace
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = (ROOT / "benchmarking" / "witness-lifting-20260918" / "families" /
          "proposals" / "propose_schema.py")
SPEC = importlib.util.spec_from_file_location("propose_schema", SCRIPT)
proposer = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = proposer
SPEC.loader.exec_module(proposer)


@pytest.fixture(scope="module")
def arbiter_winners(tmp_path_factory):
    output_dir = tmp_path_factory.mktemp("automatic-arbiter")
    family, seed_2, seed_3 = proposer.arbiter_inputs(output_dir)
    required = [
        family.verify_aiger_ltl,
        family.verify_conjuncts,
        family.tlsf2ltl,
    ]
    if not all(path.exists() for path in required):
        pytest.skip("local tlsf-tools checker build is unavailable")
    try:
        import spot  # noqa: F401
    except ImportError:
        pytest.skip("Spot Python bindings are unavailable to this interpreter")
    return proposer.propose(family, seed_2, seed_3)


def test_real_arbiter_seeds_produce_a_candidate_verified_at_n10(arbiter_winners):
    assert arbiter_winners
    winner = arbiter_winners[0]
    assert winner.proposal_origin == "automatic"
    checks = {result.parameter: result for result in winner.checks}
    assert set(checks) == {2, 3, 4, 10}
    assert all(result.verdict == "VERIFIED" for result in checks.values())
    assert checks[10].checker == "verify_conjuncts.py"
    assert checks[10].checker_mode == "exact conjunct decomposition"
    assert "verified (all conjuncts)" in checks[10].stdout
    assert any(evidence.startswith("n=2:") for evidence in winner.influenced_by)
    assert any(evidence.startswith("n=3:") for evidence in winner.influenced_by)


def test_candidate_cap_is_enforced_from_the_single_package_limit(tmp_path, monkeypatch):
    limits = proposer.ProposerLimits()
    _, seed_2, seed_3 = proposer.arbiter_inputs(tmp_path)
    template = proposer.discover_candidate_schemas(seed_2, seed_3)[0]

    def forty_recognized_schemas(_observations):
        for index in range(40):
            yield replace(template, schema_id=f"bounded-test/{index:02d}")

    monkeypatch.setattr(proposer, "_recognize_candidates", forty_recognized_schemas)
    bounded = proposer.discover_candidate_schemas(seed_2, seed_3, limits=limits)
    assert len(bounded) == proposer.MAX_CANDIDATES_PER_TARGET == 32
    assert [candidate.schema_id for candidate in bounded] == [
        f"bounded-test/{index:02d}" for index in range(32)
    ]
    with pytest.raises(ValueError):
        proposer.ProposerLimits(max_candidates=33)


def test_only_one_seed_declines_as_unknown(tmp_path):
    family, seed_2, _ = proposer.arbiter_inputs(tmp_path)
    assert proposer.propose(family, seed_2, None) == []


def test_unrecognizable_seed_declines_instead_of_guessing(tmp_path):
    # Correct indexed symbol roles, but no state, output behavior, remembered
    # request, or cyclic phase: it must not match the supported skeleton.
    dead = tmp_path / "dead_n2.aag"
    dead.write_text(
        "aag 2 2 0 2 0\n2\n4\n0\n0\ni0 r_0\ni1 r_1\no0 g_0\no1 g_1\n",
        encoding="utf-8",
    )
    _, real_seed_2, real_seed_3 = proposer.arbiter_inputs(tmp_path)
    dead_seed = replace(real_seed_2, aiger_path=dead)
    assert proposer.discover_candidate_schemas(dead_seed, real_seed_3) == ()


def test_deterministic_candidate_ordering_on_same_real_inputs(tmp_path):
    _, seed_2, seed_3 = proposer.arbiter_inputs(tmp_path)
    first = proposer.discover_candidate_schemas(seed_2, seed_3)
    second = proposer.discover_candidate_schemas(seed_2, seed_3)
    assert [candidate.schema_id for candidate in first] == [
        candidate.schema_id for candidate in second
    ]
    assert [candidate.description() for candidate in first] == [
        candidate.description() for candidate in second
    ]
    assert first and first[0].influenced_by == second[0].influenced_by
