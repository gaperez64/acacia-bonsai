"""Real native records and conservative first-query/boundary attribution."""
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
from phase_attribution import classify_invocation  # noqa: E402

FIXTURES = Path(__file__).parent / 'fixtures/phase-records-i251'


def module(name):
    spec = importlib.util.spec_from_file_location(name.replace('-', '_'),
                                                ROOT / 'benchmarking' / f'{name}.py')
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


TABLE = module('legacy-phase-table')
PANEL = module('memory-search-panel')


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


def test_native_censoring_classifies_without_promoting_early_declines():
    records = load_phase_records(FIXTURES / 'native-censored')
    events = tuple(e for ev in records.worker_events().values() for e in ev
                   if e.get('requested_backend') == 'oxidd' or
                   str(e.get('arm', '')).startswith('both:gr1'))
    native = PhaseRecords(records.directory, (PhaseProcess('1.jsonl', events, False, 0),))
    assert classify_invocation('TIMEOUT', native, TABLE.build_rows('x', native)) == (
        'construction_limited')
    assert classify_invocation('MEMOUT', native, []) == 'memory_limited'
    assert classify_invocation('TIMEOUT', load_phase_records('/absent/records'), []) == 'unknown'


def test_native_missing_start_is_unknown_and_live_drop_counts_survive(tmp_path):
    event = {'event': 'parent_terminal', 'worker_pid': 7, 'worker': 0, 'signal': 9,
             'mono_ns': 50, 'seq': 3, 'stage': 'trusted_prepare', 'requested_backend': 'oxidd',
             'dropped_records': 2}
    (tmp_path / '1.jsonl').write_text(json.dumps(event) + '\n')
    records = load_phase_records(tmp_path)
    assert records.dropped == 2
    assert TABLE.build_rows('missing', records) == []
    # A lifecycle terminal gives an observed obstruction but no duration/entry.
    assert classify_invocation('TIMEOUT', records, []) == 'construction_limited'


def test_boundary_requires_close_bracketing_samples():
    samples = [{'start_ns': 80, 'end_ns': 90, 'scope_memory_current_bytes': 4},
               {'start_ns': 110, 'end_ns': 120, 'scope_memory_current_bytes': 8}]
    bound = PANEL.boundary_memory(100, samples)
    assert bound['state'] == 'bracketed' and bound['gap_ns'] == 40
    assert PANEL.boundary_memory(60, samples)['state'] == 'unknown'
    assert PANEL.boundary_memory(100, [samples[0], {
        **samples[1], 'end_ns': 200_000_000}])['state'] == 'unknown'


def test_evidence_selection_ignores_names_and_retains_memory_gain():
    def candidate(number):
        return {'instance': f'old-{number}', 'old_memory': number == 0,
                'construction_to_search': number % 2 == 0, 'paired_gain': number == 1,
                'old_construction': True, 'source_bytes': 10 << number,
                'source_sha256': str(number), 'legacy': ['observed'],
                'unfinished': ['K-search']}
    rows = [candidate(i) for i in range(12)]
    selected = PANEL.select(rows, 6)
    renamed = [{**r, 'instance': 'renamed-' + r['instance']} for r in reversed(rows)]
    assert [r['source_sha256'] for r in selected] == [
        r['source_sha256'] for r in PANEL.select(renamed, 6)]
    assert {r['source_sha256'] for r in selected} >= {'0', '1'}


def test_torn_capture_tail_does_not_supply_completion(tmp_path):
    path = tmp_path / 'capture.history.jsonl'
    path.write_bytes(b'{"stage":"search"}\n{"stage":"verified-attempt"')
    assert PANEL.complete_jsonl(path) == [{'stage': 'search'}]


def test_existing_classification_reader_maps_native_and_obligation_phases():
    classifier = module('classify-coverage')
    for stage, expected in [('trusted_prepare', 'IO/action construction'),
                            ('target_check', 'independent verification'),
                            ('original_game', 'K-search')]:
        observed = classifier.phase_attribution([{'event': 'route_start', 'stage': stage}])
        assert observed['last_observed_stage'] == stage
        assert observed['last_plan_stage'] == expected
    assert classifier.phase_attribution([])['last_plan_stage'] == 'unknown'


