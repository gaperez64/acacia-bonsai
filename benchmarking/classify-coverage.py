#!/usr/bin/env python3
"""Compatibility entry point; implementation lives in benchmarking/cactus-report.py."""
import pathlib
import sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from benchlib import load_tool

_owner = load_tool('benchmarking/cactus-report.py')
HERE = _owner.classification_HERE
SOLVED = _owner.classification_SOLVED
UNKNOWN = _owner.classification_UNKNOWN
arm_attribution = _owner.classification_arm_attribution
classify = _owner.classification_classify
evidence_path = _owner.classification_evidence_path
join = _owner.classification_join
load_evidence = _owner.classification_load_evidence
load_probes = _owner.classification_load_probes
load_tool = _owner.classification_load_tool
main = _owner.classification_main
metadata = _owner.classification_metadata
observed_polarity = _owner.classification_observed_polarity
phase_attribution = _owner.classification_phase_attribution
requested_arms = _owner.classification_requested_arms
standalone_boundary = _owner.classification_standalone_boundary
stop_category = _owner.classification_stop_category
structural_sample = _owner.classification_structural_sample
table_rows = _owner.classification_table_rows
write_tsv = _owner.classification_write_tsv


def __getattr__(name):
    return getattr(_owner, name)

if __name__ == "__main__":
    raise SystemExit(main())
