from __future__ import annotations

import csv
import importlib.util
import json
import os
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'benchmarking'))
from benchlib import load_phase_records  # noqa: E402


def module():
    spec = importlib.util.spec_from_file_location(
        'legacy_phase_table', ROOT / 'benchmarking/legacy-phase-table.py')
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def records(tmp_path, events):
    (tmp_path / '10.jsonl').write_text(''.join(json.dumps(e) + '\n' for e in events))
    return load_phase_records(tmp_path)


def event(kind, seq, time, stage_id=1, **kwargs):
    return dict(event=kind, worker_pid=10, worker=2, seq=seq, mono_ns=time,
                stage_id=stage_id, stage='translation', subjob=1, subjobs=2, k=None,
                run_id=0, dropped_records=0, **kwargs)


def test_completed_and_killed_stages_stay_separate(tmp_path):
    data = records(tmp_path, [event('stage_entry', 1, 100),
                             event('stage_completion', 2, 140, wall_ns=40, cpu_ns=20),
                             event('stage_entry', 3, 150, stage_id=2),
                             event('parent_terminal', 4, 200, stage_id=2, signal=9)])
    rows = module().build_rows('run', data)
    assert rows[0]['wall_ns'] == 40
    assert rows[1]['state'] == 'censored'
    assert rows[1]['censored_lower_ns'] == 50
    assert 'wall_ns' not in rows[1]
    assert 'states' not in rows[1]


def test_missing_packet_is_unknown_and_metrics_keep_stage_identity(tmp_path):
    data = records(tmp_path, [event('stage_entry', 1, 100),
                             event('stage_metric', 3, 120, key='states', value='7'),
                             event('parent_terminal', 4, 140, signal=9, stage_start_ns=100)])
    rows = module().build_rows('run', data)
    assert rows[0]['delivery'] == 'partial'
    assert rows[0]['states'] == '7'
    assert 'cpu_ns' not in rows[0]


def test_parent_snapshot_recovers_dropped_entry(tmp_path):
    data = records(tmp_path, [event('parent_terminal', 3, 140, signal=9, stage_start_ns=100)])
    row = module().build_rows('run', data)[0]
    assert row['entry_ns'] == 100
    assert row['state'] == 'censored'
    assert row['delivery'] == 'partial'


def test_summary_does_not_double_count_nested_stages(tmp_path):
    events = [event('stage_entry', 1, 100), event('stage_entry', 2, 110, stage_id=2),
              event('stage_completion', 3, 130, stage_id=2),
              event('stage_completion', 4, 150)]
    tool = module()
    summary = tool.worker_summaries(tool.build_rows('run', records(tmp_path, events)))[0]
    assert summary['observed_finished_union_ns'] == 50
    assert summary['exclusive_finished_wall_ns']['translation'] == 50


def test_capture_does_not_join_ambiguous_stage_occurrences():
    rows = [dict(invocation='run', worker_pid='10', run_id=0, k=2, stage='search', subjob=1,
                 entry_ns=100, completion_ns=150, state='completed') for i in (1, 2)]
    capture = dict(invocation='run', worker_pid='10', run_id='0', subjob='1', k='2',
                   status='WIN_K', stage='verified-attempt', search_steps='99')
    module().join_captures(rows, [capture])
    assert all(r['capture_join'] == 'ambiguous' and 'search_steps' not in r for r in rows)


def test_enclosing_stage_keeps_entry_identity(tmp_path):
    start = event('stage_entry', 1, 100)
    start.update(subjob=0, subjobs=None, k=None)
    finish = event('stage_completion', 2, 150)
    finish.update(subjob=8, subjobs=8, k=2)
    row = module().build_rows('run', records(tmp_path, [start, finish]))[0]
    assert row['subjob'] == 0 and row['k'] is None
    assert row['subjobs'] == 8


def test_writer_failure_marks_even_finished_rows_partial(tmp_path):
    events = [event('stage_entry', 1, 100), event('stage_completion', 2, 150),
              dict(event='writer_summary', failed_records=1, incomplete_packet=False)]
    row = module().build_rows('run', records(tmp_path, events))[0]
    assert row['state'] == 'completed' and row['delivery'] == 'partial'
    assert 'states' not in row


def test_dropped_translation_completion_before_search_kill(tmp_path):
    translation = event('stage_entry', 1, 100)
    search = {**event('stage_entry', 3, 130, stage_id=2), 'stage': 'search', 'k': 2}
    killed = {**event('stage_censored', 4, 210, stage_id=2, signal=9, entry_ns=130),
              'stage': 'search', 'k': 2}
    rows = module().build_rows('run', records(tmp_path, [translation, search, killed]))
    assert rows[0]['state'] == 'unknown-completion'
    assert 'censored_lower_ns' not in rows[0]
    assert rows[0]['delivery'] == 'partial'
    assert rows[1]['state'] == 'censored'
    assert rows[1]['censored_lower_ns'] == 80


