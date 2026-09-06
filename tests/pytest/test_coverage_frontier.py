"""Tests for build-coverage-frontier.py.

Every coverage number in SYNTCOMP26-COVERAGE-FRONTIER.md flows through this
script, and its delicate part is the ordering: a multi-parameter family must be
compared componentwise, never by a lexicographic total order dressed up as a
cutoff.  These tests pin that behaviour and the strictness that protects it.
"""
from __future__ import annotations

import csv
import importlib.util
import pathlib
import sys
from decimal import Decimal

import pytest

BENCHMARKING = pathlib.Path(__file__).resolve().parents[2] / "benchmarking"
SCRIPT = BENCHMARKING / "build-coverage-frontier.py"


def load():
    sys.path.insert(0, str(BENCHMARKING))
    try:
        spec = importlib.util.spec_from_file_location("build_coverage_frontier", SCRIPT)
        module = importlib.util.module_from_spec(spec)
        assert spec.loader is not None
        sys.modules[spec.name] = module
        spec.loader.exec_module(module)
        return module
    finally:
        sys.path.pop(0)


frontier = load()


def member(instance, params, portfolio_result="TIMEOUT", seconds="60.0"):
    """Build the family-member shape the ordering helpers consume."""
    class Meta:
        pass

    class Portfolio:
        pass

    meta, portfolio = Meta(), Portfolio()
    meta.instance = instance
    meta.parameter_values = params
    meta.coordinates = tuple(params.values())
    portfolio.result = portfolio_result
    portfolio.seconds_text = seconds
    m = frontier.FamilyMember(metadata=meta, portfolio=portfolio) \
        if hasattr(frontier, "FamilyMember") else None
    if m is None:  # dataclass shape differs; fall back to a simple namespace
        import types
        m = types.SimpleNamespace(metadata=meta, portfolio=portfolio)
    return m


def test_componentwise_lt_requires_strictness_in_some_coordinate():
    a = member("a", {"N": 1, "M": 2})
    b = member("b", {"N": 1, "M": 3})
    same = member("c", {"N": 1, "M": 2})
    assert frontier.componentwise_lt(a, b)
    assert not frontier.componentwise_lt(b, a)
    # Equal points are <= but never <, or every point would dominate itself.
    assert frontier.componentwise_le(a, same)
    assert not frontier.componentwise_lt(a, same)


def test_incomparable_points_are_not_ordered():
    """The whole reason for a componentwise order: (1,3) and (3,1) do not compare."""
    a = member("a", {"N": 1, "M": 3})
    b = member("b", {"N": 3, "M": 1})
    assert not frontier.componentwise_lt(a, b)
    assert not frontier.componentwise_lt(b, a)
    assert not frontier.componentwise_le(a, b)


def test_minimal_and_maximal_members_use_the_right_quantifier():
    points = [
        member("low", {"N": 1, "M": 1}),
        member("mid", {"N": 2, "M": 2}),
        member("side", {"N": 1, "M": 5}),
        member("top", {"N": 3, "M": 5}),
    ]
    minimal = {m.metadata.instance for m in frontier.minimal_members(points)}
    maximal = {m.metadata.instance for m in frontier.maximal_members(points)}
    # low is below everything comparable to it; side is not above low? it is (1,5)>=(1,1)
    assert "low" in minimal
    assert "mid" not in minimal and "top" not in minimal
    assert "top" in maximal
    assert "low" not in maximal


def test_two_incomparable_points_are_both_minimal_and_both_maximal():
    points = [member("a", {"N": 1, "M": 3}), member("b", {"N": 3, "M": 1})]
    assert len(frontier.minimal_members(points)) == 2
    assert len(frontier.maximal_members(points)) == 2


# --------------------------------------------------------------------------
# Staged-input strictness: the script must refuse data it cannot trust.

def write_tsv(path, header, rows):
    with open(path, "w", newline="") as handle:
        writer = csv.writer(handle, delimiter="\t", lineterminator="\n")
        writer.writerow(header)
        writer.writerows(rows)


