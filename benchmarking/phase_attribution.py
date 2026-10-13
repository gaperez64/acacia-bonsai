"""Shared P0 plan-chain attribution for compact and native phase records.

A native route is an observed interval, not evidence of a completed K attempt.
Summary-only durations have no reconstructible monotonic endpoints.
"""
from __future__ import annotations

from benchlib import phase_plan_stage

NATIVE_BACKENDS = {'oxidd', 'obligation'}
CONSTRUCTION = 'IO/action construction'


def native_rows(invocation, records):
    rows = []
    for pid, events in records.worker_events().items():
        context = {key: event[key] for event in events for key in (
            'worker', 'requested_backend', 'effective_backend', 'original_polarity',
            'proof_polarity', 'route') if key in event}
        if not context.get('requested_backend'):
            arm = next((e.get('arm') for e in events if ':' in e.get('arm', '')), '')
            if arm:
                context['requested_backend'] = 'obligation' if ':obligation:' in arm else (
                    arm.rsplit(':', 1)[-1])
        if context.get('requested_backend') not in NATIVE_BACKENDS:
            continue
        timed = [e for e in events if 'mono_ns' in e]
        terminal = next((e for e in reversed(timed)
                         if e.get('event') == 'parent_terminal'), {})
        starts = [e for e in timed if e.get('event') in {
            'route_start', 'verification_start', 'obligation_start'}]
        starts = [e for e in starts if not (e.get('stage') == 'dispatch' and
                  context.get('requested_backend') == 'obligation')]
        active = starts[-1] if starts else None
        for event in starts:
            kind = event['event']
            stage = 'obligation' if kind == 'obligation_start' else event.get('stage', '')
            if stage == 'dispatch' and context.get('requested_backend') == 'obligation':
                continue  # The parent obligation_start is the supervised boundary.
            row = {**context, 'invocation': invocation, 'worker_pid': pid,
                   'stage_id': f"native:{event['mono_ns']}", 'stage': stage,
                   'route': event.get('route', 'obligation'),
                   'plan_stage': 'independent verification' if kind == 'verification_start'
                   else phase_plan_stage(stage, event.get('route', '')),
                   'entry_ns': event['mono_ns'], 'entry_source': 'native-lifecycle',
                   'state': 'unknown-completion', 'reason': 'missing-completion',
                   'capture_join': 'absent'}
            completion = next((e for e in timed if e['mono_ns'] >= event['mono_ns'] and (
                kind == 'verification_start' and e.get('event') in {
                    'check_complete', 'verification_complete'} or
                kind == 'obligation_start' and e.get('event') == 'obligation_completion')), None)
            if completion:
                outcome = completion.get('outcome', '')
                cancelled = outcome == 'cancelled'
                row.update(state='censored' if cancelled else 'completed' if (
                    not outcome or outcome.startswith('completed_')) else 'stopped',
                           reason=completion.get('reason', outcome or 'checked'))
                if cancelled:
                    row['censored_lower_ns'] = completion['mono_ns'] - event['mono_ns']
                else:
                    row.update(completion_ns=completion['mono_ns'],
                               wall_ns=completion['mono_ns'] - event['mono_ns'])
            elif (event is active and terminal.get('signal', 0) > 0
                  and terminal.get('stage') == event.get('stage')
                  and terminal.get('route') == event.get('route')):
                row.update(state='censored', reason=terminal.get('reason', 'signal'))
                if terminal.get('mono_ns', 0) >= row['entry_ns']:
                    row['censored_lower_ns'] = terminal['mono_ns'] - row['entry_ns']
            rows.append(row)
        # Native phase objects can contain aggregate counters for unentered
        # work. Keep their durations without inferring stage completion/endpoints.
        for index, event in enumerate(events):
            phase = event.get('phase')
            if phase and phase != 'record_summary' and 'wall_ns' in event:
                stage = event.get('stage', context.get('route', '')) if (
                    phase == 'budget_decline') else phase
                rows.append({**context, 'invocation': invocation, 'worker_pid': pid,
                             'stage_id': f'native-summary:{index}', 'stage': stage,
                             'plan_stage': phase_plan_stage(stage, context.get('route', '')),
                             'state': 'summary-only', 'entry_source': 'native-summary',
                             'wall_ns': event['wall_ns'], 'cpu_ns': event.get('cpu_ns'),
                             'peak_rss_kb': event.get('peak_rss_kb'),
                             'reason': phase, 'capture_join': 'absent'})
    return rows