def test_driver_collects_and_analyzes_a_scoped_timeout_without_budget_changes(tmp_path,
                                                                            monkeypatch):
    from types import SimpleNamespace
    import time

    from benchlib import RunResult

    binary, source = tmp_path / 'solver', tmp_path / 'input.tlsf'
    binary.write_bytes(b'fake executable')
    source.write_text('generated source')
    manifest = {'binary': str(binary), 'binary_sha256': PANEL.digest(binary), 'cap_s': 60,
                'flags': [], 'jobs': [{'instance': 'generated', 'source': str(source),
                                      'source_sha256': PANEL.digest(source)}]}
    panel = tmp_path / 'panel.json'
    panel.write_text(json.dumps(manifest))
    output = tmp_path / 'run'

    class Samples:
        def __init__(self, phases, destination):
            self.destination = destination

        def __enter__(self):
            self.destination.write_text('')

        def __exit__(self, *args):
            pass

    def scoped(command, timeout, memory, swap, *, env, **kwargs):
        assert command == [str(binary), '-T', str(source)]
        assert (timeout, memory, swap) == (60, '8G', '0')
        assert env['ACACIA_SPOT_CAPTURE_HISTORY'] == '1'
        assert 'ACACIA_OUTER_DEADLINE_MONOTONIC' not in env
        assert 'ACACIA_FORWARD_MAX_NODES' not in env
        assert kwargs['scratch_root'] == ROOT / 'build_scratch/i251/scope-memory'
        now = time.monotonic_ns()
        identity = {'worker_pid': 123, 'worker': 0, 'dropped_records': 0}
        events = [
            {**identity, 'seq': 1, 'event': 'worker_start', 'mono_ns': now,
             'requested_backend': 'forward'},
            {**identity, 'seq': 2, 'event': 'stage_entry', 'stage': 'search',
             'stage_id': 1, 'k': 2, 'mono_ns': now + 10},
            {**identity, 'seq': 3, 'event': 'stage_metric', 'stage_id': 1,
             'key': 'first_useful_action_query_ns', 'value': str(now + 20), 'mono_ns': now + 20},
            {**identity, 'seq': 4, 'event': 'stage_completion', 'stage': 'search',
             'stage_id': 1, 'k': 2, 'mono_ns': now + 30, 'wall_ns': 20},
            {**identity, 'seq': 5, 'event': 'stage_entry', 'stage': 'verification',
             'stage_id': 2, 'k': 2, 'mono_ns': now + 50},
            {**identity, 'seq': 6, 'event': 'parent_terminal', 'stage': 'verification',
             'stage_id': 2, 'signal': 9, 'mono_ns': now + 80},
            {'event': 'writer_summary', 'failed_records': 0, 'incomplete_packet': False},
        ]
        phases = Path(env['ACACIA_PHASE_RECORDS'])
        (phases / '123.jsonl').write_text(''.join(json.dumps(e) + '\n' for e in events))
        samples = [{'start_ns': now + 35, 'end_ns': now + 40,
                    'scope_memory_current_bytes': 70},
                   {'start_ns': now + 60, 'end_ns': now + 65,
                    'scope_memory_current_bytes': 90}]
        (phases.parent / 'scope-samples.jsonl').write_text(
            ''.join(json.dumps(e) + '\n' for e in samples))
        return RunResult('', '', 124, 60, True, memory_peak_bytes=100,
                         scope_memory_peak_source='cgroup-v2/memory.peak',
                         scope_memory_peak_missing_reason='', memory_cgroup='/test/scope')

    monkeypatch.setenv('ACACIA_FORWARD_MAX_NODES', '1')
    monkeypatch.setenv('ACACIA_OUTER_DEADLINE_MONOTONIC', '1')
    monkeypatch.setattr(PANEL, 'cooldown', lambda *args: None)
    monkeypatch.setattr(PANEL, 'ScopeSamples', Samples)
    monkeypatch.setattr(PANEL, 'run_systemd_scope', scoped)
    PANEL.run(SimpleNamespace(panel=panel, output=output, observation_binary=None,
                              one=False, cap=None, execute=True, resume=False))
    observed = json.loads((output / 'analysis.json').read_text())[0]
    assert observed['result'] == 'TIMEOUT'
    assert observed['scope_memory_peak_bytes'] == 100
    assert observed['invocation_obstruction'] == 'verification_limited'
    worker = observed['workers'][0]
    assert worker['k_sequence'][0]['k'] == 2
    assert worker['first_useful_query'][0]['search_elapsed_ns'] == 10
    assert worker['boundary_scope_memory'][0]['state'] == 'bracketed'
    assert len(worker['completed_search_check_stages']) == 1


