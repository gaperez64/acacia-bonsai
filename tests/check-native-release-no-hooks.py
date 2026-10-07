#!/usr/bin/env python3
"""Release binaries must never carry the native fault injection environment names."""
from pathlib import Path
import sys

binary = Path(sys.argv[1]).read_bytes()
for marker in (b"ACACIA_NATIVE_TEST_", b"ACACIA_TEST_", b"tlsf_gr1_lift_test_",
               b"tlsf_gr1_both_test_", b"tlsf_gr1_env_test_", b"tlsf_gr1_env_rank_test_", b"reduction_test_",
               b"contract_original_validate_game", b"typed_test_ambiguous_linkage"):
    assert marker not in binary, marker
print("release binary contains no native test hook")