RUN_HEADER = ["solver_label", "instance", "tlsf_file", "cap_s", "result", "seconds"]


def metadata(instance, tlsf_file="instance.tlsf", family_key="family"):
    return frontier.InstanceMetadata(
        instance=instance,
        tlsf_file=tlsf_file,
        origin_kind="generated",
        family_key=family_key,
        family_display="Family",
        parameter_confidence="exact",
        parameter_names_text="N",
        parameter_names=("N",),
        parameter_values_text='{"N":1}',
        parameter_values={"N": 1},
        parameter_dimension=1,
        parameters_numeric=True,
        coordinates=(Decimal("1"),),
        tlsf_bytes_text="100",
    )


def observation(result, cap="1", seconds="0.1"):
    return frontier.Observation(
        cap=Decimal(cap),
        result=result,
        seconds=Decimal(seconds),
        seconds_text=seconds,
    )


def test_opposite_decisive_verdicts_are_a_correctness_failure(tmp_path):
    point = metadata("case.ltl")
    specs = [
        frontier.RunSpec("B", tmp_path / "B.tsv"),
        frontier.RunSpec("S", tmp_path / "S.tsv"),
    ]
    runs = {
        "B": {point.instance: (observation("REALIZABLE"),)},
        "S": {point.instance: (observation("UNREALIZABLE", cap="5"),)},
    }

    with pytest.raises(frontier.CorrectnessFailure) as raised:
        frontier.find_correctness_failures({point.instance: point}, specs, runs)

    message = str(raised.value)
    assert "correctness failure: case.ltl" in message
    assert "REALIZABLE (B@1)" in message
    assert "UNREALIZABLE (S@5)" in message


@pytest.mark.parametrize(
    ("left", "right"),
    [("REALIZABLE", "REALIZABLE"), ("REALIZABLE", "TIMEOUT")],
)
def test_agreement_or_an_undecided_label_is_not_a_correctness_failure(
    tmp_path, left, right
):
    point = metadata("case.ltl")
    specs = [
        frontier.RunSpec("B", tmp_path / "B.tsv"),
        frontier.RunSpec("S", tmp_path / "S.tsv"),
    ]
    runs = {
        "B": {point.instance: (observation(left),)},
        "S": {point.instance: (observation(right),)},
    }

    assert (
        frontier.find_correctness_failures({point.instance: point}, specs, runs)
        is None
    )


def test_rows_are_only_required_until_a_cap_decides(tmp_path):
    """An instance solved at 1 s legitimately has no 5 s row.

    A naive "every instance at every cap" check would reject every complete
    staged run, so this is the property that makes the strictness usable.
    """
    runs = tmp_path / "runs.tsv"
    write_tsv(runs, RUN_HEADER, [
        ["B", "solved.ltl", "s.tlsf", "1", "REALIZABLE", "0.1"],
        ["B", "hard.ltl", "h.tlsf", "1", "TIMEOUT", "1.0"],
        ["B", "hard.ltl", "h.tlsf", "5", "REALIZABLE", "2.0"],
    ])
    points = {
        "solved.ltl": metadata("solved.ltl", "s.tlsf"),
        "hard.ltl": metadata("hard.ltl", "h.tlsf"),
    }

    loaded = frontier.load_run(
        frontier.RunSpec("B", runs), points, (Decimal("1"), Decimal("5"))
    )

    assert [row.cap for row in loaded["solved.ltl"]] == [Decimal("1")]
    assert [row.cap for row in loaded["hard.ltl"]] == [
        Decimal("1"),
        Decimal("5"),
    ]


def test_missing_row_for_an_undecided_instance_is_fatal(tmp_path):
    runs = tmp_path / "runs.tsv"
    write_tsv(runs, RUN_HEADER, [
        ["B", "hard.ltl", "h.tlsf", "1", "TIMEOUT", "1.0"],
    ])
    with pytest.raises(ValueError, match="missing"):
        frontier.load_run(
            frontier.RunSpec("B", runs),
            {"hard.ltl": metadata("hard.ltl", "h.tlsf")},
            (Decimal("1"), Decimal("5")),
        )
