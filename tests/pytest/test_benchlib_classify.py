import itertools
import pathlib
import sys

import pytest


ROOT = pathlib.Path(__file__).resolve().parents[2]
BENCHMARKING = ROOT / "benchmarking"

sys.path.insert(0, str(BENCHMARKING))
try:
    from benchlib import (
        RunResult,
        classify_run,
        parse_acacia_result,
        realizability_from_output,
        verdict_from_output,
    )
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


@pytest.mark.parametrize(
    ("output", "expected"),
    (
        ("REALIZABLE", "REALIZABLE"),
        ("UNREALIZABLE", "UNREALIZABLE"),
        ("TIMEOUT", "TIMEOUT"),
        ("nothing here", "UNKNOWN"),
        ("REALIZABLE and UNREALIZABLE", "UNKNOWN"),
        ("unknown", "UNKNOWN"),
        ("", "UNKNOWN"),
        ("REALIZABLE\n", "REALIZABLE"),
        ("REALIZABLE\nnote: could not prove unrealizable\n", "REALIZABLE"),
        ("REALIZABLE\nnote: could not prove UNREALIZABLE\n", "REALIZABLE"),
        ("[real=small,backend=backward] UNREALIZABLE\n", "UNREALIZABLE"),
        ("REALIZABLE\nUNREALIZABLE\n", "UNKNOWN"),
        ("TIMEOUT\n", "TIMEOUT"),
        ("diagnostic\n \tTIMEOUT \t\n", "TIMEOUT"),
        ("timeout\n", "TIMEOUT"),
        ("note: solver reached its timeout\n", "UNKNOWN"),
        ("note: solver reached its TIMEOUT\n", "UNKNOWN"),
        ("TIMEOUT while waiting\n", "UNKNOWN"),
        ("REALIZABLE\nTIMEOUT\n", "REALIZABLE"),
    ),
)
def test_parse_acacia_result(output, expected):
    assert parse_acacia_result(output) == expected


@pytest.mark.parametrize("on_conflict", ("last", "raise"))
@pytest.mark.parametrize(
    "output",
    (
        None,
        "",
        "nothing here",
        "REALIZABLE and UNREALIZABLE\n",
        "note: could not prove UNREALIZABLE\n",
        "REALIZABLE: diagnostic only\n",
        "NOTREALIZABLE\n",
    ),
)
def test_verdict_from_output_requires_a_verdict_line(output, on_conflict):
    assert verdict_from_output(output, on_conflict=on_conflict) is None
    assert realizability_from_output(output) is None


@pytest.mark.parametrize("on_conflict", ("last", "raise"))
@pytest.mark.parametrize("verdict", ("REALIZABLE", "UNREALIZABLE"))
def test_repeated_verdict_lines_agree(verdict, on_conflict):
    output = f"{verdict}\n[real=small,backend=backward] {verdict}\n"

    assert verdict_from_output(output, on_conflict=on_conflict) == verdict
    assert realizability_from_output(output) == verdict


@pytest.mark.parametrize(
    "output",
    ("REALIZABLE\nUNREALIZABLE\n", "UNREALIZABLE\nREALIZABLE\n"),
)
def test_conflicting_verdict_lines(output):
    assert verdict_from_output(output) is None
    assert verdict_from_output(output, on_conflict="last") is None
    assert realizability_from_output(output) is None
    with pytest.raises(ValueError) as error:
        verdict_from_output(output, on_conflict="raise")
    assert str(error.value) == (
        "conflicting printed verdicts: ['REALIZABLE', 'UNREALIZABLE']"
    )


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


@pytest.mark.parametrize("tool", TOOLS)
def test_diagnostic_cannot_flip_a_printed_verdict(tool):
    run = make_run("REALIZABLE\nnote: could not prove unrealizable\n", 0)

    assert classify_run(run, tool) == "REALIZABLE"


@pytest.mark.parametrize("tool", TOOLS)
def test_printed_timeout_without_timeout_flag_is_error(tool):
    for returncode in EXIT_CODES:
        assert classify_run(make_run(" \tTIMEOUT \n", returncode), tool) == "ERROR"


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
        ("REALIZABLE and UNREALIZABLE", 2, False, False): ("UNKNOWN", "ERROR"),
        ("REALIZABLE and UNREALIZABLE", 3, False, False): ("ERROR", "UNKNOWN"),
        ("unknown", 2, False, False): ("UNKNOWN", "ERROR"),
        ("unknown", 3, False, False): ("ERROR", "UNKNOWN"),
    }


def test_unknown_tool_raises_clear_error():
    with pytest.raises(ValueError, match="unknown tool: 'missing'"):
        classify_run(make_run(), "missing")
