import itertools
import pathlib
import sys

import pytest


ROOT = pathlib.Path(__file__).resolve().parents[2]
BENCHMARKING = ROOT / "benchmarking"

sys.path.insert(0, str(BENCHMARKING))
try:
    from benchlib import RunResult, classify_run
finally:
    sys.path.pop(0)


TOOLS = ("acacia", "acacia1x", "ltlsynt")
OUTPUT_TEXTS = (
    "REALIZABLE",
    "UNREALIZABLE",
    "TIMEOUT",
    "nothing here",
    "REALIZABLE and UNREALIZABLE",
    "unknown",
)
EXIT_CODES = (0, 1, 2, 3, 124, -9)


def make_run(
    output="nothing here", returncode=0, *, timed_out=False, resource_limited=False
):
    return RunResult(
        stdout=output,
        stderr="",
        returncode=returncode,
        seconds=0.0,
        timed_out=timed_out,
        resource_limited=resource_limited,
    )


@pytest.mark.parametrize("tool", TOOLS)
def test_timed_out_dominates_everything(tool):
    for output, returncode, resource_limited in itertools.product(
        OUTPUT_TEXTS, EXIT_CODES, (False, True)
    ):
        run = make_run(
            output,
            returncode,
            timed_out=True,
            resource_limited=resource_limited,
        )

        assert classify_run(run, tool) == "TIMEOUT"


@pytest.mark.parametrize("tool", TOOLS)
def test_resource_limited_dominates_when_not_timed_out(tool):
    for output, returncode in itertools.product(OUTPUT_TEXTS, EXIT_CODES):
        run = make_run(output, returncode, resource_limited=True)

        assert classify_run(run, tool) == "RESOURCE_LIMIT"


@pytest.mark.parametrize(
    ("tool", "verdict", "returncode"),
    (
        ("acacia", "REALIZABLE", 0),
        ("acacia", "UNREALIZABLE", 1),
        ("acacia", "UNKNOWN", 2),
        ("acacia1x", "REALIZABLE", 0),
        ("acacia1x", "UNREALIZABLE", 1),
        ("acacia1x", "UNKNOWN", 3),
        ("ltlsynt", "REALIZABLE", 0),
        ("ltlsynt", "UNREALIZABLE", 1),
        ("ltlsynt", "UNKNOWN", 2),
    ),
)
def test_matching_verdict_and_exit_code_is_accepted(tool, verdict, returncode):
    assert classify_run(make_run(verdict, returncode), tool) == verdict


@pytest.mark.parametrize("tool", TOOLS)
def test_mismatched_exit_code_is_error(tool):
    assert classify_run(make_run("REALIZABLE", 1), tool) == "ERROR"


def test_tool_relationships_over_exhaustive_grid():
    differences = {}

    for output, returncode, timed_out, resource_limited in itertools.product(
        OUTPUT_TEXTS, EXIT_CODES, (False, True), (False, True)
    ):
        run = make_run(
            output,
            returncode,
            timed_out=timed_out,
            resource_limited=resource_limited,
        )
        acacia = classify_run(run, "acacia")
        acacia1x = classify_run(run, "acacia1x")

        assert classify_run(run, "ltlsynt") == acacia
        if acacia1x != acacia:
            differences[(output, returncode, timed_out, resource_limited)] = (
                acacia,
                acacia1x,
            )

    assert differences == {
        ("nothing here", 2, False, False): ("UNKNOWN", "ERROR"),
        ("nothing here", 3, False, False): ("ERROR", "UNKNOWN"),
        ("unknown", 2, False, False): ("UNKNOWN", "ERROR"),
        ("unknown", 3, False, False): ("ERROR", "UNKNOWN"),
    }


def test_unknown_tool_raises_clear_error():
    with pytest.raises(ValueError, match="unknown tool: 'missing'"):
        classify_run(make_run(), "missing")
