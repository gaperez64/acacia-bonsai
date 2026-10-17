"""Native phase endpoints, mapping and transport delivery through the retained reader."""


from __future__ import annotations


import hashlib


import importlib.util


import json


from pathlib import Path


import sys


import pytest


ROOT = Path(__file__).resolve().parents[2]


sys.path.insert(0, str(ROOT / 'benchmarking'))


from benchlib import PhaseProcess, PhaseRecords, load_phase_records  # noqa: E402


FIXTURES = Path(__file__).parent / 'fixtures/phase-records-i251'


def module(name):
    spec = importlib.util.spec_from_file_location(name.replace('-', '_'),
                                                ROOT / 'benchmarking' / f'{name}.py')
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


TABLE = module('legacy-phase-table')


def test_fixture_bytes_bound_to_archived_records():
    for row in json.loads((FIXTURES / 'provenance.json').read_text()):
        assert hashlib.sha256((FIXTURES / row['fixture']).read_bytes()).hexdigest() == row['sha256']


@pytest.mark.parametrize('fixture', ['native-censored', 'native-check', 'obligation'])
def test_native_plan_chain_preserves_missing_endpoints(fixture):
    records = load_phase_records(FIXTURES / fixture)
    rows = TABLE.build_rows('bound', records)
    native = [r for r in rows if r.get('requested_backend') in {'oxidd', 'obligation'}]
    assert native and all(r['plan_stage'] != 'unknown' for r in native)
    for row in native:
        if row['state'] == 'censored':
            assert row['censored_lower_ns'] > 0
            assert 'completion_ns' not in row and 'wall_ns' not in row
        elif row['state'] == 'summary-only':
            assert row['wall_ns'] >= 0
            assert 'entry_ns' not in row and 'completion_ns' not in row
        elif row['state'] == 'unknown-completion':
            assert 'completion_ns' not in row and 'censored_lower_ns' not in row
    if fixture == 'native-check':
        check = next(r for r in native if r['stage'] == 'target_check')
        assert check['plan_stage'] == 'independent verification' and check['state'] == 'censored'
    if fixture == 'obligation':
        completed = next(r for r in native if r['stage'] == 'obligation')
        assert completed['state'] == 'completed'
        assert completed['plan_stage'] == 'K-search' and completed.get('k') is None
    summaries = TABLE.worker_summaries(rows)
    assert all(r['stage'] != 'child_startup' for w in summaries for r in w['censored_stages'])


def test_existing_classification_reader_maps_native_and_obligation_phases():
    classifier = module('classify-coverage')
    for stage, expected in [('trusted_prepare', 'IO/action construction'),
                            ('target_check', 'independent verification'),
                            ('original_game', 'K-search')]:
        observed = classifier.phase_attribution([{'event': 'route_start', 'stage': stage}])
        assert observed['last_observed_stage'] == stage
        assert observed['last_plan_stage'] == expected
    assert classifier.phase_attribution([])['last_plan_stage'] == 'unknown'


@pytest.mark.parametrize('fixture,arm,stage,plan_stage', [
    ('native-check', 'both:gr1-real-lift:oxidd', 'target_check', 'independent verification'),
    ('obligation', 'both:obligation:spot', 'original_game', 'K-search'),
    ('native-censored', 'both:gr1-real-lift:oxidd', 'trusted_prepare', 'IO/action construction'),
])
def test_public_arm_reader_joins_native_lifecycle_by_pid(fixture, arm, stage, plan_stage):
    classifier = module('classify-coverage')
    records = load_phase_records(FIXTURES / fixture)
    observed = classifier.arm_attribution(records, arm)
    assert observed['last_observed_stage'] == stage
    assert observed['last_plan_stage'] == plan_stage
    # Parent records for other workers sharing the file must not join this arm.
    assert classifier.arm_attribution(records, 'absent')['last_plan_stage'] == 'unknown'