def test_snapshot_and_entry_packet_have_distinct_clocks(tmp_path):
    translation = event('stage_entry', 1, 100)
    search = {**event('stage_entry', 2, 130, stage_id=2), 'stage': 'search', 'k': 2}
    snapshot = {**event('stage_censored', 3, 210, stage_id=2, signal=9, entry_ns=125),
                'stage': 'search', 'k': 2}
    terminal = dict(event='parent_terminal', worker_pid=10, worker=2, seq=4,
                    mono_ns=215, signal=9, reason='interrupted', dropped_records=0)
    rows = module().build_rows('run', records(tmp_path, [translation, search, snapshot,
                                                       terminal]))
    assert rows[0]['state'] == 'unknown-completion'
    assert 'censored_lower_ns' not in rows[0]
    assert rows[1]['state'] == 'censored'
    assert rows[1]['entry_ns'] == 130
    assert 'entry_source' not in rows[1]
    assert rows[1]['censored_lower_ns'] == 85
    assert rows[1]['delivery'] == 'unconfirmed'


def test_limited_capture_never_supplies_unentered_verification():
    tool = module()
    row = dict(invocation='run', worker_pid='10', subjob=1, run_id=0, k=2,
               stage='search', entry_ns=100, state='censored', capture_join='absent')
    capture = dict(stage='verified-attempt', worker_pid='10', k='2', status='RESOURCE_LIMIT',
                   search_steps='999', verification_rows_rebuilt='0', verify_queries='0')
    for attempt in (capture, {**capture, 'invocation': 'run', 'subjob': '1', 'run_id': '0'},
                    {**capture, 'invocation': 'run', 'subjob': '1', 'run_id': '0',
                     'status': 'WIN_K'}):
        tool.join_captures([row], [attempt])
        assert row['capture_join'] == 'absent'
        assert 'verification_rows_rebuilt' not in row and 'verify_queries' not in row
        assert 'search_steps' not in row


def test_capture_requires_invocation_identity_and_matching_completed_stage():
    tool = module()
    rows = [dict(invocation='run', worker_pid='10', subjob=1, run_id=0, k=2,
                 stage=stage, entry_ns=100, completion_ns=150, state='completed',
                 capture_join='absent') for stage in ('search', 'verification')]
    complete = dict(invocation='run', worker_pid='10', subjob='1', run_id='0', k='2',
                    stage='verified-attempt', status='WIN_K', search_steps='99',
                    verify_queries='5', verification_rows_rebuilt='7',
                    verify_traversal_queries='2', verify_traversal_observed='false')
    for field in ('invocation', 'worker_pid', 'subjob', 'run_id', 'k'):
        missing = {k: v for k, v in complete.items() if k != field}
        tool.join_captures(rows, [missing])
        assert all(r['capture_join'] == 'absent' for r in rows)
    tool.join_captures(rows, [{**complete, 'invocation': 'other'}])
    assert all(r['capture_join'] == 'absent' for r in rows)
    tool.join_captures(rows, [complete])
    assert rows[0]['search_steps'] == '99' and 'verify_queries' not in rows[0]
    assert rows[1]['verify_queries'] == '5' and 'search_steps' not in rows[1]
    assert rows[1]['verification_rows_rebuilt'] == '7'
    assert 'verify_traversal_queries' not in rows[1]


def test_capture_never_completes_stopped_or_unobserved_stages():
    capture = dict(invocation='run', worker_pid='10', subjob='1', run_id='0', k='2',
                   stage='verified-attempt', status='WIN_K', search_steps='99', verify_queries='5')
    for state in ('stopped', 'unknown-entry', 'unknown-completion', 'censored'):
        row = dict(invocation='run', worker_pid='10', subjob=1, run_id=0, k=2,
                   stage='verification', entry_ns=100, state=state, capture_join='absent')
        module().join_captures([row], [capture])
        assert row['capture_join'] == 'absent' and 'verify_queries' not in row


