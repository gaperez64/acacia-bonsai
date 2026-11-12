#!/usr/bin/env python3
"""Compile the backward solver with registry-selected downsets."""

from __future__ import annotations

import json
from pathlib import Path
import shlex
import subprocess
import sys


def main():
    root = Path(__file__).resolve().parents[1]
    build = Path(sys.argv[1]).resolve()
    option = json.loads((root / 'config/acacia-options.json').read_text())['options']['vector_downset']
    types = option['macros'][0]['map']
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
    failures = []
    for backend in option['choices']:
        downset_type = types[backend]
        flags = [f'-DVECTOR_AND_BITSET_DOWNSET_IMPL={downset_type}',
                 f'-DPOSETS_ENABLE_DOWNSET_{downset_type.upper()}=1']
        result = subprocess.run(command + flags, cwd=entry['directory'], check=False)
        if result.returncode:
            failures.append(backend)
        print(f'{backend}: return code {result.returncode}', flush=True)
    assert not failures, failures


if __name__ == '__main__':
    main()
