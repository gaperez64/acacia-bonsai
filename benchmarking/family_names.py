#!/usr/bin/env python3
"""Compatibility entry point; implementation lives in benchmarking/make-panel.py."""
import pathlib
import sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from benchlib import load_tool

_owner = load_tool('benchmarking/make-panel.py')
family_of = _owner.family_of



def __getattr__(name):
    return getattr(_owner, name)