def obstruction(plan_stage):
    return {
        'pre-translation checks': 'translation_limited', 'rewrite': 'translation_limited',
        'translator/postprocessing': 'translation_limited', 'input push': 'construction_limited',
        'preprocessing/Booleanization': 'construction_limited',
        CONSTRUCTION: 'construction_limited', 'K-search': 'search_limited',
        'independent verification': 'verification_limited',
    }.get(plan_stage, 'unknown')


def classify_invocation(result, records, rows):
    """Attribute only surviving/censored workers; early declines do not vote.

    Explicit invocation MEMOUT is separate. Missing worker/active-stage evidence
    stays unknown; known competing categories become mixed. No duration ranking.
    """
    if result == 'MEMOUT':
        return 'memory_limited'
    if result in {'REALIZABLE', 'UNREALIZABLE'}:
        return 'solved'
    workers = records.worker_events()
    if not workers:
        return 'unknown'
    categories = set()
    for pid, events in workers.items():
        terminal = next((e for e in reversed(events)
                         if e.get('event') == 'parent_terminal'), {})
        if terminal and not terminal.get('signal', 0):
            continue
        candidates = [r for r in rows if r['worker_pid'] == pid and r.get('entry_ns')
                      and r['state'] in {'censored', 'unknown-completion'}]
        last = max(candidates, key=lambda r: r['entry_ns']) if candidates else None
        # A dropped entry must not let an older surviving start override the
        # parent's newer active-stage evidence. It supplies no missing endpoint.
        observed = next((e for e in reversed(events) if e.get('stage')), {})
        if last and observed.get('mono_ns', 0) < last['entry_ns']:
            categories.add(obstruction(last.get('plan_stage', 'unknown')))
        else:
            categories.add(obstruction(phase_plan_stage(observed.get('stage', ''),
                                                       observed.get('route', ''))))
    if not categories or 'unknown' in categories:
        return 'unknown'
    return next(iter(categories)) if len(categories) == 1 else 'mixed'


def main():
    """Refresh the P0 invocation obstruction table from explicit archived rows."""
    import argparse
    from collections import Counter
    import csv
    import importlib.util
    import json
    from pathlib import Path

    from benchlib import load_phase_records, phase_record_dir

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rows', type=Path, required=True)
    parser.add_argument('--phase-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('p0_phase_table',
                                                Path(__file__).with_name('legacy-phase-table.py'))
    table = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(table)
    output = []
    with args.rows.open(newline='') as stream:
        for row in csv.DictReader(stream, delimiter='\t'):
            if row['result'] in {'REALIZABLE', 'UNREALIZABLE'}:
                continue
            directory = phase_record_dir(args.phase_root, row['solver_label'], row['cap_s'],
                                         row['instance'], row['run_index'])
            records = load_phase_records(directory)
            phases = table.build_rows(row['instance'], records)
            output.append({'instance': row['instance'], 'result': row['result'],
                           'invocation_obstruction': classify_invocation(row['result'],
                                                                       records, phases),
                           'phase_directory': str(directory),
                           'workers': json.dumps([{key: r.get(key) for key in (
                               'worker_pid', 'stage', 'plan_stage', 'state', 'entry_ns',
                               'completion_ns', 'censored_lower_ns', 'delivery')}
                                                  for r in phases])})
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('w', newline='') as stream:
        fields = ['instance', 'result', 'invocation_obstruction', 'phase_directory', 'workers']
        writer = csv.DictWriter(stream, fields, delimiter='\t', lineterminator='\n')
        writer.writeheader()
        writer.writerows(output)
    print(json.dumps(dict(Counter(r['invocation_obstruction'] for r in output)), sort_keys=True))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
