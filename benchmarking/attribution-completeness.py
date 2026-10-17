#!/usr/bin/env python3
"""Compatibility entry point; implementation lives in benchmarking/legacy-phase-table.py."""
import pathlib
import sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from benchlib import load_tool

_owner = load_tool('benchmarking/legacy-phase-table.py')
BOOLEAN_CONTEXT = _owner.completeness_BOOLEAN_CONTEXT
CONFIG = _owner.completeness_CONFIG
CONTEXT = _owner.completeness_CONTEXT
DIAG = _owner.completeness_DIAG
MAX_RECORD_FILE_BYTES = _owner.completeness_MAX_RECORD_FILE_BYTES
PARENT_EVENTS = _owner.completeness_PARENT_EVENTS
ROOT = _owner.completeness_ROOT
SOLVED = _owner.completeness_SOLVED
SPEC_FIELDS = _owner.completeness_SPEC_FIELDS
WORKER = _owner.completeness_WORKER
audit_campaign = _owner.completeness_audit_campaign
audit_row = _owner.completeness_audit_row
expected_configuration = _owner.completeness_expected_configuration
expected_worker_count = _owner.completeness_expected_worker_count
flag_value = _owner.completeness_flag_value
load_module = _owner.completeness_load_module
location = _owner.completeness_location
main = _owner.completeness_main
make_report = _owner.completeness_make_report
nonnegative_int = _owner.completeness_nonnegative_int
phase_directory = _owner.completeness_phase_directory
project = _owner.completeness_project
summarize = _owner.completeness_summarize
write_tsv = _owner.completeness_write_tsv


def __getattr__(name):
    return getattr(_owner, name)

if __name__ == "__main__":
    raise SystemExit(main())
