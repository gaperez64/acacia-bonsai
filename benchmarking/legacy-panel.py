#!/usr/bin/env python3
"""Select a seeded structural panel from explicit coverage rows and phase evidence.

No instance-specific selectors: gains come from a paired evidence comparison.
Historical comparator timings are disclosed and never called a matched campaign.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import importlib.util
import json
import random
import re
from collections import Counter
from pathlib import Path

from benchlib import load_phase_records, phase_record_dir


def classification_tools():
    spec = importlib.util.spec_from_file_location('legacy_classification',
                                                Path(__file__).with_name('classify-coverage.py'))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def read_rows(path):
    with path.open(newline='') as stream:
        rows = list(csv.DictReader(stream, delimiter='\t'))
    indexed = {}
    for row in rows:
        key = row['instance']
        if key in indexed:
            raise ValueError(f'{path}: duplicate instance')
        indexed[key] = row
    return indexed


def solved(row):
    return (row.get('result') in {'REALIZABLE', 'UNREALIZABLE'}
            and float(row['seconds']) < float(row['cap_s'])
            and row.get('timed_out', 'false').lower() == 'false'
            and row.get('exit_code') == ('0' if row['result'] == 'REALIZABLE' else '1'))


def refresh(metadata, acacia, comparator, baseline):
    if set(acacia) != set(comparator) or set(acacia) != set(metadata):
        raise ValueError('coverage and structural metadata membership differ')
    output = []
    for name, source in metadata.items():
        a, c = acacia[name], comparator[name]
        if float(a['cap_s']) != float(c['cap_s']):
            raise ValueError('coverage rows have different caps')
        answer_a, answer_c = solved(a), solved(c)
        if answer_a and answer_c and a['result'] != c['result']:
            raise ValueError('opposing solved verdicts')
        cohort = ('common-solved' if answer_c else 'Acacia-only') if answer_a else (
            'ltlsynt-only' if answer_c else 'both-unsolved')
        polarity = (a['result'] if answer_a else c['result'] if answer_c else 'UNKNOWN')
        row = {**source, 'set': cohort, 'polarity': polarity,
               'acacia_result': a['result'], 'acacia_seconds': a['seconds'],
               'comparator_result': c['result'], 'comparator_seconds': c['seconds'],
               'cap_s': a['cap_s'], 'native_rejections': [], 'legacy_last_stages': [],
               'equivariance_gain': bool(baseline and answer_a and not solved(baseline[name])),
               'near_cap': cohort == 'common-solved' and max(float(a['seconds']),
                          float(c['seconds'])) >= 0.8 * float(a['cap_s'])}
        for prefix, evidence in [('acacia', a), ('comparator', c)]:
            row[prefix + '_binary_sha256'] = evidence.get('binary_sha256', '')
            row[prefix + '_acacia_sha'] = evidence.get('acacia_sha', '')
            row[prefix + '_provenance'] = 'measured-input-rows'
        output.append(row)
    return output


def add_phase_evidence(rows, acacia, phase_root):
    tool = classification_tools()
    for row in rows:
        evidence = acacia[row['instance']]
        directory = phase_record_dir(phase_root, evidence['solver_label'], evidence['cap_s'],
                                     row['instance'], evidence['run_index'])
        records = load_phase_records(directory)
        stages, reasons = set(), set()
        workers = {}
        for process in records.processes:
            for event in process.events:
                if 'worker_pid' in event:
                    workers.setdefault(str(event['worker_pid']), []).append(event)
                phase = event.get('phase', '')
                if (phase.startswith(('reduce_decline_', 'lift_decline_'))
                        or phase == 'budget_decline'):
                    reasons.add(tool.phase_attribution([event])['first_stop_reason'])
        for events in workers.values():
            events.sort(key=lambda e: (e.get('mono_ns', 0), e.get('seq', 0)))
            backend = next((e.get('requested_backend') for e in events
                            if e.get('requested_backend')), '')
            last = next((e for e in reversed(events) if e.get('stage')), {})
            if backend in {'backward', 'forward', 'spot-guarded-sparse', 'spot-guarded'}:
                # Completed winners are preservation evidence; stopped/unfinished
                # workers supply the cost frontier's last observed stages.
                if last.get('stage') != 'verified':
                    stages.add(f"{backend}:{last['stage']}" if last.get('stage') else 'unknown')
            else:
                for event in events:
                    if event.get('event') in {'decline', 'route_stopped'}:
                        reasons.add(event.get('reason', 'unknown'))
        row['native_rejections'] = sorted(reasons)
        row['legacy_last_stages'] = sorted(stages)
        row['phase_evidence'] = str(directory)
        events = [e for p in records.processes for e in p.events]
        writers = [e for e in events if e.get('event') == 'writer_summary']
        confirmed = len(writers) == 1 and writers[0].get('incomplete_packet') is False
        gap = False
        for group in workers.values():
            seq = sorted({int(e['seq']) for e in group if 'seq' in e})
            gap |= bool(seq and (seq[0] != 1 or any(b != a + 1 for a, b in zip(seq, seq[1:]))))
        partial = (records.dropped or gap
                   or any(e.get('dropped_records', 0) for e in events)
                   or any(e.get('failed_records', 0) or e.get('incomplete_packet')
                          for e in writers))
        row['phase_delivery'] = 'partial' if partial else (
            'observed' if confirmed else 'unconfirmed' if records.processes else 'absent')


def features(row):
    tags = set()
    if row['set'] == 'ltlsynt-only':
        tags.add('missing-polarity:' + row['polarity'])
    if row['set'] in {'ltlsynt-only', 'both-unsolved'}:
        tags.update('native-rejection:' + value for value in row['native_rejections'])
        tags.update('legacy-last-stage:' + value for value in row['legacy_last_stages'])
    if row['set'] == 'Acacia-only':
        tags.add('Acacia-only-preservation')
    if row['near_cap']:
        tags.add('near-cap-common-solve')
    if row['equivariance_gain']:
        tags.add('paired-allowance-gain')
    return tags


def add_structural_digests(rows, source_root):
    """Hash source tokens, omitting comments, quoted descriptive text and whitespace."""
    root = source_root.resolve()
    for row in rows:
        path = (root / row['tlsf_file']).resolve()
        if not path.is_relative_to(root):
            raise ValueError('source escapes source root')
        content = path.read_bytes()
        if hashlib.sha256(content).hexdigest() != row['source_sha256']:
            raise ValueError('source digest differs from structural metadata')
        clean = re.sub(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*.*?\*/', '',
                       content.decode(), flags=re.S)
        tokens = re.findall(r'\w+|[^\s]', clean)
        row['source_structural_sha256'] = hashlib.sha256(
            json.dumps(tokens, separators=(',', ':')).encode()).hexdigest()


def structural_sample(rows, size, seed):
    """Seeded round-robin strata; structural/evidence ordering with source-token ties."""
    def order(row):
        if not row.get('source_structural_sha256'):
            raise ValueError('source structural digest is required')
        return (int(row['tlsf_bytes']), int(row['inputs']), int(row['outputs']),
                int(row['parameter_dimension']), row.get('semantics', ''),
                row.get('effective_target', ''), tuple(sorted(features(row))),
                row['source_structural_sha256'])

    rng = random.Random(seed)
    strata = {}
    for row in sorted(rows, key=order):
        key = (int(row['tlsf_bytes']).bit_length() - 1,
               (int(row['inputs']) + int(row['outputs'])).bit_length() - 1,
               int(row['parameter_dimension']))
        strata.setdefault(key, []).append(row)
    keys = sorted(strata)
    for key in keys:
        rng.shuffle(strata[key])
    rng.shuffle(keys)
    chosen = []
    while keys and len(chosen) < size:
        for key in keys[:]:
            chosen.append({**strata[key].pop(), 'sample_stratum': json.dumps(key),
                           'sample_seed': seed, 'sample_rank': len(chosen) + 1})
            if not strata[key]:
                keys.remove(key)
            if len(chosen) == size:
                break
    return chosen


def select(rows, size, seed, open_count=4):
    if not 24 <= size <= 32:
        raise ValueError('panel size must be between 24 and 32')
    ranked = structural_sample(rows, len(rows), seed)
    selected, reasons = {}, {}

    def choose(row, reason):
        name = row['instance']
        selected[name] = row
        reasons.setdefault(name, set()).add(reason)

    for row in ranked:
        if row['equivariance_gain']:
            choose(row, 'paired-allowance-gain')
    for row in structural_sample([r for r in rows if r['set'] == 'both-unsolved'
                                       and r['comparator_result'] != 'SYFCO-FAIL'],
                                      open_count, seed + 1):
        choose(row, 'seeded-both-unsolved-structural-sample')
    for offset, (reason, pool) in enumerate([
        ('Acacia-only-preservation', [r for r in rows if r['set'] == 'Acacia-only']),
        ('near-cap-common-solve', [r for r in rows if r['near_cap']]),
    ]):
        for row in structural_sample(pool, min(3, len(pool)), seed + 2 + offset):
            choose(row, reason)
    required = set().union(*(features(r) for r in rows))
    covered = set().union(*(features(r) for r in selected.values())) if selected else set()
    while required - covered:
        remaining = [r for r in ranked if r['instance'] not in selected]
        best = max(remaining, key=lambda r: len(features(r) - covered), default=None)
        if best is None or not features(best) - covered or len(selected) >= size:
            raise ValueError('panel cannot cover observed strata at this size')
        for reason in features(best) - covered:
            choose(best, reason)
        covered |= features(best)
    # Fill hard frontier first, round-robin on the same disclosed structural bins.
    pool = [r for r in ranked if r['set'] == 'ltlsynt-only' and r['instance'] not in selected]
    pool += [r for r in ranked if r['set'] == 'Acacia-only' and r['instance'] not in selected]
    pool += [r for r in ranked if r['near_cap'] and r['instance'] not in selected]
    for row in pool:
        if len(selected) >= size:
            break
        choose(row, 'seeded-structural-frontier' if row['set'] == 'ltlsynt-only'
               else 'preservation-structural-sample')
    if len(selected) != size:
        raise ValueError('not enough eligible evidence rows')
    return [{**row, 'selection_reasons': sorted(reasons[name])}
            for name, row in selected.items()], sorted(required)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--acacia', required=True, type=Path)
    parser.add_argument('--comparator', required=True, type=Path)
    parser.add_argument('--classification', required=True, type=Path)
    parser.add_argument('--baseline', type=Path, help='paired unbounded-allowance rows')
    parser.add_argument('--phase-root', required=True, type=Path)
    parser.add_argument('--source-root', required=True, type=Path,
                        help='TLSF sources matching structural metadata digests')
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--size', type=int, default=28)
    parser.add_argument('--seed', type=int, default=207)
    args = parser.parse_args()
    a, c, meta = (read_rows(path) for path in (args.acacia, args.comparator, args.classification))
    baseline = read_rows(args.baseline) if args.baseline else None
    if baseline and set(baseline) != set(a):
        raise ValueError('paired baseline membership differs')
    rows = refresh(meta, a, c, baseline)
    add_phase_evidence(rows, a, args.phase_root)
    add_structural_digests(rows, args.source_root)
    panel, required = select(rows, args.size, args.seed)
    args.output.mkdir(parents=True, exist_ok=True)
    inputs = {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
              for path in (args.acacia, args.comparator, args.classification, args.baseline)
              if path}
    phase_hash = hashlib.sha256()
    for path in sorted(args.phase_root.rglob('*.jsonl')):
        phase_hash.update(str(path.relative_to(args.phase_root)).encode())
        phase_hash.update(path.read_bytes())
    manifest = {'seed': args.seed, 'size': args.size, 'input_sha256': inputs,
                'phase_root': str(args.phase_root), 'phase_tree_sha256': phase_hash.hexdigest(),
                'source_root': str(args.source_root),
                'source_tie_break':
                    'SHA-256 of source tokens without comments/quoted text/whitespace',
                'selection': 'structural/evidence ordering, source-token digest ties; '
                             'seeded round-robin log2(bytes), log2(AP), parameter dimension; '
                             'greedy evidence stratum cover, then structural frontier fill',
                'near_cap_fraction': 0.8, 'both_unsolved_sample': 4,
                'preservation_samples_per_cohort': 3,
                'comparator_relation': 'historical comparator; membership refresh only',
                'refreshed_counts': dict(Counter(r['set'] for r in rows)),
                'required_observed_strata': required, 'rows': panel}
    (args.output / 'panel-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
