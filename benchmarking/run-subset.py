#!/usr/bin/env python3
"""Compatibility entry point; implementation lives in benchmarking/run-syntcomp26-coverage.py."""
import pathlib
import sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from benchlib import load_tool

_owner = load_tool('benchmarking/run-syntcomp26-coverage.py')
DEFAULT_SOURCE_MAP = _owner.subset_DEFAULT_SOURCE_MAP
ROOT = _owner.subset_ROOT
RawObservations = _owner.subset_RawObservations
_parse_tlsf_semantics = _owner.subset__parse_tlsf_semantics
build_command = _owner.subset_build_command
convert_tlsf = _owner.subset_convert_tlsf
coverage_module = _owner.subset_coverage_module
main = _owner.subset_main
read_adhoc_tlsf_map = _owner.subset_read_adhoc_tlsf_map
read_instance_list = _owner.subset_read_instance_list
read_ltl_partition = _owner.subset_read_ltl_partition
read_semantics_map = _owner.subset_read_semantics_map
read_tlsf_map = _owner.subset_read_tlsf_map


def __getattr__(name):
    return getattr(_owner, name)

if __name__ == "__main__":
    raise SystemExit(main())
