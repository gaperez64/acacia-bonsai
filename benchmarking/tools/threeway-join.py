#!/usr/bin/env python3
"""Compatibility entry point; implementation lives in benchmarking/cactus-report.py."""
import pathlib
import sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "benchmarking"))
from benchlib import load_tool

_owner = load_tool('benchmarking/cactus-report.py')
CACTUS_RESULT = _owner.threeway_CACTUS_RESULT
IDENTITY_FIELDS = _owner.threeway_IDENTITY_FIELDS
JoinError = _owner.threeway_JoinError
ROOT = _owner.threeway_ROOT
SOLVED = _owner.threeway_SOLVED
SUBTYPES = _owner.threeway_SUBTYPES
UNTIMED_CONVERSION_TOOLS = _owner.threeway_UNTIMED_CONVERSION_TOOLS
_load = _owner.threeway__load
audit_solved_verdicts = _owner.threeway_audit_solved_verdicts
build_parser = _owner.threeway_build_parser
cactus = _owner.threeway_cactus
cactus_rows = _owner.threeway_cactus_rows
campaign = _owner.threeway_campaign
conversion_boundary = _owner.threeway_conversion_boundary
corpus_digest = _owner.threeway_corpus_digest
coverage = _owner.threeway_coverage
expected_verdicts = _owner.threeway_expected_verdicts
is_signal_crash = _owner.threeway_is_signal_crash
join = _owner.threeway_join
load_series = _owner.threeway_load_series
main = _owner.threeway_main
match_manifests = _owner.threeway_match_manifests
peak_memory = _owner.threeway_peak_memory
render = _owner.threeway_render
row_source = _owner.threeway_row_source
score = _owner.threeway_score
sha256_file = _owner.threeway_sha256_file
slug = _owner.threeway_slug
split_assignment = _owner.threeway_split_assignment
write_outputs = _owner.threeway_write_outputs


def __getattr__(name):
    return getattr(_owner, name)

if __name__ == "__main__":
    raise SystemExit(main())