def test_standalone_native_summary_keeps_known_stage_but_missing_entry(tmp_path):
    event = {'arm': 'real:gr1:oxidd', 'phase': 'trusted_prepare', 'wall_ns': 10}
    (tmp_path / '77.jsonl').write_text(json.dumps(event) + '\n')
    records = load_phase_records(tmp_path)
    assert '77' in records.worker_events()
    row, = TABLE.build_rows('standalone', records)
    assert row['plan_stage'] == 'IO/action construction'
    assert row['state'] == 'summary-only'
    assert 'entry_ns' not in row and 'completion_ns' not in row
    assert classify_invocation('TIMEOUT', records, [row]) == 'unknown'


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
    worker = PhaseRecords(tmp_path, tuple(PhaseProcess(p.name, tuple(
        e for e in p.events if e.get('worker_pid') == 747586 or
        e.get('arm') == 'both:gr1-real-lift:oxidd'), p.finished, p.dropped)
        for p in records.processes))
    assert classify_invocation('TIMEOUT', worker, TABLE.build_rows('dropped', worker)) == (
        'verification_limited')


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


def test_k_sequence_keeps_attempt_occurrences_and_search_fallback():
    def row(stage, identifier, start, k, **extra):
        return {'stage': stage, 'stage_id': identifier, 'entry_ns': start, 'k': k,
                'subjob': 1, 'run_id': 0, **extra}
    rows = [row('k-attempt', 1, 10, 2, completion_ns=40), row('search', 2, 20, 2),
            row('k-attempt', 3, 50, 5, completion_ns=80), row('search', 4, 60, 5),
            row('k-attempt', 5, 90, 5), row('search', 6, 100, 5),
            row('search', 7, 110, 8), row('search', 8, 120, 8, subjob=2)]
    sequence = PANEL.k_attempts(rows)
    assert [r['k'] for r in sequence] == [2, 5, 5, 8, 8]
    assert [r['stage_id'] for r in sequence] == [1, 3, 5, 7, 8]
    assert len(rows) == 8  # Nested search evidence remains available independently.


@pytest.fixture
def resumable_panel(tmp_path, monkeypatch):
    from types import SimpleNamespace
    from benchlib import RunResult

    binary = tmp_path / 'solver'
    binary.write_bytes(b'generated executable')
    jobs = []
    for index in range(2):
        source = tmp_path / f'input-{index}.tlsf'
        source.write_text(f'generated source {index}')
        jobs.append({'instance': f'generated-{index}', 'source': str(source),
                     'source_sha256': PANEL.digest(source)})
    panel = tmp_path / 'panel.json'
    panel.write_text(json.dumps({'binary': str(binary), 'binary_sha256': PANEL.digest(binary),
                                'cap_s': 60, 'flags': [], 'jobs': jobs}))
    args = SimpleNamespace(panel=panel, output=tmp_path / 'output', observation_binary=None,
                           cap=None, one=False, execute=True, resume=False)
    launched, cooled = [], []

    class Samples:
        def __init__(self, phases, destination):
            self.destination = destination

        def __enter__(self):
            self.destination.write_text('')

        def __exit__(self, *args):
            pass

    def scoped(command, *unused, env, **kwargs):
        identity = env['ACACIA_PHASE_INVOCATION']
        assert cooled[-1] == identity  # Every launch, including retries, is cooled.
        launched.append(identity)
        return RunResult('', '', 124, 60, True, memory_cgroup='/test/scope')

    monkeypatch.setattr(PANEL, 'cooldown', lambda log, identity: cooled.append(
        identity['invocation']))
    monkeypatch.setattr(PANEL, 'ScopeSamples', Samples)
    monkeypatch.setattr(PANEL, 'run_systemd_scope', scoped)
    return args, launched, cooled, scoped


def test_resume_skips_completed_jobs_and_preserves_the_plan(resumable_panel):
    args, launched, cooled, _ = resumable_panel
    PANEL.run(args)
    plan_bytes = (args.output / 'run-plan.json').read_bytes()
    first_bytes = (args.output / 'job-00/run.json').read_bytes()
    assert json.loads(plan_bytes)['cooldown'] == PANEL.COOLDOWN
    args.resume = True
    PANEL.run(args)
    assert len(launched) == len(cooled) == 2
    assert (args.output / 'run-plan.json').read_bytes() == plan_bytes
    assert (args.output / 'job-00/run.json').read_bytes() == first_bytes
    assert len(json.loads((args.output / 'analysis.json').read_text())) == 2


