#!/usr/bin/env python3
"""Join existing phase transport records by invocation, physical worker, subjob and K.

Empty TSV cells are unknown. Censored durations are lower bounds, never completions.
Payload counters retain their recorded names; byte estimates are not RSS or heap sizes.
"""
from __future__ import annotations

import argparse
import csv
import json
from collections import defaultdict
from pathlib import Path

from benchlib import load_phase_records

IDENTITY = ('invocation', 'worker_pid', 'worker', 'subjob', 'subjobs', 'run_id', 'k',
            'stage_id', 'stage', 'requested_backend', 'effective_backend',
            'original_polarity', 'proof_polarity', 'route')
BASE_FIELDS = (*IDENTITY, 'entry_ns', 'completion_ns', 'wall_ns', 'cpu_ns', 'peak_rss_kb',
               'state', 'reason', 'delivery', 'dropped_records', 'censored_lower_ns',
               'capture_join')


def build_rows(invocation, records, captures=()):
    events = [event for process in records.processes for event in process.events]
    events.sort(key=lambda e: (e.get('mono_ns', 0), e.get('seq', 0)))
    writers = [e for e in events if e.get('event') == 'writer_summary']
    writer_confirmed = len(writers) == 1 and writers[0].get('incomplete_packet') is False
    global_partial = records.dropped > 0 or any(
        e.get('failed_records', 0) or e.get('incomplete_packet') for e in writers)
    context, terminal, drops, sequences = {}, {}, defaultdict(int), defaultdict(list)
    stages = {}
    for event in events:
        pid = event.get('worker_pid')
        if pid is None:
            continue
        pid = str(pid)
        if 'seq' in event:
            sequences[pid].append(int(event['seq']))
        drops[pid] = max(drops[pid], int(event.get('dropped_records') or 0))
        context.setdefault(pid, {}).update({k: event[k] for k in IDENTITY if k in event})
        kind = event.get('event')
        if kind in {'parent_terminal', 'terminal_result', 'stage_censored'}:
            terminal.setdefault(pid, {}).update(event)
        if not kind or not kind.startswith('stage_'):
            continue
        key = (pid, event['stage_id'])
        row = stages.setdefault(key, {'invocation': invocation, 'worker_pid': pid,
                                      'stage_id': event['stage_id'], 'state': 'unknown',
                                      'capture_join': 'absent'})
        if kind == 'stage_metric':
            row[event['key']] = event['value']
            continue
        for field in ('worker', 'requested_backend', 'effective_backend',
                      'original_polarity', 'proof_polarity', 'route'):
            if field in context[pid]:
                row.setdefault(field, context[pid][field])
        for field in IDENTITY:
            if field in event:
                if kind == 'stage_entry' or (field == 'subjobs' and row.get(field) is None):
                    row[field] = event[field]
                else:
                    row.setdefault(field, event[field])
        row['invocation'] = invocation
        row['worker_pid'] = pid
        if kind == 'stage_censored':
            row.setdefault('entry_ns', event['entry_ns'])
            row['entry_source'] = 'parent_snapshot'
        elif kind == 'stage_entry':
            row['entry_ns'] = event['mono_ns']
        elif kind in {'stage_completion', 'stage_stopped'}:
            row.update({k: event[k] for k in ('wall_ns', 'cpu_ns', 'peak_rss_kb', 'reason')
                        if k in event})
            row['completion_ns'] = event['mono_ns']
            row['state'] = 'stopped' if kind == 'stage_stopped' else 'completed'
            if str(event.get('reason', '')).startswith('inapplicable-'):
                row['state'] = 'inapplicable'
    # Parent snapshots can rescue a dropped entry, even after SIGKILL.
    for pid, event in terminal.items():
        if event.get('stage_id') and event.get('stage_start_ns'):
            key = (pid, event['stage_id'])
            if key not in stages:
                stages[key] = {**{k: event[k] for k in IDENTITY if k in event},
                               'invocation': invocation, 'worker_pid': pid,
                               'entry_ns': event['stage_start_ns'], 'state': 'unknown',
                               'capture_join': 'absent', 'entry_source': 'parent_snapshot'}
    for (pid, _), row in stages.items():
        for field in IDENTITY:
            # Stage-dependent identity must never come from a later lifecycle event.
            if field not in {'subjob', 'subjobs', 'run_id', 'k', 'stage_id', 'stage'}:
                row.setdefault(field, context.get(pid, {}).get(field))
        seq = sorted(set(sequences[pid]))
        gap = bool(seq and (seq[0] != 1 or any(b != a + 1 for a, b in zip(seq, seq[1:]))))
        row['dropped_records'] = drops[pid]
        row['delivery'] = 'partial' if gap or drops[pid] or global_partial else (
            'observed' if writer_confirmed else 'unconfirmed')
        if 'entry_ns' not in row:
            row['state'] = 'unknown-entry'
        elif 'completion_ns' not in row:
            end = terminal.get(pid, {})
            active = (end.get('stage_id') == row['stage_id']
                      and end.get('entry_ns', end.get('stage_start_ns', row['entry_ns']))
                      == row['entry_ns'])
            censored = active and end.get('signal', 0) > 0
            row['state'] = 'censored' if censored else 'unknown-completion'
            if censored and end.get('mono_ns', 0) >= row['entry_ns']:
                row['censored_lower_ns'] = end['mono_ns'] - row['entry_ns']
            row['reason'] = end.get('reason', 'signal') if censored else 'missing-completion'
    join_captures(list(stages.values()), captures)
    return sorted(stages.values(), key=lambda r: (str(r['worker_pid']), r['stage_id']))


