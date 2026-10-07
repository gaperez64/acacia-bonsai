#!/usr/bin/env python3
"""Generated near-threshold native guard test and lifecycle attribution."""
import json
import os
import re
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def main():
    binary, build = map(lambda p: Path(p).resolve(), sys.argv[1:])
    arm = 'both:gr1-real-lift:oxidd'
    # A controlled output under 17 next operators crosses only the global
    # temporal-depth guard (16); its deterministic safety monitor is small.
    depth = 17
    source = ('INFO { TITLE: "generated boundary" SEMANTICS: Mealy TARGET: Mealy }\n'
              'MAIN { OUTPUTS { response; } GUARANTEES { ' + 'X ' * depth + 'response; } }\n')
    with tempfile.TemporaryDirectory(dir=build) as directory:
        root = Path(directory)
        path = root / 'boundary.tlsf'
        path.write_text(source)
        outcomes = []
        for scale in (None, '1', '2', '4', 'off'):
            records = root / ('default' if scale is None else scale)
            records.mkdir()
            env = os.environ.copy()
            env['ACACIA_OUTER_DEADLINE_MONOTONIC'] = str(time.monotonic() + 40)
            env['ACACIA_PHASE_RECORDS'] = str(records)
            flags = [] if scale is None else ['--native-structure-guard-scale', scale]
            process = subprocess.run([str(binary), '-T', str(path), '--arms', arm, *flags],
                                     env=env, capture_output=True, text=True, timeout=45)
            rows = [json.loads(line) for file in records.glob('*.jsonl')
                    for line in file.read_text().splitlines()]
            events = [row for row in rows if row.get('event') and 'requested_backend' in row]
            expected = 'off' if scale == 'off' else float(scale or 1)
            assert events and all(row['native_structure_guard_scale'] == expected
                                  for row in events), (scale, events)
            assert {'worker_start', 'parent_terminal'} <= {r['event'] for r in events}
            if scale in (None, '1'):
                assert process.returncode == 2, (scale, process.stdout, process.stderr)
                stop = next(r for r in rows if r.get('phase') == 'budget_decline')
                assert stop['stage'] == 'budget-structure'
                assert stop['max_temporal_depth'] == depth
                assert stop['max_conjunct_nodes'] == depth + 1
                assert stop['formula_nodes'] < 4096 and stop['ap_count'] < 2048
                assert stop['monitors'] == stop['states'] == stop['edges'] == 0
                outcomes.append((process.returncode, process.stdout,
                                 re.sub(r'peak_rss_bytes=\d+', 'peak_rss_bytes=<sample>',
                                        process.stderr)))
            else:
                assert process.returncode == 0 and process.stdout.strip() == 'REALIZABLE', (
                    scale, process.stdout, process.stderr)
                assert any(r.get('event') == 'verification_complete' for r in rows)
                assert not any(r.get('stage') == 'budget-structure' for r in events)
        assert outcomes[0] == outcomes[1]
        for bad in ('0', '-1', 'nan', 'inf', '1junk', '', '1e999', '1e-999'):
            process = subprocess.run([str(binary), '--native-structure-guard-scale', bad],
                                     capture_output=True, text=True, timeout=5)
            assert process.returncode == 3 and 'finite positive factor or off' in process.stderr


if __name__ == '__main__':
    main()