@pytest.mark.parametrize('k', [2, 5])
def test_release_capture_producer_joins_completed_sparse_subjobs(tmp_path, k):
    build = os.environ.get('ACACIA_RELEASE_TEST_BUILD')
    if not build:
        pytest.skip('set ACACIA_RELEASE_TEST_BUILD to a release solver build')
    binary = Path(build).resolve() / 'src/acacia-bonsai'
    assert binary.is_file(), binary
    source = tmp_path / 'generated.ltl'
    source.write_text('G(i_001 <-> X(o_001)) & G(i_002 <-> X(o_002))\n')
    command = [str(binary), '-F', str(source), '-i', 'i_001,i_002', '-o', 'o_001,o_002',
               '--arms', 'real:small:spot-guarded-sparse', '--spot-fast', 'off',
               '--weakening', 'off', '--equivariance', 'off', '-M', str(k),
               '-K', str(k), '-I', '3']
    env = dict(os.environ)
    for key in ('ACACIA_PHASE_RECORDS', 'ACACIA_PHASE_INVOCATION', 'ACACIA_SPOT_CAPTURE_DIR',
                'ACACIA_SPOT_CAPTURE_HISTORY', 'ACACIA_DIAG', 'ACACIA_DIAG_INSTANCE',
                'ACACIA_OUTER_DEADLINE_MONOTONIC'):
        env.pop(key, None)
    plain = subprocess.run(command, env=env, capture_output=True, timeout=15, check=True)
    invocation = f'capture-"bound"-{k}'
    env['ACACIA_PHASE_INVOCATION'] = invocation
    bound_only = subprocess.run(command, env=env, capture_output=True, timeout=15, check=True)
    phases, captures = tmp_path / 'phases', tmp_path / 'captures'
    phases.mkdir()
    env.update(ACACIA_PHASE_RECORDS=str(phases), ACACIA_SPOT_CAPTURE_DIR=str(captures),
               ACACIA_SPOT_CAPTURE_HISTORY='1', ACACIA_DIAG_INSTANCE='separate-instance')
    observed = subprocess.run(command, env=env, capture_output=True, timeout=15, check=True)
    assert (plain.returncode, plain.stdout, plain.stderr) == (
        bound_only.returncode, bound_only.stdout, bound_only.stderr) == (
        observed.returncode, observed.stdout, observed.stderr)
    produced = [json.loads(path.read_text()) for path in captures.glob('*.json')]
    assert len(produced) == 2
    assert {capture['subjob'] for capture in produced} == {'1', '2'}
    for capture in produced:
        assert capture['invocation'] == invocation
        assert capture['instance'] == 'separate-instance'
        assert capture['provider'] == 'frozen-graph'
        assert capture['backend'] == 'spot-guarded-sparse'
        assert capture['stage'] == 'verified-attempt' and capture['status'] == 'WIN_K'
        assert capture['k'] == str(k)
    histories = [json.loads(line) for path in captures.glob('*.history.jsonl')
                 for line in path.read_text().splitlines()]
    assert histories and all(capture['invocation'] == invocation for capture in histories)

    def table(label, output):
        subprocess.run([sys.executable, str(ROOT / 'benchmarking/legacy-phase-table.py'),
                        '--invocation', f'{label}={phases}', '--capture', str(captures),
                        '--output', str(output)], capture_output=True, timeout=15, check=True)
        with (output / 'phases.tsv').open() as handle:
            return list(csv.DictReader(handle, delimiter='\t'))

    rows = table(invocation, tmp_path / 'joined')
    joined = [row for row in rows if row['capture_join'] == 'complete-attempt']
    assert len(joined) == 4
    for capture in produced:
        matching = [row for row in joined if all(row[key] == capture[key]
                    for key in ('invocation', 'worker_pid', 'subjob', 'run_id', 'k'))]
        assert {row['stage'] for row in matching} == {'search', 'verification'}
        assert len(matching) == 2
        for row in matching:
            assert row['worker'] == '0' and row['subjobs'] == '2'
            assert row['state'] == 'completed' and row['delivery'] == 'observed'
            assert row['entry_ns'] and row['completion_ns']
            if row['stage'] == 'search':
                assert row['search_ms'] == capture['search_ms']
                assert row['search_steps'] == capture['search_steps']
                assert not row['verification_ms'] and not row['verify_queries']
            else:
                assert row['verification_ms'] == capture['verification_ms']
                assert row['verify_queries'] == capture['verify_queries']
                assert row['verification_rows_rebuilt'] == capture['verification_rows_rebuilt']
                assert not row['search_ms'] and not row['search_steps']
                assert not row.get('verify_proof_bad_queries')
    assert all(row['capture_join'] == 'absent' for row in rows
               if row['stage'] not in ('search', 'verification'))
    foreign = table('other-invocation', tmp_path / 'foreign')
    assert all(row['capture_join'] == 'absent' for row in foreign)
    assert all(not row.get('search_ms') and not row.get('verification_ms') for row in foreign)
