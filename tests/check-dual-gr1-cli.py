#!/usr/bin/env python3
"""Reduction-only attribution and unchanged off-path bytes through native workers."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def main():
    binary, build = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    incumbent = Path(sys.argv[3]).resolve() if len(sys.argv) > 3 else None
    with tempfile.TemporaryDirectory(dir=build, prefix='dual-cli-') as temporary:
        root = Path(temporary)
        counter = 0

        def run(source, flags, telemetry=False, executable=binary):
            nonlocal counter
            directory = root / str(counter)
            counter += 1
            directory.mkdir()
            env = os.environ.copy()
            env['ACACIA_OUTER_DEADLINE_MONOTONIC'] = str(time.monotonic() + 20)
            env.pop('ACACIA_PHASE_RECORDS', None)
            env['LD_LIBRARY_PATH'] = str(build / 'subprojects/tlsf-tools/subprojects/yyjson-0.12.0') + ':' + env.get('LD_LIBRARY_PATH', '')
            if telemetry:
                env['ACACIA_PHASE_RECORDS'] = str(directory)
            if source is not None:
                path = directory / 'source.tlsf'
                path.write_text(source)
                flags = ['-T', str(path), *flags]
            result = subprocess.run([str(executable), *flags], env=env,
                                    capture_output=True, timeout=25)
            rows = [json.loads(line) for file in directory.glob('*.jsonl')
                    for line in file.read_text().splitlines()]
            return (result.returncode, result.stdout, result.stderr), rows

        def source(body, assumptions=''):
            return f'''INFO {{ TITLE: "research" SEMANTICS: Mealy TARGET: Mealy }}
                MAIN {{ INPUTS {{ i; }} OUTPUTS {{ o; }} {assumptions}
                GUARANTEE {{ {body}; }} }}'''.replace('GF', 'G F').replace('FG', 'F G')

        accepted = source('FG o')
        rejected = source('(GF i) <-> (GF o)')
        real = source('G(o <-> i)')
        for arm in ('both:gr1:oxidd', 'both:gr1-lift:oxidd', 'both:gr1-real-lift:oxidd'):
            for spec, expected in ((accepted, 'accepted'), (rejected, 'rejected')):
                outcome, rows = run(spec, ['--arms', arm, '--dual-gr1', 'recognize'], True)
                assert outcome[0] == 2 and not outcome[1], outcome
                events = {r['event']: r for r in rows if r.get('event', '').startswith('dual_gr1_')}
                assert events['dual_gr1_original_rejection']['stage'] == 'mp-class', rows
                assert events['dual_gr1_original_rejection']['cause'] == 'decline', rows
                assert events['dual_gr1_construction']['outcome'] == 'constructed', rows
                reduction = events['dual_gr1_reduction']
                assert reduction['outcome'] == expected, rows
                assert reduction['cause'] == ('none' if expected == 'accepted' else 'decline'), rows
                assert not any(r.get('event') in ('verification_complete', 'winner_accepted',
                                                  'verification_start') for r in rows), rows
                phases = {r['phase']: r for r in rows if 'phase' in r}
                for phase in ('dual_load', 'dual_construct', 'dual_reduce'):
                    assert phases[phase]['wall_ns'] > 0 and phases[phase]['cpu_ns'] > 0, rows
                assert phases['dual_reduce']['monitor_count'] > 0 or expected == 'rejected'
                assert all(r.get('dropped_records', 0) == 0 for r in rows), rows
                assert all(r.get('failed_records', 0) == 0 for r in rows), rows
            plain, rows = run(real, ['--arms', arm, '--dual-gr1', 'recognize'], True)
            assert plain[0] == 0 and plain[1] == b'REALIZABLE\n', plain
            assert not any(r.get('event', '').startswith('dual_gr1_') for r in rows), rows

        # Default off, explicit off and the frozen clean build must agree in
        # stdout, stderr and exit status, including declines and unsupported games.
        specs = [accepted, rejected, real, source('G !o'),
                 source('GF o', 'ASSUME { GF i; }'),
                 accepted.replace('SEMANTICS: Mealy', 'SEMANTICS: Moore'),
                 accepted.replace('SEMANTICS: Mealy', 'SEMANTICS: Strict,Mealy')]
        for arm in ('both:gr1:oxidd', 'both:gr1-lift:oxidd', 'both:gr1-real-lift:oxidd'):
            for spec in specs:
                default, rows = run(spec, ['--arms', arm], True)
                off, _ = run(spec, ['--arms', arm, '--dual-gr1', 'off'])
                assert default == off, (arm, default, off)
                assert not any(r.get('event', '').startswith('dual_gr1_') for r in rows), rows
                if incumbent:
                    baseline, _ = run(spec, ['--arms', arm], executable=incumbent)
                    assert default == baseline, (arm, default, baseline)
        for flags in (['-f', 'G(o <-> i)', '-i', 'i', '-o', 'o', '--dual-gr1', 'recognize'],
                      ['--dual-gr1', 'solve', '-f', '1'],
                      ['--dual-gr1', 'recognize', '-f', '1', '-s', str(root / 'out.aag')]):
            result, _ = run(None, flags)
            assert result[0] != 0 and not result[1], result
        # Raw LTL and synthesis keep their incumbent behavior while off.
        for flags in (['-f', 'G(o <-> i)', '-i', 'i', '-o', 'o'],
                      ['-f', 'G(o <-> i)', '-i', 'i', '-o', 'o', '-s', str(root / 'out.aag')]):
            flags = [*flags, '--real-backend', 'backward', '--unreal-backend', 'backward']
            result, _ = run(None, flags)
            if incumbent:
                baseline, _ = run(None, flags, executable=incumbent)
                assert result == baseline, (result, baseline)
        print('dual CLI attribution, no-verdict boundary and default-off bytes passed')


if __name__ == '__main__':
    main()
