#!/usr/bin/env python3
"""Release binaries must never carry the native fault injection environment names."""
from pathlib import Path
import sys

binary = Path(sys.argv[1]).read_bytes()
assert b"ACACIA_NATIVE_TEST_" not in binary
print("release binary contains no native test hook")