def test_resume_retries_interruption_in_a_separate_attempt(resumable_panel, monkeypatch):
    args, launched, cooled, scoped = resumable_panel

    def interrupted(command, *unused, env, **kwargs):
        if 'input-1.tlsf' in command[-1]:
            (Path(env['ACACIA_PHASE_RECORDS']) / '7.jsonl').write_text('retained partial records')
            raise KeyboardInterrupt
        return scoped(command, *unused, env=env, **kwargs)

    monkeypatch.setattr(PANEL, 'run_systemd_scope', interrupted)
    with pytest.raises(KeyboardInterrupt):
        PANEL.run(args)
    old = args.output / 'job-01'
    header = (old / 'attempt.json').read_bytes()
    records = (old / 'phase-records/7.jsonl').read_bytes()
    assert not (old / 'run.json').exists()
    args.resume = True
    monkeypatch.setattr(PANEL, 'run_systemd_scope', scoped)
    PANEL.run(args)
    retry = args.output / 'job-01-attempt-02'
    assert (old / 'attempt.json').read_bytes() == header
    assert (old / 'phase-records/7.jsonl').read_bytes() == records
    assert json.loads((retry / 'run.json').read_text())['attempt'] == 2
    assert launched[0] != launched[1] and launched[1].endswith(':2')
    assert len(launched) == 2 and len(cooled) == 3
    analyzed = json.loads((args.output / 'analysis.json').read_text())
    assert len(analyzed) == 2 and analyzed[1]['attempt_directory'] == str(retry)
    PANEL.run(args)
    assert len(launched) == 2


@pytest.mark.parametrize('change', ['cap', 'panel', 'binary', 'treatment', 'source',
                                    'run', 'attempt'])
def test_resume_rejects_changed_provenance_before_launch(resumable_panel, change):
    args, launched, cooled, _ = resumable_panel
    PANEL.run(args)
    plan_path = args.output / 'run-plan.json'
    plan_bytes = plan_path.read_bytes()
    args.resume = True
    if change == 'cap':
        args.cap = 10
    elif change == 'panel':
        args.panel.write_text(args.panel.read_text() + '\n')
    elif change in {'binary', 'treatment'}:
        manifest = json.loads(args.panel.read_text())
        if change == 'binary':
            Path(manifest['binary']).write_bytes(b'changed executable')
        else:
            args.observation_binary = Path(manifest['binary'])
    elif change == 'source':
        Path(json.loads(args.panel.read_text())['jobs'][1]['source']).write_text('changed source')
    else:
        path = args.output / 'job-01' / ('run.json' if change == 'run' else 'attempt.json')
        saved = json.loads(path.read_text())
        saved['source_sha256'] = 'changed'
        path.write_text(json.dumps(saved))
    with pytest.raises(ValueError):
        PANEL.run(args)
    assert len(launched) == len(cooled) == 2
    assert plan_path.read_bytes() == plan_bytes


def test_existing_jobs_require_resume_and_resume_requires_a_plan(resumable_panel):
    args, launched, _, _ = resumable_panel
    args.resume = True
    with pytest.raises(ValueError, match='requires a saved run plan'):
        PANEL.run(args)
    args.resume = False
    PANEL.run(args)
    with pytest.raises(ValueError, match='require --resume'):
        PANEL.run(args)
    assert len(launched) == 2


def test_preview_then_execution_preserves_the_run_plan(resumable_panel):
    args, launched, cooled, _ = resumable_panel
    args.execute = False
    PANEL.run(args)
    plan = (args.output / 'run-plan.json').read_bytes()
    assert not launched and not cooled
    args.execute = True
    PANEL.run(args)
    assert len(launched) == 2
    assert (args.output / 'run-plan.json').read_bytes() == plan


def test_cooldown_waits_to_70_and_records_readiness(tmp_path, monkeypatch):
    sensor, log = tmp_path / 'sensor', tmp_path / 'cooldown.jsonl'
    sensor.write_text('80000')
    monkeypatch.setattr(PANEL, 'COOLDOWN', {**PANEL.COOLDOWN, 'sensor': str(sensor)})
    elapsed = [0]

    def sleep(duration):
        assert duration == 1
        elapsed[0] += duration
        sensor.write_text('70000')

    PANEL.cooldown(log, {'invocation': 'generated'}, now=lambda: elapsed[0], sleep=sleep)
    records = [json.loads(line) for line in log.read_text().splitlines()]
    assert [r['state'] for r in records] == ['waiting', 'ready']
    assert records[-1]['temperature_millidegrees'] == 70000
    assert all(r['invocation'] == 'generated' for r in records)


