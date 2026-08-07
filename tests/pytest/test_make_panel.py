from __future__ import annotations

import importlib.util
import json
import pathlib
import sys


BENCHMARKING = pathlib.Path(__file__).resolve().parents[2] / "benchmarking"
SCRIPT = BENCHMARKING / "make-panel.py"


def load_make_panel():
    sys.path.insert(0, str(BENCHMARKING))
    try:
        spec = importlib.util.spec_from_file_location("make_panel", SCRIPT)
        module = importlib.util.module_from_spec(spec)
        assert spec.loader is not None
        sys.modules[spec.name] = module
        spec.loader.exec_module(module)
        return module
    finally:
        sys.path.pop(0)


def test_standalone_verdict_rejects_harness_prose_and_unknown():
    module = load_make_panel()

    assert module.standalone_verdict("formula is realizable.\nPASS.\n") is None
    assert module.standalone_verdict("UNKNOWN\nPASS.\n") is None
    assert module.standalone_verdict("[x.ltl] REALIZABLE\n[x.ltl] PASS.\n") == "REALIZABLE"
    assert module.standalone_verdict("UNREALIZABLE\n") == "UNREALIZABLE"


def test_family_numeric_suffix_stripping_is_linear_and_stable():
    module = load_make_panel()

    assert module.family_of("family_12_34.ltl") == "family"
    assert module.family_of("family-12_34.ltl") == "family"
    assert module.family_of("family12__34.ltl") == "family12"
    assert module.family_of("family123__.ltl") == "family123"
    assert module.family_of("999999.ltl") == "numeric"

    digits = "9" * 100_000
    assert module.family_of(f"family_{digits}.ltl") == "family"
    assert module.family_of(f"family_{digits}x.ltl") == f"family_{digits}x"


def test_family_round_robin_is_deterministic_and_balanced():
    module = load_make_panel()
    pool = []
    for family, count in (("large", 20), ("medium", 5), ("small", 3)):
        for index in range(count):
            pool.append(
                module.Candidate(
                    f"{family}{index}.ltl",
                    "easy",
                    0.1,
                    0.1,
                    "REALIZABLE",
                    family,
                    "reference",
                )
            )

    first = module.family_round_robin(pool, 9, 7)
    second = module.family_round_robin(pool, 9, 7)
    assert first == second
    counts = {family: sum(row.family == family for row in first) for family in ("large", "medium", "small")}
    assert counts == {"large": 3, "medium": 3, "small": 3}


def test_load_tool_filters_duplicate_basenames_by_corpus(tmp_path):
    module = load_make_panel()
    corpus = tmp_path / "syntcomp24"
    other = tmp_path / "syntcomp21"
    corpus.mkdir()
    other.mkdir()
    rows = [
        {
            "command": ["wrapper", "-F", str(corpus / "same.ltl")],
            "result": "OK",
            "duration": 0.2,
            "stdout": "REALIZABLE\n",
        },
        {
            "command": ["wrapper", "-F", str(other / "same.ltl")],
            "result": "TIMEOUT",
            "duration": 17.0,
            "stdout": "",
        },
    ]
    path = tmp_path / "combined.json"
    path.write_text("".join(f"{json.dumps(row)}\n" for row in rows))

    results = module.load_tool(path, 17.0, corpus)

    assert results == {
        "same.ltl": module.ToolResult(0.2, "REALIZABLE", True),
    }


def test_load_references_unions_coverage_and_latest_campaign_wins(tmp_path):
    module = load_make_panel()
    corpus = tmp_path / "corpus"
    corpus.mkdir()
    newest = tmp_path / "newest"
    older = tmp_path / "older"
    newest.mkdir()
    older.mkdir()

    def write_tool(reference, tool, rows):
        path = reference / f"{tool}.json"
        path.write_text(
            "".join(
                f"{json.dumps({'command': ['solver', '-F', str(corpus / name)], 'result': 'OK', 'duration': duration, 'stdout': verdict + chr(10)})}\n"
                for name, duration, verdict in rows
            )
        )

    write_tool(newest, "acacia", [("overlap.ltl", 2.0, "REALIZABLE"), ("new.ltl", 0.2, "REALIZABLE")])
    write_tool(newest, "ltlsynt", [("overlap.ltl", 0.1, "REALIZABLE"), ("new.ltl", 0.1, "REALIZABLE")])
    write_tool(older, "acacia", [("overlap.ltl", 0.2, "REALIZABLE"), ("old.ltl", 18.0, "UNREALIZABLE")])
    write_tool(older, "ltlsynt", [("overlap.ltl", 0.1, "REALIZABLE"), ("old.ltl", 0.1, "UNREALIZABLE")])

    candidates, observed = module.load_references(
        [newest, older], "acacia", "ltlsynt", 17.0, corpus
    )

    assert observed == {"new.ltl", "old.ltl", "overlap.ltl"}
    assert set(candidates) == observed
    assert candidates["overlap.ltl"].source_campaign == "newest"
    assert candidates["overlap.ltl"].stratum == "border"
    assert candidates["old.ltl"].source_campaign == "older"
    assert candidates["old.ltl"].stratum == "gap"
