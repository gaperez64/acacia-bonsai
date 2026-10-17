#!/usr/bin/env python3
"""Compatibility entry point for the shared native conversion traversal."""
import pathlib
import sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from benchlib import load_tool

_owner = load_tool("benchmarking/convert-tlsf-corpus.py")
selected_sources = _owner.selected_sources
inspect = _owner.native_inspect
sha256 = _owner.sha256
main = _owner.native_main

if __name__ == "__main__":
    raise SystemExit(main())
