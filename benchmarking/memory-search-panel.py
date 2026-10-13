#!/usr/bin/env python3
"""Prepare an evidence-selected panel, rerun frozen inputs, and attribute search.

Selection and analysis are offline. Execution requires --execute and is serial.
The frozen binary's default portfolio and solver resource thresholds are preserved.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import csv
from dataclasses import asdict
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import threading
import time

from benchlib import (campaign_scope_guard, classify_run, load_phase_records, phase_record_dir,
                      run_systemd_scope)
from phase_attribution import classify_invocation

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('panel_phase_table',
                                            Path(__file__).with_name('legacy-phase-table.py'))
TABLE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TABLE)
# Same sensor and launch policy as the closing campaign's campaign-config.json.
COOLDOWN = {'sensor': '/sys/class/hwmon/hwmon6/temp1_input', 'max_millidegrees': 70000,
            'max_wait_s': 180, 'poll_s': 1, 'on_failure': 'stop before solver launch'}
SEARCH_CONSTRUCTION = {'K-search', 'IO/action construction', 'input push',
                       'preprocessing/Booleanization'}


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def tsv(path):
    with Path(path).open(newline='') as stream:
        return list(csv.DictReader(stream, delimiter='\t'))


def write_tsv(path, rows):
    fields = list(dict.fromkeys(key for row in rows for key in row))
    with Path(path).open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fields, delimiter='\t', lineterminator='\n')
        writer.writeheader()
        writer.writerows(rows)


def observation(row, source):
    directory = phase_record_dir(source.parent / 'phase-records', row['solver_label'],
                                 row['cap_s'], row['instance'], row['run_index'])
    records = load_phase_records(directory)
    phases = TABLE.build_rows(row['instance'], records)
    return {'row_source': str(source), 'row_sha256': digest(source),
            'phase_directory': str(directory), 'result': row['result'],
            'cap_s': row['cap_s'], 'seconds': row['seconds'],
            'scope_memory_peak_bytes': row.get('scope_memory_peak_bytes', ''),
            'obstruction': classify_invocation(row['result'], records, phases),
            'reached': sorted({p['plan_stage'] for p in phases}),
            'legacy_reached': sorted({p['plan_stage'] for p in phases
                                      if p.get('requested_backend') not in
                                      {'oxidd', 'obligation'}}),
            'legacy_unfinished': sorted({p['plan_stage'] for p in phases
                        if p['state'] in {'censored', 'unknown-completion'} and
                        p.get('requested_backend') not in {'oxidd', 'obligation'}}),
            'unfinished': sorted({p['plan_stage'] for p in phases
                                  if p['state'] in {'censored', 'unknown-completion'}}),
            'delivery': dict(Counter(p['delivery'] for p in phases))}


def select(candidates, limit):
    """Prioritize paired memory/construction evidence, then diversify observed stages.

    Ties use current source size and digest; identity names are never features.
    Global panel cap is evaluation capacity, not a solver threshold.
    """
    def order(row):
        return (-row['old_memory'], -row['construction_to_search'], -row['paired_gain'],
                -row['old_construction'], row['source_bytes'], row['source_sha256'])

    ranked = sorted(candidates, key=order)
    preferred = [r for r in ranked if r['old_memory'] or r['paired_gain']]
    selected = preferred[:limit]
    # Reserve up to three fresh construction/search strata for jobs outside
    # the older screens. The rest prioritize the requested historic transition.
    fresh = [r for r in ranked if not r['legacy']]
    historic_cap = limit - min(3, len(fresh))
    for pool, capacity in [([r for r in ranked if r['legacy']], historic_cap),
                           (fresh, limit), (ranked, limit)]:
        strata = defaultdict(list)
        for row in pool:
            if row not in selected:
                strata[(row['construction_to_search'], row['old_construction'],
                        tuple(row['unfinished']), row['source_bytes'].bit_length())].append(row)
        while strata and len(selected) < capacity:
            for key in sorted(strata, reverse=True):
                selected.append(strata[key].pop(0))
                if not strata[key]:
                    del strata[key]
                if len(selected) == capacity:
                    break
    return selected


def prepare(args):
    campaign = args.campaign.resolve()
    freeze_path = campaign / 'candidate-freeze.json'
    freeze = json.loads(freeze_path.read_text())
    binary = args.binary.resolve()
    if digest(binary) != freeze['binary_sha256']:
        raise ValueError('binary does not match closing candidate freeze')
    seventeen = campaign / '17/candidate-17s.tsv'
    sixty = campaign / 'measured/candidate-60s.tsv'
    rows17 = {r['instance']: r for r in tsv(seventeen)}
    rows60 = {r['instance']: r for r in tsv(sixty)}
    if set(rows17) != set(rows60):
        raise ValueError('fresh 17 s / 60 s membership differs')
    old = defaultdict(list)
    for directory in sorted(args.legacy.glob('i208-*')):
        if not directory.is_dir() or not (directory / 'master.tsv').is_file():
            continue
        compact = {r['instance']: r for r in tsv(directory / 'compact.tsv')}
        for row in tsv(directory / 'master.tsv'):
            recdir = phase_record_dir(directory / 'phases', row['solver_label'], row['cap_s'],
                                      row['instance'], row['run_index'])
            phases = TABLE.build_rows('old', load_phase_records(recdir))
            old[row['instance']].append({
                'row_source': str(directory / 'master.tsv'),
                'row_sha256': digest(directory / 'master.tsv'), 'result': row['result'],
                'phase_directory': str(recdir),
                'unfinished': sorted({p['plan_stage'] for p in phases
                                      if p['state'] in {'censored', 'unknown-completion'}
                                      and p.get('requested_backend') not in
                                      {'oxidd', 'obligation'}}),
                'compact_result': compact[row['instance']]['result'],
                'compact_row_source': str(directory / 'compact.tsv'),
                'compact_row_sha256': digest(directory / 'compact.tsv')})
    candidates, classification = [], []
    for name, row in rows60.items():
        source60 = (sixty.parent / row['source_run']).resolve()
        if not source60.is_relative_to(campaign):
            raise ValueError('fresh 60 s source escapes campaign')
        if digest(source60) != row['source_run_sha256']:
            raise ValueError('fresh 60 s raw source digest differs')
        if row['provenance'] != 'measured-60s' or row['source_cap_s'] != '60':
            raise ValueError('requires fresh measured 60 s rows')
        if row['binary_sha256'] != freeze['binary_sha256'] or (
                rows17[name]['binary_sha256'] != freeze['binary_sha256']):
            raise ValueError('fresh row binary pin differs')
        observed = [observation(rows17[name], seventeen), observation(row, source60)]
        classification.extend({'instance': name, 'cap_s': o['cap_s'], 'result': o['result'],
                               'invocation_obstruction': o['obstruction'],
                               'phase_directory': o['phase_directory'],
                               'delivery': json.dumps(o['delivery'])} for o in observed)
        at_cap = [o for o in observed if o['result'] in {'TIMEOUT', 'MEMOUT'}
                  and SEARCH_CONSTRUCTION.intersection(o['legacy_unfinished'])]
        prior = old[name]
        paired_gain = any(o['result'] not in {'REALIZABLE', 'UNREALIZABLE'} and
                          o['compact_result'] in {'REALIZABLE', 'UNREALIZABLE'} for o in prior)
        if not at_cap and not paired_gain:
            continue
        source = (args.corpus / row['tlsf_file']).resolve()
        if not source.is_relative_to(args.corpus.resolve()):
            raise ValueError('input escapes corpus')
        old_memory = any(o['result'] == 'MEMOUT' for o in prior)
        old_construction = any(SEARCH_CONSTRUCTION.intersection(o['unfinished']) - {'K-search'}
                               for o in prior)
        reached_search = any('K-search' in o['legacy_reached'] for o in observed)
        candidates.append({'instance': name, 'tlsf_file': row['tlsf_file'],
                           'source': str(source), 'source_sha256': digest(source),
                           'source_bytes': source.stat().st_size, 'old_memory': old_memory,
                           'old_construction': old_construction,
                           'construction_to_search': old_construction and reached_search,
                           'paired_gain': paired_gain,
                           'unfinished': sorted({s for o in at_cap for s in o['legacy_unfinished']}),
                           'closing': observed, 'legacy': prior})
    selected = select(candidates, args.limit)
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = {'schema': 1, 'binary': str(binary), 'binary_sha256': digest(binary),
                'freeze_source': str(freeze_path), 'freeze_sha256': digest(freeze_path),
                'default_arms': freeze['default_arms'], 'flags': [],
                'cap_s': 60, 'memory_max': '8G', 'memory_swap_max': '0',
                'solver_visible_deadline': False, 'candidates': len(candidates),
                'selection': 'paired memory/construction/gain priority; evidence/size strata; '
                             'source bytes/digest ties; names identify rows only',
                'jobs': selected}
    (args.output / 'panel.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (args.output / 'panel.list').write_text(''.join(r['instance'] + '\n' for r in selected))
    write_tsv(args.output / 'classification.tsv', classification)
    write_tsv(args.output / 'panel.tsv', [{k: json.dumps(v) if isinstance(v, (list, dict)) else v
                                        for k, v in row.items()} for row in selected])
    print(f'{len(selected)} selected from {len(candidates)} evidence-qualified inputs')


class ScopeSamples:
    """External 20 ms scope memory samples; bracketed values, never exact RSS."""
    def __init__(self, phases, output):
        self.phases, self.output = phases, output
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self.collect, daemon=True)

    def collect(self):
        cgroup = None
        with self.output.open('w') as stream:
            while not self.stop.is_set():
                if cgroup is None:
                    for path in self.phases.glob('*.jsonl'):
                        try:
                            for line in Path(f'/proc/{path.stem}/cgroup').read_text().splitlines():
                                if line.startswith('0::'):
                                    cgroup = Path('/sys/fs/cgroup') / line[3:].lstrip('/')
                                    break
                        except OSError:
                            continue
                        if cgroup:
                            break
                if cgroup:
                    start = time.monotonic_ns()
                    try:
                        current = int((cgroup / 'memory.current').read_text())
                        peak = int((cgroup / 'memory.peak').read_text())
                        stream.write(json.dumps({'start_ns': start, 'end_ns': time.monotonic_ns(),
                                                 'scope_memory_current_bytes': current,
                                                 'scope_memory_peak_bytes': peak,
                                                 'memory_cgroup': str(cgroup)}) + '\n')
                        stream.flush()
                    except OSError:
                        pass
                self.stop.wait(0.02)

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *unused):
        self.stop.set()
        self.thread.join()


def run(args):
    if args.execute:
        with campaign_scope_guard('memory-search-panel'):
            run_serial(args)
    else:
        run_serial(args)


def cooldown(log, identity, *, now=time.monotonic, sleep=time.sleep):
    """Wait for the campaign package sensor; log readiness or stop before launch."""
    start = now()
    while True:
        elapsed = now() - start
        try:
            value = int(Path(COOLDOWN['sensor']).read_text().strip())
            if not 0 < value <= 150000:
                raise ValueError('invalid CPU temperature')
        except (OSError, ValueError) as error:
            state, value = f'sensor-error: {error}', None
        else:
            state = 'ready' if value <= COOLDOWN['max_millidegrees'] else 'waiting'
        if state == 'waiting' and elapsed >= COOLDOWN['max_wait_s']:
            state = 'max-wait-exceeded'
        with log.open('a') as stream:
            stream.write(json.dumps({**identity, 'utc': datetime.now(timezone.utc).isoformat(),
                                     'temperature_millidegrees': value, 'elapsed_s': elapsed,
                                     'state': state}) + '\n')
        if state == 'ready':
            return
        if state != 'waiting':
            raise ValueError(f'cooldown stopped before invocation: {state}')
        sleep(min(COOLDOWN['poll_s'], COOLDOWN['max_wait_s'] - elapsed))


def write_json(path, value):
    temporary = path.with_name('.' + path.name + '.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n')
    temporary.replace(path)


def attempt_identity(job, plan, index, attempt):
    return {**job, **plan, 'job_index': index, 'attempt': attempt,
            'invocation': f"{plan['treatment']}:{index}:{job['source_sha256'][:16]}:{attempt}"}


def saved_attempts(output, index, job, plan):
    """Validate every retained attempt before skipping or retrying a job."""
    base = output / f'job-{index:02d}'
    directories = ([base] if base.exists() else []) + sorted(
        output.glob(f'{base.name}-attempt-*'))
    attempts = []
    for directory in directories:
        attempt = 1 if directory == base else int(directory.name.split('-attempt-')[1])
        identity = attempt_identity(job, plan, index, attempt)
        header = directory / 'attempt.json'
        completed = directory / 'run.json'
        # A kill before header publication can leave only its temporary file.
        if not header.exists() and any(p.name != '.attempt.json.tmp'
                                       for p in directory.iterdir()):
            raise ValueError(f'missing attempt provenance: {directory}')
        for path in (header, completed):
            if path.exists():
                saved = json.loads(path.read_text())
                if any(saved.get(key) != value for key, value in identity.items()):
                    raise ValueError(f'saved attempt provenance changed: {path}')
                if path == completed and (not saved.get('run', {}).get('memory_cgroup')
                                          or not saved.get('result')):
                    raise ValueError(f'incomplete saved run: {path}')
        attempts.append((attempt, directory, completed.exists()))
    return attempts


def run_serial(args):
    manifest = json.loads(args.panel.read_text())
    binary = (args.observation_binary or Path(manifest['binary'])).resolve()
    if not args.observation_binary and digest(binary) != manifest['binary_sha256']:
        raise ValueError('frozen binary digest changed')
    jobs = manifest['jobs'][:1] if args.one else manifest['jobs']
    cap = args.cap or manifest['cap_s']
    plan = {'panel_sha256': digest(args.panel), 'binary': str(binary),
            'binary_sha256': digest(binary), 'cap_s': cap, 'flags': manifest['flags'],
            'treatment': 'supplemental-observation-build' if args.observation_binary else 'frozen-PMC4',
            'jobs': len(jobs), 'solver_visible_deadline': False,
            'memory_sample_period_s': 0.02, 'diagnostic_progress_every': 64,
            'memory_max': '8G', 'memory_swap_max': '0', 'cooldown': COOLDOWN}
    plan_path = args.output / 'run-plan.json'
    if plan_path.exists():
        if json.loads(plan_path.read_text()) != plan:
            raise ValueError('saved run plan provenance changed')
    elif args.resume:
        raise ValueError('--resume requires a saved run plan')
    elif any(args.output.glob('job-*')):
        raise ValueError('existing attempts have no saved run plan')
    scheduled = []
    # Preflight all inputs and attempts before writing or launching anything.
    for index, job in enumerate(jobs):
        source = Path(job['source'])
        if digest(source) != job['source_sha256']:
            raise ValueError('panel source digest changed')
        attempts = saved_attempts(args.output, index, job, plan)
        if attempts and not args.resume:
            raise ValueError('existing job attempts require --resume')
        if any(completed for _, _, completed in attempts):
            print(json.dumps({'instance': job['instance'], 'state': 'skipped-completed'}))
            continue
        attempt = max((n for n, _, _ in attempts), default=0) + 1
        directory = args.output.resolve() / f'job-{index:02d}'
        if attempt > 1:
            directory = directory.with_name(f'{directory.name}-attempt-{attempt:02d}')
        scheduled.append((index, job, attempt, directory))
    args.output.mkdir(parents=True, exist_ok=True)
    if not plan_path.exists():
        write_json(plan_path, plan)
    for index, job, attempt, directory in scheduled:
        source = Path(job['source'])
        command = [str(binary), *manifest['flags'], '-T', str(source)]
        print(json.dumps({'instance': job['instance'], 'cap_s': cap, 'command': command,
                          'attempt': attempt}))
        if not args.execute:
            continue
        identity = attempt_identity(job, plan, index, attempt)
        cooldown(args.output / 'cooldown.jsonl', identity)
        directory.mkdir()  # Retain interrupted attempts in their own directories.
        write_json(directory / 'attempt.json', identity)
        phases, captures = directory / 'phase-records', directory / 'captures'
        phases.mkdir()
        env = {k: v for k, v in os.environ.items() if not k.startswith(('ACACIA_',))}
        env.update(ACACIA_PHASE_RECORDS=str(phases),
                   ACACIA_PHASE_INVOCATION=identity['invocation'],
                   ACACIA_SPOT_CAPTURE_DIR=str(captures), ACACIA_SPOT_CAPTURE_HISTORY='1',
                   ACACIA_DIAG='1', ACACIA_DIAG_PROGRESS_EVERY='64',
                   ACACIA_DIAG_SUPPORT_DEMAND='1', TMPDIR=str(ROOT / 'build_scratch/i251/tmp'))
        start = time.monotonic_ns()
        with ScopeSamples(phases, directory / 'scope-samples.jsonl'):
            result = run_systemd_scope(command, cap, '8G', '0', env=env,
                                       unit_prefix='acacia-i251',
                                       scratch_root=ROOT / 'build_scratch/i251/scope-memory')
        (directory / 'stdout.log').write_text(result.stdout)
        (directory / 'stderr.log').write_text(result.stderr)
        if not result.memory_cgroup:
            raise RuntimeError('scope runner did not establish the required memory cgroup')
        metadata = {**identity, 'invocation_start_ns': start,
                    'result': 'MEMOUT' if result.resource_limited else classify_run(result, 'acacia'),
                    'run': asdict(result)}
        write_json(directory / 'run.json', metadata)
    if args.execute:
        analyze(args)


def complete_jsonl(path):
    """Keep complete prefix of a killed capture writer; do not repair torn lines."""
    data = path.read_bytes()
    lines = data.splitlines(keepends=True)
    return [json.loads(line) for line in lines if line.endswith(b'\n')]


def boundary_memory(when, samples):
    before = [s for s in samples if s['end_ns'] <= when]
    after = [s for s in samples if s['start_ns'] >= when]
    if not before or not after:
        return {'state': 'unknown', 'reason': 'no samples bracket verification entry'}
    left, right = before[-1], after[0]
    gap = right['end_ns'] - left['start_ns']
    if gap > 100_000_000:
        return {'state': 'unknown', 'reason': 'sample bracket exceeds global 100 ms bound',
                'gap_ns': gap}
    return {'state': 'bracketed', 'boundary_ns': when, 'gap_ns': gap,
            'before': left, 'after': right}


def k_attempts(search):
    """Keep attempt occurrences; use nested search only when its attempt is missing."""
    attempts = [r for r in search if r['stage'] == 'k-attempt']
    result = list(attempts)
    for row in search:
        if row['stage'] != 'search':
            continue
        matching = [r for r in attempts if all(r.get(k) == row.get(k) for k in
                    ('subjob', 'run_id', 'k')) and r['entry_ns'] <= row['entry_ns']]
        enclosing = max(matching, key=lambda r: r['entry_ns']) if matching else None
        if not enclosing or enclosing.get('completion_ns', row['entry_ns']) < row['entry_ns']:
            result.append(row)
    return sorted(result, key=lambda r: r['entry_ns'])


def analyze(args):
    jobs = []
    for directory in sorted(args.output.glob('job-*')):
        if not (directory / 'run.json').exists():
            continue  # Interrupted evidence remains separate from completed retries.
        meta = json.loads((directory / 'run.json').read_text())
        records = load_phase_records(directory / 'phase-records')
        captures = []
        for path in (directory / 'captures').glob('*.json'):
            captures.append(json.loads(path.read_text()))
        for path in (directory / 'captures').glob('*.history.jsonl'):
            captures.extend(complete_jsonl(path))
        phases = TABLE.build_rows(meta['invocation'], records, captures)
        TABLE.write_tables(directory / 'table', phases)
        events = [e for p in records.processes for e in p.events]
        samples = complete_jsonl(directory / 'scope-samples.jsonl')
        workers = []
        for pid, group in records.worker_events().items():
            rows = [r for r in phases if r['worker_pid'] == pid]
            search = [r for r in rows if r['stage'] in {'search', 'k-attempt'}
                      and r.get('entry_ns')]
            search.sort(key=lambda r: r['entry_ns'])
            first = []
            for row in search:
                if row.get('first_useful_action_query_ns'):
                    first.append({'definition': 'first completed dense rank/action application',
                                  'origin': 'invocation-launch (includes scope startup)',
                                  'search_elapsed_ns': int(row['first_useful_action_query_ns']) -
                                  row['entry_ns'],
                                  'ns': int(row['first_useful_action_query_ns']) -
                                  meta['invocation_start_ns'], 'stage_id': row['stage_id']})
                if row.get('first_useful_rank_query_ms'):
                    first.append({'definition': 'first successful oracle query using a rank',
                                  'origin': 'row-store factory start',
                                  'ms': row['first_useful_rank_query_ms'],
                                  'stage_id': row['stage_id']})
            snapshots = [e for e in group if e.get('event') == 'stage_metric' and (
                e.get('key') in {'query_snapshot_ns', 'completed_action_applications',
                                'query_rank_support', 'query_rank_maximum'} or
                e.get('key', '').startswith(('rank_support', 'losing_antichain', 'game_states')))]
            boundaries = [{'stage_id': r['stage_id'], 'k': r.get('k'),
                           **boundary_memory(r['entry_ns'], samples)} for r in rows
                          if r['stage'] in {'verification', 'certificate-check', 'target_check'}
                          and r.get('entry_ns') and r['state'] != 'inapplicable']
            workers.append({'worker_pid': pid,
                            'k_sequence': [{'k': r.get('k'), 'subjob': r.get('subjob'),
                                            'run_id': r.get('run_id'), 'stage_id': r['stage_id'],
                                            'state': r['state']} for r in k_attempts(search)],
                            'first_useful_query': first or 'unknown: no observed counter',
                            'completed_search_check_stages': [
                                {k: r.get(k) for k in ('stage', 'stage_id', 'k', 'reason', 'state',
                                                      'entry_source', 'entry_ns', 'completion_ns')}
                                for r in rows if r['plan_stage'] in {
                                    'K-search', 'independent verification'} and
                                r['state'] == 'completed'],
                            'rank_support_snapshots': snapshots or 'unknown: no observed snapshots',
                            'boundary_scope_memory': boundaries or 'unknown: no entered checker',
                            'delivery': sorted({r['delivery'] for r in rows})})
        # Keep diagnostics as their own timestamped/censored observations.
        diag_spec = importlib.util.spec_from_file_location('panel_diag',
                                    ROOT / 'benchmarking/run_diag_targets.py')
        diag = importlib.util.module_from_spec(diag_spec)
        diag_spec.loader.exec_module(diag)
        diagnostics = [r for line in (directory / 'stderr.log').read_text().splitlines()
                       if (r := diag.parse_diag(line))]
        job = {'instance': meta['instance'], 'result': meta['result'],
               'invocation': meta['invocation'], 'attempt': meta.get('attempt', 1),
               'attempt_directory': str(directory),
               'treatment': meta['treatment'], 'binary_sha256': meta['binary_sha256'],
               'invocation_obstruction': classify_invocation(meta['result'], records, phases),
               'workers': workers, 'diagnostic_snapshots': diagnostics,
               'scope_memory_peak_bytes': meta['run']['memory_peak_bytes'],
               'scope_memory_peak_source': meta['run']['scope_memory_peak_source'],
               'scope_memory_peak_missing_reason': meta['run']['scope_memory_peak_missing_reason'],
               'records': len(events)}
        jobs.append(job)
    (args.output / 'analysis.json').write_text(json.dumps(jobs, indent=2) + '\n')
    write_tsv(args.output / 'per-job.tsv', [{k: json.dumps(v) if isinstance(v, list) else v
                                          for k, v in job.items()} for job in jobs])
    lines = ['# Search attribution', '', f'Jobs observed: {len(jobs)}.', '',
             'Frozen PMC4 has compile-time diagnostics disabled. Missing dense first-query and',
             'support snapshots stay unknown. Supplemental binaries are a separate treatment.',
             'Censored stages are lower bounds. Stage summaries without monotonic endpoints',
             'are excluded from duration unions. Cgroup boundary memory uses external samples',
             'bracketing entry (20 ms polling, maximum 100 ms bracket), never process RSS.', '',
             '## Recommendation template', '',
             'Evidence job/worker/subjob/K: ___',
             'First useful query definition, origin and observed time: ___',
             'Completed searches/checks and censored stages: ___',
             'Rank/support growth and boundary scope memory (or unknown): ___',
             'Observed obstruction and competing native/legacy routes: ___',
             'Recommendation and evidence needed to validate it: ___',
             'K / row / memo budget decision: leave unchanged pending driver review.', '']
    (args.output / 'REPORT.md').write_text('\n'.join(lines))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    prep = commands.add_parser('prepare')
    prep.add_argument('--campaign', type=Path, required=True)
    prep.add_argument('--legacy', type=Path, required=True)
    prep.add_argument('--corpus', type=Path, required=True)
    prep.add_argument('--binary', type=Path, required=True)
    prep.add_argument('--limit', type=int, default=30)
    prep.add_argument('--output', type=Path, required=True)
    running = commands.add_parser('run')
    running.add_argument('--panel', type=Path, required=True)
    running.add_argument('--output', type=Path, required=True)
    running.add_argument('--cap', type=float)
    running.add_argument('--one', action='store_true')
    running.add_argument('--execute', action='store_true')
    running.add_argument('--resume', action='store_true',
                         help='validate the plan, skip finished jobs and retry interruptions')
    running.add_argument('--observation-binary', type=Path)
    analysis = commands.add_parser('analyze')
    analysis.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.command == 'prepare' and not 1 <= args.limit <= 32:
        parser.error('panel limit must be between 1 and 32')
    if args.command == 'run' and args.cap is not None and args.cap <= 0:
        parser.error('cap must be positive')
    {'prepare': prepare, 'run': run, 'analyze': analyze}[args.command](args)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
