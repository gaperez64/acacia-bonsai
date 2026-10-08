#!/usr/bin/env python3
"""Compile the backward solver with registry-selected tree downsets."""

from __future__ import annotations

import json
from pathlib import Path
import shlex
import subprocess
import sys


def main():
    root = Path(__file__).resolve().parents[1]
    build = Path(sys.argv[1]).resolve()
    registry = [sys.executable, str(root / 'scripts/acacia-config.py')]
    presets = subprocess.check_output(registry + ['list-group', 'posets_downset_sweep'],
                                      text=True).splitlines()
    commands = json.loads((build / 'compile_commands.json').read_text())
    entry = next(c for c in commands if c['file'].endswith('/solver/solve_game_vector.cc'))
    arguments = iter(shlex.split(entry['command']))
    command = [next(arguments)]
    for argument in arguments:
        if argument in ('-o', '-MQ', '-MF'):
            next(arguments)
        elif argument not in ('-MD', '-c', '-O2', '-O3', '-g'):
            command.append(argument)
    command += ['-fsyntax-only', '-fdiagnostics-color=never']
    checked = set()
    failures = []
    for preset in presets:
        options = json.loads(subprocess.check_output(registry + ['show', preset], text=True))
        backend = options['vector_downset']
        if backend not in {'bboxtree_backed', 'kdtree_backed'} or backend in checked:
            continue
        assert options['enable_equivariant_solver'], preset
        flags = [f'-DVECTOR_AND_BITSET_DOWNSET_IMPL={backend}',
                 f'-DPOSETS_ENABLE_DOWNSET_{backend.upper()}=1']
        result = subprocess.run(command + flags, cwd=entry['directory'], check=False)
        checked.add(backend)
        if result.returncode:
            failures.append(preset)
        print(f'{preset}: return code {result.returncode}', flush=True)
    assert not failures, failures
    assert checked == {'bboxtree_backed', 'kdtree_backed'}, checked


if __name__ == '__main__':
    main()