def join_captures(rows, captures):
    """Join only invocation-bound completed stages; historic guesses stay absent."""
    identity = ('invocation', 'worker_pid', 'subjob', 'run_id', 'k')
    prefixes = {
        'search': ('search_', 'wrapper_', 'rank_', 'subsumption_', 'scan_', 'losing_',
                   'proofs_', 'dependency_', 'queue_'),
        'verification': ('verify_', 'verification_'),
    }
    for capture in captures:
        if (capture.get('stage') != 'verified-attempt'
                or capture.get('status') not in {'WIN_K', 'LOSE_K'}
                or any(capture.get(k) in (None, '') for k in identity)):
            continue
        matching = [r for r in rows if all(r.get(k) not in (None, '') and
                    str(r[k]) == str(capture[k]) for k in identity)
                    and r.get('stage') in prefixes
                    and r.get('state') == 'completed'
                    and 'entry_ns' in r and 'completion_ns' in r]
        for stage, allowed in prefixes.items():
            candidates = [r for r in matching if r['stage'] == stage]
            if len(candidates) != 1:
                for row in candidates:
                    row['capture_join'] = 'ambiguous'
                continue
            row = candidates[0]
            row['capture_join'] = 'complete-attempt'
            for key, value in capture.items():
                observed = all(not key.startswith(f'verify_{part}_') or
                               capture.get(f'verify_{part}_observed') == 'true'
                               for part in ('traversal', 'invariant', 'proof_bad'))
                if observed and key.startswith(allowed):
                    row.setdefault(key, value)


def union_length(intervals):
    total, end = 0, None
    for left, right in sorted(intervals):
        if end is None or left > end:
            total += right - left
        else:
            total += max(0, right - end)
        end = right if end is None else max(end, right)
    return total


def worker_summaries(rows):
    workers = defaultdict(list)
    for row in rows:
        workers[(row['invocation'], row['worker_pid'])].append(row)
    summaries = []
    for (invocation, pid), group in workers.items():
        completed = [r for r in group if r['state'] in {'completed', 'stopped'}
                     and 'entry_ns' in r and 'completion_ns' in r]
        by_stage = defaultdict(int)
        for row in completed:
            left, right = row['entry_ns'], row['completion_ns']
            # Subtract nested intervals, so subjobs and enclosing pre-passes do not
            # count translator/search time twice. Unobserved gaps remain unattributed.
            nested = [(r['entry_ns'], r['completion_ns']) for r in completed
                      if r is not row and left <= r['entry_ns'] <= r['completion_ns'] <= right]
            by_stage[row['stage']] += right - left - union_length(nested)
        summaries.append({'invocation': invocation, 'worker_pid': pid,
                          'exclusive_finished_wall_ns': dict(sorted(by_stage.items())),
                          'observed_finished_union_ns': union_length(
                              [(r['entry_ns'], r['completion_ns']) for r in completed]),
                          'censored_stages': [{k: r.get(k) for k in
                                               ('subjob', 'k', 'stage', 'state',
                                                'censored_lower_ns')}
                                              for r in group if r['state'] not in
                                              {'completed', 'stopped', 'inapplicable'}],
                          'delivery': 'partial' if any(r['delivery'] == 'partial' for r in group)
                          else 'unconfirmed' if any(r['delivery'] == 'unconfirmed' for r in group)
                          else 'observed'})
    return summaries


def write_tables(output, rows):
    output.mkdir(parents=True, exist_ok=True)
    fields = [*BASE_FIELDS, *sorted({key for row in rows for key in row} - set(BASE_FIELDS))]
    with (output / 'phases.tsv').open('w', newline='') as handle:
        writer = csv.DictWriter(handle, fields, delimiter='\t', lineterminator='\n')
        writer.writeheader()
        writer.writerows(rows)
    (output / 'workers.json').write_text(json.dumps(worker_summaries(rows), indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--invocation', action='append', required=True, metavar='LABEL=DIRECTORY')
    parser.add_argument('--capture', action='append', default=[], type=Path,
                        help='captured invocation/PID/subjob/run/K and completed stage required')
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if args.capture and len(args.invocation) != 1:
        parser.error('capture joins require exactly one invocation')
    captures = []
    for directory in args.capture:
        for path in sorted(directory.glob('*.json')):
            captures.append(json.loads(path.read_text()))
        for path in sorted(directory.glob('*.history.jsonl')):
            captures.extend(json.loads(line) for line in path.read_text().splitlines()
                            if line.strip())
    rows = []
    for spec in args.invocation:
        label, directory = spec.split('=', 1)
        rows.extend(build_rows(label, load_phase_records(directory), captures))
    write_tables(args.output, rows)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
