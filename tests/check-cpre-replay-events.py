#!/usr/bin/env python3
"""Check offline replay parser declines and untimed output on synthetic events."""

from pathlib import Path
import subprocess
import sys
import tempfile

binary, scratch = sys.argv[1:]
with tempfile.TemporaryDirectory(dir=scratch) as temporary:
    root = Path(temporary)
    (root / 'meta.tsv').write_text('schema_version\tstates\tbool_threshold\n3\t2\t1\n')
    event = ('# schema_version=2 loop=0 k=2 before=1 actions=1\n'
             '[before]\n1\t0\n[actions]\naction\t0\n0\t0\t0\n1\t1\t0\n'
             '[after]\t1\n1\t0\n')
    path = root / 'cpre-0.tsv'
    path.write_text(event)
    result = subprocess.run([binary, '--dir', str(root)], text=True, capture_output=True)
    assert result.returncode == 0, result.stderr
    lines = result.stdout.splitlines()
    header, row = lines[0].split('\t'), lines[1].split('\t')
    output = dict(zip(header, row, strict=True))
    assert output['exact'] == 'yes' and output['measured'] == 'no'
    assert output['model'] == 'production-mode'
    assert all(float(output[key]) == 0 for key in header if key.endswith('_ms'))
    for invalid in [event.split('[after]')[0], event.replace('[after]\t1', '[after]\t2'),
                    event.replace('0\t0\t0', '0\t2\t0'),
                    event.replace('1\t0\n[actions]', '128\t0\n[actions]'),
                    event.replace('1\t0\n[actions]', '-2\t0\n[actions]')]:
        path.write_text(invalid)
        result = subprocess.run([binary, '--dir', str(root)], text=True, capture_output=True)
        assert result.returncode != 0, invalid