@pytest.mark.parametrize('value', [None, 'bad', '0', '150001', '80000'])
def test_cooldown_stops_on_sensor_failure_or_max_wait(tmp_path, monkeypatch, value):
    sensor, log = tmp_path / 'sensor', tmp_path / 'cooldown.jsonl'
    if value is not None:
        sensor.write_text(value)
    monkeypatch.setattr(PANEL, 'COOLDOWN', {**PANEL.COOLDOWN, 'sensor': str(sensor)})
    elapsed = [0]

    def sleep(duration):
        elapsed[0] += duration

    with pytest.raises(ValueError, match='cooldown stopped before invocation'):
        PANEL.cooldown(log, {}, now=lambda: elapsed[0], sleep=sleep)
    records = [json.loads(line) for line in log.read_text().splitlines()]
    if value == '80000':
        assert elapsed[0] == 180 and records[-1]['state'] == 'max-wait-exceeded'
    else:
        assert elapsed[0] == 0 and records[-1]['state'].startswith('sensor-error')


def test_failed_cooldown_never_launches_the_next_job(resumable_panel, monkeypatch):
    args, launched, cooled, _ = resumable_panel

    def cooldown(log, identity):
        if identity['job_index'] == 1:
            raise ValueError('cooldown stopped before invocation: sensor-error')
        cooled.append(identity['invocation'])

    monkeypatch.setattr(PANEL, 'cooldown', cooldown)
    with pytest.raises(ValueError, match='sensor-error'):
        PANEL.run(args)
    assert len(launched) == 1
    assert not (args.output / 'job-01').exists()



def test_analysis_emits_one_k_per_attempt_and_retains_nested_queries(resumable_panel):
    args, _, _, _ = resumable_panel
    PANEL.run(args)
    directory = args.output / 'job-00'
    meta = json.loads((directory / 'run.json').read_text())
    start = meta['invocation_start_ns']
    identity = {'worker_pid': 7, 'subjob': 1, 'run_id': 0}
    events = [
        {'event': 'worker_start', 'requested_backend': 'forward', 'mono_ns': start},
        {'event': 'stage_entry', 'stage': 'k-attempt', 'stage_id': 1, 'k': 2,
         'mono_ns': start + 10},
        {'event': 'stage_entry', 'stage': 'search', 'stage_id': 2, 'k': 2,
         'mono_ns': start + 20},
        {'event': 'stage_metric', 'stage_id': 2, 'key': 'first_useful_action_query_ns',
         'value': str(start + 25), 'mono_ns': start + 25},
        {'event': 'stage_completion', 'stage': 'search', 'stage_id': 2, 'k': 2,
         'mono_ns': start + 30},
        {'event': 'stage_completion', 'stage': 'k-attempt', 'stage_id': 1, 'k': 2,
         'mono_ns': start + 40},
        {'event': 'stage_entry', 'stage': 'k-attempt', 'stage_id': 3, 'k': 5,
         'mono_ns': start + 50},
        {'event': 'stage_entry', 'stage': 'search', 'stage_id': 4, 'k': 5,
         'mono_ns': start + 60},
        {'event': 'parent_terminal', 'stage': 'search', 'stage_id': 4, 'signal': 9,
         'mono_ns': start + 70},
    ]
    (directory / 'phase-records/7.jsonl').write_text(''.join(json.dumps(
        {**identity, **e, 'seq': i + 1}) + '\n' for i, e in enumerate(events)))
    PANEL.analyze(args)
    worker = json.loads((args.output / 'analysis.json').read_text())[0]['workers'][0]
    assert [r['k'] for r in worker['k_sequence']] == [2, 5]
    assert [r['stage_id'] for r in worker['k_sequence']] == [1, 3]
    assert worker['first_useful_query'][0]['stage_id'] == 2
    assert worker['first_useful_query'][0]['search_elapsed_ns'] == 5
    assert {r['stage'] for r in worker['completed_search_check_stages']} == {'k-attempt', 'search'}
