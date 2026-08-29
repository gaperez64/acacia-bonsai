"""The frozen G2s panel must stay resolvable.

The solver-profile gate reads its ten targets from a frozen TSV and resolves
each through a suite source map.  When syntcomp25 moved to the reconstructed
TLSF corpus, two of those targets stopped existing in `sources.tsv` and the
gate began aborting partway through -- after burning most of an hour on the
targets it could still resolve.  Nothing caught it, because no test ever asked
whether the frozen panel still resolves.
"""

import csv
import pathlib

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
BENCHMARKS = ROOT / "tests/suites/benchmarks"
PROFILE = BENCHMARKS / "solver-profile.tsv"


def read_map(path):
    if not path.exists():
        return {}
    with path.open(newline="", encoding="utf-8") as handle:
        return {row[0]: row[1] for row in csv.reader(handle, delimiter="\t")
                if len(row) >= 2 and row[0] != "instance" and row[0] != "suite"}


def profile_targets():
    with PROFILE.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


def test_the_panel_is_exactly_ten_frozen_targets():
    targets = profile_targets()
    assert len(targets) == 10
    assert {t["fixpoint_bucket"] for t in targets} <= {
        "downset-bound", "letter-loop-bound", "mixed"
    }


@pytest.mark.parametrize("target", profile_targets(),
                         ids=lambda t: f"{t['suite']}/{t['instance']}")
def test_every_target_resolves_through_one_of_the_two_source_maps(target):
    suite, instance = target["suite"], target["instance"]
    ltl_map = read_map(BENCHMARKS / suite / "sources.tsv")
    tlsf_map = read_map(BENCHMARKS / suite / "tlsf-sources.tsv")

    assert instance in ltl_map or instance in tlsf_map, (
        f"{suite}/{instance} is in neither sources.tsv nor tlsf-sources.tsv, "
        "so the solver-profile gate cannot run it"
    )

    if instance in ltl_map:
        ltl = ROOT / "tests/ltl" / ltl_map[instance]
        assert ltl.exists(), f"{ltl} is missing"
        assert ltl.with_suffix(".part").exists(), f"{ltl} has no .part"
    else:
        # TLSF sources are resolved against a materialized corpus that is not in
        # the tree, so only the mapping itself is checkable here.
        assert tlsf_map[instance].endswith(".tlsf")


def test_at_least_one_target_needs_the_tlsf_route():
    """If this ever fails, the fallback in solver-profile-gate.sh is untested by
    the panel and can rot unnoticed."""
    needs_tlsf = []
    for target in profile_targets():
        suite, instance = target["suite"], target["instance"]
        if instance not in read_map(BENCHMARKS / suite / "sources.tsv"):
            needs_tlsf.append(f"{suite}/{instance}")
    assert needs_tlsf