def test_public_arm_reader_does_not_order_two_physical_workers(tmp_path):
    classifier = module('classify-coverage')
    arm = 'both:gr1-real-lift:oxidd'
    for pid in [7, 8]:
        (tmp_path / f'{pid}.jsonl').write_text(json.dumps({
            'arm': arm, 'phase': 'child_startup'}) + '\n')
    (tmp_path / '1.jsonl').write_text(json.dumps({
        'event': 'parent_terminal', 'worker_pid': 7, 'stage': 'target_check',
        'route': 'direct', 'mono_ns': 100, 'signal': 9}) + '\n')
    observed = classifier.arm_attribution(load_phase_records(tmp_path), arm)
    assert observed['last_plan_stage'] == 'unknown'


@pytest.mark.parametrize('terminal_stage,terminal_route,censored', [
    ('trusted_prepare', 'trusted_prepare', True),
    ('target_check', 'direct', False),
    ('trusted_prepare', 'direct', False),
])
def test_native_censoring_requires_matching_terminal_stage_and_route(
        terminal_stage, terminal_route, censored):
    events = ({'event': 'route_start', 'requested_backend': 'oxidd', 'worker_pid': 7,
               'stage': 'trusted_prepare', 'route': 'trusted_prepare', 'mono_ns': 10},
              {'event': 'parent_terminal', 'worker_pid': 7, 'mono_ns': 50, 'signal': 9,
               'stage': terminal_stage, 'route': terminal_route, 'dropped_records': 1})
    records = PhaseRecords(Path('/unused'), (PhaseProcess('7.jsonl', events, False, 1),))
    row, = TABLE.build_rows('dropped', records)
    assert row['state'] == ('censored' if censored else 'unknown-completion')
    assert ('censored_lower_ns' in row) == censored
    assert 'completion_ns' not in row
    if censored:
        assert row['censored_lower_ns'] == 40


def test_native_missing_start_is_unknown_and_live_drop_counts_survive(tmp_path):
    event = {'event': 'parent_terminal', 'worker_pid': 7, 'worker': 0, 'signal': 9,
             'mono_ns': 50, 'seq': 3, 'stage': 'trusted_prepare', 'requested_backend': 'oxidd',
             'dropped_records': 2}
    (tmp_path / '1.jsonl').write_text(json.dumps(event) + '\n')
    records = load_phase_records(tmp_path)
    assert records.dropped == 2
    assert TABLE.build_rows('missing', records) == []


def test_standalone_native_summary_keeps_known_stage_but_missing_entry(tmp_path):
    event = {'arm': 'real:gr1:oxidd', 'phase': 'trusted_prepare', 'wall_ns': 10}
    (tmp_path / '77.jsonl').write_text(json.dumps(event) + '\n')
    records = load_phase_records(tmp_path)
    assert '77' in records.worker_events()
    row, = TABLE.build_rows('standalone', records)
    assert row['plan_stage'] == 'IO/action construction'
    assert row['state'] == 'summary-only'
    assert 'entry_ns' not in row and 'completion_ns' not in row


def test_dropped_native_entries_keep_earlier_endpoints_missing(tmp_path):
    original = load_phase_records(FIXTURES / 'native-check')
    for process in original.processes:
        events = [e for e in process.events if not (e.get('worker_pid') == 747586 and
                  e.get('event') in {'route_start', 'route_selected', 'verification_start'} and
                  e.get('stage') not in {'dispatch', 'trusted_prepare'})]
        (tmp_path / process.name).write_text(''.join(json.dumps(e) + '\n' for e in events))
    records = load_phase_records(tmp_path)
    rows = TABLE.build_rows('dropped', records)
    earlier = [r for r in rows if r['worker_pid'] == '747586' and r.get('entry_ns')]
    assert earlier and all(r['state'] == 'unknown-completion' for r in earlier)
    assert all('completion_ns' not in r and 'censored_lower_ns' not in r for r in earlier)
    assert all(r['delivery'] == 'partial' for r in earlier)
