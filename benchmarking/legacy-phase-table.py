#!/usr/bin/env python3
"""Join existing phase transport records by invocation, physical worker, subjob and K.

Empty TSV cells are unknown. Censored durations are lower bounds, never completions.
Payload counters retain their recorded names; byte estimates are not RSS or heap sizes.
"""
from __future__ import annotations
from benchlib import phase_plan_stage
from benchlib import dispatch_mode
import argparse
from collections import Counter, defaultdict
import csv
import importlib.util
import json
import os
from pathlib import Path
from types import SimpleNamespace
import re
import shlex
import stat
import sys
import collections
from decimal import Decimal, InvalidOperation
from benchlib import load_phase_records

IDENTITY = ('invocation', 'worker_pid', 'worker', 'subjob', 'subjobs', 'run_id', 'k',
            'stage_id', 'stage', 'requested_backend', 'effective_backend',
            'original_polarity', 'proof_polarity', 'route')
BASE_FIELDS = (*IDENTITY, 'plan_stage', 'entry_ns', 'completion_ns', 'wall_ns', 'cpu_ns', 'peak_rss_kb',
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
            if 'entry_ns' not in row:
                row['entry_ns'] = event['entry_ns']
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
            # Occurrence IDs are unique within a worker. The shared snapshot
            # starts before the entry packet samples its own clock.
            active = end.get('stage_id') == row['stage_id']
            censored = active and end.get('signal', 0) > 0
            row['state'] = 'censored' if censored else 'unknown-completion'
            if censored and end.get('mono_ns', 0) >= row['entry_ns']:
                row['censored_lower_ns'] = end['mono_ns'] - row['entry_ns']
            row['reason'] = end.get('reason', 'signal') if censored else 'missing-completion'
    rows = list(stages.values())
    for row in rows:
        row['plan_stage'] = phase_plan_stage(row.get('stage', ''), row.get('route', ''))
    additions = attribution_native_rows(invocation, records)
    for row in additions:
        pid = row['worker_pid']
        seq = sorted(set(sequences[pid]))
        gap = bool(seq and (seq[0] != 1 or any(b != a + 1 for a, b in zip(seq, seq[1:]))))
        row['dropped_records'] = drops[pid]
        row['delivery'] = 'partial' if gap or drops[pid] or global_partial else (
            'observed' if writer_confirmed else 'unconfirmed')
    rows.extend(additions)
    join_captures(rows, captures)
    return sorted(rows, key=lambda r: (str(r['worker_pid']), (0, r['stage_id']) if isinstance(r['stage_id'], int)
                                           else (1, r['stage_id'])))


def join_captures(rows, captures):
    """Join only invocation-bound completed stages; historic guesses stay absent."""
    identity = ('invocation', 'worker_pid', 'subjob', 'run_id', 'k')
    prefixes = {
        'search': ('search_', 'wrapper_', 'rank_', 'subsumption_', 'scan_', 'losing_',
                   'proofs_', 'dependency_', 'queue_', 'first_useful_rank_query_'),
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
                                              {'completed', 'stopped', 'inapplicable', 'summary-only'}],
                          'summary_only_stages': [r for r in group
                                                  if r['state'] == 'summary-only'],
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
    dispatched = dispatch_mode(MODES, None)
    if dispatched is not None:
        return dispatched
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


attribution_NATIVE_BACKENDS = {'oxidd', 'obligation'}


def attribution_native_rows(invocation, records):
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
        if context.get('requested_backend') not in attribution_NATIVE_BACKENDS:
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


def workers_number(value, source, field):
    try:
        if isinstance(value, bool) or not isinstance(value, (str, int, float)):
            raise ValueError("expected a number or numeric string")
        result = Decimal(str(value))
        if not result.is_finite() or result < 0:
            raise ValueError("expected a finite nonnegative number")
        return result
    except (InvalidOperation, ValueError) as error:
        raise ValueError(f"{source}: invalid {field}={value!r}: {error}") from error


def workers_read_tsv(path, required):
    try:
        with path.open(encoding="utf-8", newline="") as stream:
            reader = csv.DictReader(stream, delimiter="\t", strict=True)
            missing = set(required) - set(reader.fieldnames or ())
            if missing:
                raise ValueError(f"missing columns: {', '.join(sorted(missing))}")
            rows = []
            for row in reader:
                if None in row or any(value is None for value in row.values()):
                    raise ValueError(f"line {reader.line_num}: wrong number of columns")
                for field in required:
                    if not row[field]:
                        raise ValueError(f"line {reader.line_num}: empty {field}")
                rows.append(row)
            return rows
    except (OSError, UnicodeError, csv.Error, ValueError) as error:
        raise ValueError(f"{path}: {error}") from error


def workers_markdown_table(headers, rows):
    def line(cells):
        return "| " + " | ".join(str(cell).replace("|", "\\|") for cell in cells) + " |"
    return [line(headers), line(["---"] * len(headers)), *(line(row) for row in rows), ""]


diagnostics_FIXPOINT_CHECKPOINTS = {
    "after-action-construction",
    "solve-loop",
    "after-symmetry-diagnostics",
    "classic-after-picker",
    "classic-after-cpre",
    "cpre-after-action",
    "cpre-before-intersection",
    "equivariant-after-picker",
    "equivariant-after-cpre",
    "equivariant-after-closure",
    "equivariant-after-k-bump",
}


diagnostics_ACTION_CHECKPOINTS = {
    "after-translation",
    "after-input-push",
    "after-spot-fast",
    "after-preprocessing",
    # solve_game() constructs I/O actions and the input picker before its
    # first loop.  A child whose last checkpoint is here is stuck in that
    # construction, not in the fixed point.
    "before-solve",
}


def diagnostics_integer(row: dict[str, str], field: str) -> int:
    try:
        return int(row.get(field, "") or 0)
    except ValueError:
        return 0


def diagnostics_number(row: dict[str, str], field: str) -> float:
    try:
        return float(row.get(field, "") or 0.0)
    except ValueError:
        return 0.0


def diagnostics_fixpoint_bucket(apply_ms: float, downset_ms: float) -> str:
    largest = max(apply_ms, downset_ms)
    smallest = min(apply_ms, downset_ms)
    if largest == 0.0 or (smallest > 0.0 and largest <= 1.2 * smallest):
        return "mixed"
    if apply_ms > downset_ms:
        return "letter-loop-bound"
    return "downset-bound"


def diagnostics_checkpoint_phase(checkpoint: str) -> str:
    if checkpoint in diagnostics_FIXPOINT_CHECKPOINTS or checkpoint.startswith("equivariant-"):
        return "fixpoint-bound"
    if checkpoint in diagnostics_ACTION_CHECKPOINTS:
        return "action-construction-bound"
    return "translation-bound"


diagnostics_PHASE_RANK = {"translation-bound": 0, "action-construction-bound": 1, "fixpoint-bound": 2}


def diagnostics_classify_child(rows: list[dict[str, str]]) -> tuple[str, dict[str, str]]:
    """Classify one forked solver child by the deepest checkpoint it reached."""
    ranked = {"translation-bound": 0, "action-construction-bound": 1, "fixpoint-bound": 2}
    candidates = []
    for row in rows:
        phase = diagnostics_checkpoint_phase(row.get("checkpoint", ""))
        candidates.append(
            (
                ranked[phase],
                diagnostics_integer(row, "loops"),
                diagnostics_integer(row, "total_ms"),
                phase,
                row,
            )
        )
    if not candidates:
        return "translation-bound", {}
    _, _, _, phase, representative = max(candidates, key=lambda item: item[:3])
    return phase, representative


def diagnostics_child_classifications(
    rows: list[dict[str, str]],
) -> list[tuple[str, dict[str, str]]]:
    grouped: dict[str, list[dict[str, str]]] = collections.defaultdict(list)
    for row in rows:
        # Real diagnostics always include a PID.  Keeping absent PIDs together
        # makes hand-written/test progress sequences describe a single child.
        grouped[row.get("pid", "")].append(row)
    return [diagnostics_classify_child(child_rows) for child_rows in grouped.values()]


def diagnostics_classify_target(rows: list[dict[str, str]]) -> tuple[str, dict[str, str]]:
    """Use the modal child phase; break equal splits toward the deeper phase."""
    children = diagnostics_child_classifications(rows)
    if not children:
        return "translation-bound", {}
    counts = collections.Counter(phase for phase, _ in children)
    phase = max(diagnostics_PHASE_RANK, key=lambda candidate: (counts[candidate], diagnostics_PHASE_RANK[candidate]))
    representatives = [row for child_phase, row in children if child_phase == phase]
    representative = max(
        representatives,
        key=lambda row: (diagnostics_integer(row, "total_ms"), diagnostics_integer(row, "loops")),
    )
    return phase, representative


def diagnostics_summarize_fixpoint_children(
    rows: list[dict[str, str]],
) -> tuple[str, dict[str, float | int]]:
    representatives = [
        row
        for phase, row in diagnostics_child_classifications(rows)
        if phase == "fixpoint-bound"
    ]
    apply_ms = sum(diagnostics_number(row, "apply_ms") for row in representatives)
    downset_ms = sum(diagnostics_number(row, "downset_ms") for row in representatives)
    values: dict[str, float | int] = {
        "cpre_ms": sum(diagnostics_number(row, "cpre_ms") for row in representatives),
        "picker_ms": sum(diagnostics_number(row, "picker_ms") for row in representatives),
        "apply_ms": apply_ms,
        "downset_ms": downset_ms,
        "actions_seen": sum(diagnostics_integer(row, "actions_seen") for row in representatives),
        "meets_computed": sum(diagnostics_integer(row, "meets_computed") for row in representatives),
        "max_f_size": max(
            (diagnostics_integer(row, "max_f_size") for row in representatives), default=0
        ),
    }
    # Weight workers by the work they actually performed.  A modal vote would
    # let two children that finish after a few milliseconds outvote the one
    # child that spends the entire target budget in the fixed point.  A live
    # intersection has not yet charged its scoped downset timer, so its
    # checkpoint is direct evidence of a downset stall.
    unfinished_intersection = any(
        row.get("checkpoint") == "cpre-before-intersection"
        for row in representatives
    )
    bucket = (
        "downset-bound"
        if unfinished_intersection
        else diagnostics_fixpoint_bucket(apply_ms, downset_ms)
    )
    return bucket, values


# benchmarking/weakening-census.py: retained policy and helpers.
def weakening_read_records(directory):
    records = []
    problems = []
    for path in sorted(Path(directory).glob("*.jsonl")):
        for number, line in enumerate(path.read_text().splitlines(), 1):
            try:
                row = json.loads(line)
                if not isinstance(row, dict):
                    raise ValueError("expected object")
            except ValueError:
                problems.append(f"{path.name}:{number}: invalid/truncated record")
                continue
            row = dict(row, emitter=path.stem)
            records.append(row)
    return records, problems


def weakening_census(records, problems=()):
    """Counts are exact only with confirmed delivery and a closed pre-pass."""
    writers = [r for r in records if r.get("event") == "writer_summary"]
    summaries = [r for r in records if r.get("phase") == "record_summary"]
    delivery = bool(writers and summaries) and not problems
    delivery &= all(r.get("failed_records") == 0 and r.get("incomplete_packet") is False
                    for r in writers)
    delivery &= all(r.get("dropped_records", 0) == 0 for r in records)
    producer_sequences = defaultdict(list)
    for row in records:
        if "seq" in row and row.get("observer") != "parent" and row.get("event") not in {
            "worker_spawn", "parent_terminal", "parent_winner", "stage_censored"
        }:
            producer_sequences[(row.get("emitter"), row.get("worker_pid"))].append(row["seq"])
    for sequences in producer_sequences.values():
        delivery &= sequences == list(range(1, sequences[-1] + 1))
    workers = {r["worker_pid"] for r in records if r.get("event") in {
        "worker_start", "parent_terminal", "weakening_entry"
    }}
    result = []
    for pid in sorted(workers):
        worker_rows = [r for r in records if r.get("worker_pid") == pid]
        passes = sorted({r["prepass"] for r in worker_rows if r.get("prepass")}) or [None]
        for prepass in passes:
            rows = [r for r in worker_rows if r.get("prepass") == prepass]
            eligibility = next((r for r in rows if r.get("event") == "weakening_eligibility"), {})
            starts = [r for r in rows if r.get("event") == "weakening_attempt_start"]
            ends = [r for r in rows if r.get("event") == "weakening_attempt_end"]
            generated = [r for r in rows if r.get("event") == "weakening_candidate_generated"]
            finish = next((r for r in rows if r.get("event") == "weakening_end"), {})
            fallback = next((r for r in rows
                             if r.get("event") == "weakening_fallback_start"), None)
            terminal = next((r for r in worker_rows if r.get("event") == "parent_terminal"), None)
            exact = bool(delivery and eligibility and terminal and
                         (finish or any(r.get("observer") == "parent" for r in ends)))
            generated_ids = {r.get("run_id") for r in generated}
            started_ids = {r.get("run_id") for r in starts}
            ended_ids = {r.get("run_id") for r in ends}
            exact &= len(generated) == eligibility.get("generated", -1)
            exact &= len(generated_ids) == len(generated)
            exact &= all(type(run_id) is int and run_id > 0 for run_id in generated_ids)
            exact &= len(started_ids) == len(starts) and len(ended_ids) == len(ends)
            exact &= started_ids <= generated_ids and started_ids == ended_ids
            mode = next((r.get("mode") for r in rows
                         if r.get("event") == "weakening_mode"), "basic")
            requested_mode = next((r.get("mode") for r in worker_rows
                                   if r.get("event") == "weakening_requested_mode"), mode)
            source = next((r for r in rows if r.get("event") == "weakening_source"), {})
            proofs = [r for r in rows if r.get("event") == "weakening_proof"]
            successful_ids = {r.get("run_id") for r in ends if r.get("outcome") == "proof"}
            proof_ids = {r.get("run_id") for r in proofs}
            successes = sum(r.get("outcome") == "proof" for r in ends)
            bindings = {(r.get("run_id"), r.get("kind")) for r in rows
                        if r.get("event") == "weakening_binding"}
            exact &= len(proofs) == len(proof_ids) and proof_ids == successful_ids
            exact &= all((run_id, "candidate") in bindings for run_id in generated_ids)
            exact &= all((run_id, "runner_objective") in bindings for run_id in successful_ids)
            exact &= bool(source and (0, "simplified_original") in bindings)
            cancellations = sum(r.get("outcome") == "cancellation" for r in ends)
            starved = None
            if exact:
                if fallback:
                    remaining = fallback.get("remaining_ns")
                    starved = (remaining == 0 if remaining is not None else
                               False if fallback.get("deadline_ns") == 0 else None)
                elif successes or terminal.get("reason") == "winner_cancelled":
                    starved = False
                else:
                    # A closed lifecycle with no proof/fallback used up the
                    # worker's opportunity to run the original. External timeout
                    # reaches older binaries as "interrupted", without a local
                    # deadline; this is still starvation, not an observed fallback.
                    # Winner cancellation is the explicit exception: another arm
                    # discharged the invocation. Incomplete telemetry stays UNKNOWN.
                    exhausted = any(r.get("remaining_ns") == 0 for r in rows)
                    deadline = next((r.get("deadline_ns") for r in rows
                                     if r.get("event") == "weakening_entry"), 0)
                    exhausted |= bool(deadline and terminal.get("mono_ns", 0) >= deadline)
                    starved = (exhausted or terminal.get("reason") == "deadline" or
                               bool(starts) and terminal.get("reason") == "interrupted")
            completed = sum(r.get("observer") == "worker" for r in ends)
            cpu_known = all(r.get("cpu_ns") is not None for r in ends)
            result.append({
                "worker_pid": pid, "prepass": prepass, "mode": requested_mode,
                "effective_mode": mode,
                "source_fnv1a64": source.get("source_fnv1a64"),
                "eligible": eligibility.get("eligible"), "reason": eligibility.get("reason"),
                "largest_global_conjunction_size": eligibility.get(
                    "largest_global_conjunction_size"),
                "generated": eligibility.get("generated") if exact else None,
                "started": len(starts) if exact else None,
                "completed": completed if exact else None,
                "successes": successes if exact else None,
                "observed_generated": len(generated),
                "observed_started": len(starts), "observed_completed": completed,
                "observed_successes": successes,
                "attempt_wall_ns": sum(r["wall_ns"] for r in ends) if exact else None,
                "attempt_cpu_ns": sum(r["cpu_ns"] for r in ends) if exact and cpu_known else None,
                "prepass_wall_ns": finish.get("wall_ns"),
                "full_solver_started": True if fallback else False if exact else None,
                "full_solver_remaining_ns": fallback.get("remaining_ns") if fallback else None,
                "deadline_ns": next((r.get("deadline_ns") for r in rows
                                     if r.get("event") == "weakening_entry"), None),
                "prepass_outcome": finish.get("outcome"),
                "parent_reason": terminal.get("reason") if terminal else None,
                "full_solver_starved": starved,
                "cancellations": cancellations if exact else None,
                "observed_cancellations": cancellations,
                "observed_dropped_records": max((r.get("dropped_records", 0)
                                                 for r in worker_rows), default=0),
                "writer_failed_records": sum(r["failed_records"] for r in writers)
                if writers and all(type(r.get("failed_records")) is int for r in writers)
                else None,
                "delivery": "complete" if delivery else "incomplete",
                "census": "complete" if exact else "incomplete",
            })
    return result


def weakening_main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directories", type=Path, nargs="+")
    parser.add_argument("--output", type=Path, help="TSV output; default stdout")
    args = parser.parse_args(argv)
    rows = []
    for directory in args.directories:
        records, problems = weakening_read_records(directory)
        for problem in problems:
            print(f"{directory}: {problem}", file=sys.stderr)
        rows.extend(dict(invocation=str(directory), **r) for r in weakening_census(records, problems))
    if not rows:
        parser.error("no worker records; absence does not establish zero attempts")
    stream = args.output.open("w", newline="") if args.output else sys.stdout
    try:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]), delimiter="\t")
        writer.writeheader()
        writer.writerows({k: "UNKNOWN" if v is None else v for k, v in row.items()}
                         for row in rows)
    finally:
        if args.output:
            stream.close()
    return 0


# benchmarking/attribution-completeness.py: retained policy and helpers.
def completeness_load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


completeness_WORKER = SimpleNamespace(
    markdown_table=workers_markdown_table, number=workers_number, read_tsv=workers_read_tsv)
completeness_DIAG = SimpleNamespace(
    classify_target=diagnostics_classify_target,
    summarize_fixpoint_children=diagnostics_summarize_fixpoint_children)

completeness_ROOT = Path(__file__).resolve().parents[1]


completeness_CONFIG = completeness_load_module("attribution_config", completeness_ROOT / "scripts/acacia-config.py")


completeness_PARENT_EVENTS = {"worker_spawn", "worker_spec", "parent_verdict", "parent_terminal",
                 "parent_winner", "stage_censored"}


completeness_CONTEXT = ("requested_backend", "effective_backend", "original_polarity", "proof_polarity",
           "route", "stage", "reason", "r_prepass", "equivariance")


completeness_SOLVED = {"REALIZABLE": 0, "UNREALIZABLE": 1}


completeness_BOOLEAN_CONTEXT = ("r_prepass", "equivariance")


completeness_SPEC_FIELDS = ("kind", "requested_polarity", "translation", "transform", "provider")


completeness_MAX_RECORD_FILE_BYTES = 64 * 1024 * 1024


def completeness_location(record):
    return f"{record['_path']}:{record['_line']}"


def completeness_project(record):
    fields = ("event", "worker", "worker_pid", "seq", *completeness_CONTEXT, "exit_code", "signal",
              "telemetry", "dropped_records", "mono_ns", "kind", "requested_polarity",
              "translation", "transform", "provider")
    return {**{field: record[field] for field in fields if field in record},
            "evidence": completeness_location(record)}


def completeness_nonnegative_int(value):
    return type(value) is int and value >= 0


def completeness_phase_directory(root, row):
    def safe(value):
        return re.sub(r"[^A-Za-z0-9_.-]", "_", value)

    return root / safe(row["solver_label"]) / row["cap_s"] / (
        f"{safe(row['instance'])}-{row['run_index']}")


def completeness_flag_value(flags, name, default=None):
    value = default
    for index, flag in enumerate(flags):
        if flag == name:
            if index + 1 == len(flags):
                raise ValueError(f"{name} in campaign flags has no value")
            value = flags[index + 1]
        elif flag.startswith(name + "="):
            value = flag.split("=", 1)[1]
    return value


def completeness_expected_configuration(row):
    flags = shlex.split(row.get("flags", ""))
    options, presets = completeness_CONFIG.load_registry()
    values = None
    if row.get("preset") in presets["presets"] or row.get("preset") in presets.get("aliases", {}):
        values = completeness_CONFIG.normalize_preset(options, presets, row["preset"])
    context = {"r_prepass": True,
               "equivariance": values["enable_equivariant_solver"] if values else None}
    for field in completeness_BOOLEAN_CONTEXT:
        value = completeness_flag_value(flags, "--" + field.replace("_", "-"))
        if value is not None:
            if value not in ("on", "off"):
                raise ValueError(f"invalid --{field.replace('_', '-')}={value!r}")
            context[field] = value == "on"
    arms = completeness_flag_value(flags, "--arms", values["default_arms"] if values else None)
    if arms is None or arms == "":
        return None, context
    inventory = []
    for arm in arms.split(","):
        parts = arm.strip().split(":")
        if len(parts) not in (3, 4):
            raise ValueError(f"invalid arm specification {arm!r}")
        polarity, transform, backend = parts[:3]
        if polarity not in ("real", "unreal", "both"):
            raise ValueError(f"invalid arm polarity {arm!r}")
        native = transform in ("gr1", "gr1-lift", "gr1-real-lift", "param-lift")
        if native:
            if backend != "oxidd" or len(parts) != 3 or (
                    transform in ("gr1-lift", "gr1-real-lift") and polarity != "both") or (
                    transform == "param-lift" and polarity != "real"):
                raise ValueError(f"invalid native arm {arm!r}")
            specification = dict(kind=arm.strip(), translation="native", transform="exact",
                                 provider="native")
        else:
            transforms = {"real": ("small", "any"), "unreal": ("formula", "automaton")}
            if transform not in transforms.get(polarity, ()) or backend not in (
                    "backward", "forward", "spot-guarded", "spot-guarded-sparse"):
                raise ValueError(f"invalid legacy arm {arm!r}")
            provider = parts[3] if len(parts) == 4 else completeness_flag_value(
                flags, f"--{polarity}-provider", "frozen-graph")
            if provider not in ("frozen-graph", "spot-lazy", "spot-eager", "closure-buchi",
                                "closure-buchi-eager"):
                raise ValueError(f"invalid arm provider {arm!r}")
            translation = transform if polarity == "real" else (
                values["translation_pref"].split("+", 1)[0] if values else None)
            specification = dict(kind="legacy", translation=translation,
                                 transform="real" if polarity == "real" else transform,
                                 provider=provider)
        specification.update(requested_polarity="both" if polarity == "both" else polarity.upper(),
                             requested_backend=backend, **context)
        inventory.append(specification)
    return inventory, context


def completeness_expected_worker_count(row):
    inventory, _ = completeness_expected_configuration(row)
    return len(inventory) if inventory is not None else None


def completeness_audit_row(row, directory, expected_workers=None):
    issues = []
    inventory, requested_context = completeness_expected_configuration(row)

    def issue(dimension, reason, evidence):
        item = {"dimension": dimension, "reason": reason, "evidence": str(evidence)}
        if item not in issues:
            issues.append(item)

    records = []
    if not directory.is_dir():
        issue("delivery", "missing_phase_directory", directory)
    for path in sorted(directory.glob("*.jsonl")):
        try:
            emitter = int(path.stem.removesuffix("-final"))
            if emitter <= 0:
                raise ValueError("emitter PID must be positive")
            before = path.lstat()
            if not stat.S_ISREG(before.st_mode):
                issue("delivery", "non_regular_record_file", path)
                continue
            if before.st_size > completeness_MAX_RECORD_FILE_BYTES:
                issue("delivery", "record_file_too_large", path)
                continue
            # Nonblocking/no-follow open and fstat also cover replacement after lstat.
            descriptor = os.open(path, os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
            with os.fdopen(descriptor, "rb") as stream:
                opened = os.fstat(stream.fileno())
                if not stat.S_ISREG(opened.st_mode):
                    issue("delivery", "non_regular_record_file", path)
                    continue
                if (before.st_dev, before.st_ino) != (opened.st_dev, opened.st_ino):
                    issue("delivery", "record_file_changed", path)
                    continue
                if opened.st_size > completeness_MAX_RECORD_FILE_BYTES:
                    issue("delivery", "record_file_too_large", path)
                    continue
                data = stream.read(completeness_MAX_RECORD_FILE_BYTES + 1)
            if len(data) > completeness_MAX_RECORD_FILE_BYTES:
                issue("delivery", "record_file_too_large", path)
                continue
            lines = data.splitlines(keepends=True)
        except (OSError, ValueError) as error:
            issue("delivery", "unreadable_record_file", f"{path}: {error}")
            continue
        for index, line in enumerate(lines, 1):
            source = f"{path}:{index}"
            if not line.endswith(b"\n"):
                issue("delivery", "truncated_trailing_line", source)
                continue
            try:
                record = json.loads(line)
                if not isinstance(record, dict):
                    raise ValueError("expected a JSON object")
            except (UnicodeError, ValueError) as error:
                issue("delivery", "malformed_json", f"{source}: {error}")
                continue
            record.update(_emitter_pid=emitter, _path=str(path), _line=index,
                          _durable=path.stem.endswith("-final"))
            records.append(record)
            if "dropped_records" in record:
                drops = record["dropped_records"]
                if not completeness_nonnegative_int(drops):
                    issue("delivery", "invalid_drop_count", source)
                    issue("producer", "invalid_drop_count", source)
                elif drops:
                    issue("delivery", "producer_drops", f"{source}: dropped_records={drops}")
                    issue("producer", "producer_drops", f"{source}: dropped_records={drops}")

    writers = [r for r in records if r.get("event") == "writer_summary"]
    if len(writers) != 1:
        issue("delivery", "missing_writer_summary" if not writers else "duplicate_writer_summary",
              directory)
    for writer in writers:
        source = completeness_location(writer)
        failed = writer.get("failed_records")
        if not completeness_nonnegative_int(failed):
            issue("delivery", "invalid_writer_summary", source)
        elif failed:
            issue("delivery", "writer_failed_records", f"{source}: failed_records={failed}")
        if writer.get("incomplete_packet") is not False:
            issue("delivery", "writer_incomplete_packet", source)
        delivered = writer.get("delivered_records")
        confirmed = sum(not r.get("_durable", False) for r in records) - len(writers)
        if not completeness_nonnegative_int(delivered) or delivered != confirmed:
            issue("delivery", "delivered_count_mismatch",
                  f"{source}: delivered_records={delivered}, "
                  f"confirmed={confirmed}")

    workers = defaultdict(list)
    worker_indices = {r['worker_pid']: r['worker'] for r in records
                      if completeness_nonnegative_int(r.get('worker_pid')) and completeness_nonnegative_int(r.get('worker'))}
    parents = set()
    lifecycle = []
    for record in records:
        event = record.get("event")
        if event is None or event == "writer_summary":
            continue
        if not isinstance(event, str):
            issue("delivery", "invalid_event", completeness_location(record))
            continue
        # Accounting has its own phase/resource schema, audited by prof_lib.
        # Its records still participate in delivery validation above.
        if event in {"worker_reap", "worker_resource", "parent_record_summary",
                     "work_identity", "native_work_identity"} or event.startswith(
                         ("accounting_", "descendant_")):
            continue
        stage_record = event in {'stage_entry', 'stage_completion', 'stage_stopped',
                                 'stage_metric', 'stage_censored'}
        supplemental = stage_record or event.startswith('weakening_')
        if supplemental and 'worker' not in record:
            record['worker'] = worker_indices.get(record.get('worker_pid'))
        pid, index = record.get("worker_pid"), record.get("worker")
        if not completeness_nonnegative_int(pid) or pid == 0 or not completeness_nonnegative_int(index):
            issue("delivery", "invalid_worker_identity", completeness_location(record))
            continue
        workers[pid].append(record)
        lifecycle.append(record)
        parent_observation = event in completeness_PARENT_EVENTS or (
            event.startswith('weakening_') and record.get('observer') == 'parent')
        if parent_observation:
            parents.add(record["_emitter_pid"])
            if record["_emitter_pid"] == pid:
                issue("delivery", "wrong_parent_emitter", completeness_location(record))
        elif record["_emitter_pid"] != pid:
            issue("delivery", "wrong_child_emitter", completeness_location(record))
        if supplemental:
            if not completeness_nonnegative_int(record.get("seq")) or record["seq"] == 0:
                issue("delivery", "invalid_sequence", completeness_location(record))
            if not completeness_nonnegative_int(record.get("mono_ns")):
                issue("delivery", "invalid_event_context", completeness_location(record))
            if not completeness_nonnegative_int(record.get("dropped_records")):
                issue("delivery", "invalid_drop_count", completeness_location(record))
            if stage_record:
                valid = completeness_nonnegative_int(record.get('stage_id')) and record.get('stage_id', 0) > 0
                if event == 'stage_metric':
                    valid &= all(isinstance(record.get(k), str) for k in ('key', 'value'))
                else:
                    valid &= isinstance(record.get('stage'), str)
                if not valid:
                    issue('delivery', 'invalid_stage_record', completeness_location(record))
            continue
        if event != "worker_spec":
            if not completeness_nonnegative_int(record.get("seq")) or record["seq"] == 0:
                issue("delivery", "invalid_sequence", completeness_location(record))
            if any(field not in record for field in (*completeness_CONTEXT, "dropped_records", "mono_ns")):
                issue("delivery", "missing_event_context", completeness_location(record))
            if any(not isinstance(record.get(field), str) for field in completeness_CONTEXT[:7]):
                issue("delivery", "invalid_event_context", completeness_location(record))
            if not completeness_nonnegative_int(record.get("mono_ns")):
                issue("delivery", "invalid_event_context", completeness_location(record))
        context_type = "spec" if event == "worker_spec" else "event"
        if any(field not in record for field in completeness_BOOLEAN_CONTEXT):
            issue("delivery", f"missing_{context_type}_context", completeness_location(record))
        if any(type(record.get(field)) is not bool for field in completeness_BOOLEAN_CONTEXT):
            issue("delivery", f"invalid_{context_type}_context", completeness_location(record))
        expected = inventory[index] if inventory is not None and index < len(inventory) else None
        context = expected or requested_context
        fields = ((*completeness_BOOLEAN_CONTEXT, "requested_backend")
                  if expected and event != "worker_spec" else completeness_BOOLEAN_CONTEXT)
        for field in fields:
            value = context.get(field)
            if value is not None and record.get(field) != value:
                issue("delivery", "worker_context_mismatch",
                      f"{completeness_location(record)}: worker={index}, {field} expected={value!r}, "
                      f"recorded={record.get(field)!r}")
        if expected and event in ("worker_spawn", "worker_start") and (
                record.get("original_polarity") != expected["requested_polarity"]):
            issue("delivery", "worker_context_mismatch",
                  f"{completeness_location(record)}: worker={index}, expected polarity="
                  f"{expected['requested_polarity']!r}")

    parents.update(r["_emitter_pid"] for r in records
                   if r.get("arm") == "legacy_parent")
    if len(parents) != 1:
        issue("delivery", "missing_parent_identity" if not parents else "multiple_parents",
              directory)
    for parent in parents:
        summaries = [r for r in records if r["_emitter_pid"] == parent
                     and r.get("phase") == "record_summary"]
        if len(summaries) != 1:
            issue("delivery", "missing_parent_record_summary" if not summaries
                  else "duplicate_parent_record_summary", f"{directory}/{parent}.jsonl")
        elif not completeness_nonnegative_int(summaries[0].get("dropped_records")):
            issue("delivery", "invalid_drop_count", completeness_location(summaries[0]))
            issue("producer", "invalid_drop_count", completeness_location(summaries[0]))

    indices = [group[0]["worker"] for group in workers.values()]
    count = expected_workers if expected_workers is not None else (
        len(inventory) if inventory is not None else None)
    if count is None:
        count = max(indices) + 1 if indices else 0
    if not count or sorted(indices) != list(range(count)):
        issue("delivery", "worker_inventory_mismatch",
              f"{directory}: expected indices 0..{count-1}, observed={sorted(indices)}")
        issue("producer", "worker_inventory_unknown", directory)
    details = []
    for pid, group in sorted(workers.items(), key=lambda item: item[1][0]["worker"]):
        by_event = defaultdict(list)
        for record in group:
            by_event[record["event"]].append(record)
        child = [r for r in group if r["_emitter_pid"] == pid and r["event"] not in completeness_PARENT_EVENTS
                 and r.get('observer') != 'parent']
        child.sort(key=lambda r: r["_line"])
        terminals = by_event["parent_terminal"]
        terminal = terminals[0] if len(terminals) == 1 else None
        source = completeness_location(terminal) if terminal else f"{directory}: worker_pid={pid}"
        for event in ("worker_spawn", "worker_spec", "worker_start", "parent_terminal"):
            events = by_event[event]
            if len(events) != 1:
                issue("delivery", f"missing_{event}" if not events else f"duplicate_{event}",
                      source)
        if any(r["worker"] != group[0]["worker"] for r in group):
            issue("delivery", "worker_index_mismatch", source)
        for spec in by_event["worker_spec"]:
            if any(not isinstance(spec.get(field), str) or not spec[field]
                   for field in completeness_SPEC_FIELDS):
                issue("delivery", "invalid_worker_spec", completeness_location(spec))
            index = spec["worker"]
            if inventory is not None and index < len(inventory):
                for field in completeness_SPEC_FIELDS:
                    expected = inventory[index][field]
                    if expected is not None and spec.get(field) != expected:
                        issue("delivery", "worker_spec_mismatch",
                              f"{completeness_location(spec)}: worker={index}, {field} expected={expected!r}, "
                              f"recorded={spec.get(field)!r}")
            for record in group:
                for field in completeness_BOOLEAN_CONTEXT:
                    if type(spec.get(field)) is bool and type(record.get(field)) is bool and (
                            record[field] != spec[field]):
                        issue("delivery", "worker_context_mismatch",
                              f"{completeness_location(record)}: {field} disagrees with {completeness_location(spec)}")
        sequences = [r.get("seq") for r in child]
        if sequences != list(range(1, len(child) + 1)):
            issue("delivery", "sequence_gap", f"{directory}/{pid}.jsonl: seq={sequences}")
        for selection in by_event["parent_verdict"]:
            # Selection observes a paused child; its private snapshot must not
            # consume the sequence number of the child's next emitted record.
            before = [r for r in child if completeness_nonnegative_int(r.get("mono_ns")) and
                      completeness_nonnegative_int(selection.get("mono_ns")) and
                      r["mono_ns"] <= selection["mono_ns"]]
            last = before[-1].get("seq", 0) if before else 0
            if not completeness_nonnegative_int(last) or selection.get("seq") != last + 1:
                issue("delivery", "sequence_gap", completeness_location(selection))
            if selection.get("reason") != "selected" or selection.get("exit_code") not in (0, 1):
                issue("delivery", "invalid_parent_verdict", completeness_location(selection))
        # Spawn uses a pre-fork copy. Terminal resumes the reaped child's shared
        # sequence; comparing adjacent parent-file records would invent gaps.
        if terminal and terminal.get("telemetry") != "unavailable":
            last = sequences[-1] if sequences and completeness_nonnegative_int(sequences[-1]) else 0
            snapshots = [r for r in group if r['event'] == 'stage_censored' or
                         r['event'].startswith('weakening_') and r.get('observer') == 'parent']
            snapshots.sort(key=lambda r: (r.get('mono_ns', 0), r['_line']))
            for offset, snapshot in enumerate(snapshots, 1):
                if snapshot.get('seq') != last + offset:
                    issue('delivery', 'sequence_gap', completeness_location(snapshot))
            if terminal.get("seq") != last + len(snapshots) + 1:
                issue("delivery", "sequence_gap",
                      f"{source}: parent seq={terminal.get('seq')}, last child seq={last}")
        for winner in by_event["parent_winner"]:
            if terminal and completeness_nonnegative_int(terminal.get("seq")):
                if winner.get("seq") != terminal["seq"] + 1:
                    issue("delivery", "sequence_gap", completeness_location(winner))
        telemetry = terminal.get("telemetry", "unknown") if terminal else "unknown"
        if not isinstance(telemetry, str):
            issue("delivery", "invalid_telemetry", source)
            telemetry = "unknown"
        if telemetry != "complete":
            reason = {"incomplete": "worker_final_event_not_emitted", "dropped": "producer_drops",
                      "unavailable": "producer_metadata_unavailable"}.get(
                          telemetry, "missing_parent_terminal" if not terminal
                          else "invalid_telemetry")
            issue("producer", reason, source)
        final = by_event["terminal_result"]
        if telemetry == "complete":
            if len(final) != 1:
                issue("delivery", "missing_terminal_result" if not final
                      else "duplicate_terminal_result", source)
            summaries = [r for r in records if r["_emitter_pid"] == pid
                         and r.get("phase") == "record_summary"]
            if len(summaries) != 1:
                issue("delivery", "missing_worker_record_summary" if not summaries
                      else "duplicate_worker_record_summary", source)
            elif not completeness_nonnegative_int(summaries[0].get("dropped_records")):
                issue("delivery", "invalid_drop_count", completeness_location(summaries[0]))
                issue("producer", "invalid_drop_count", completeness_location(summaries[0]))
        elif telemetry == "dropped":
            issue("delivery", "producer_drops", source)
        elif telemetry == "incomplete" and final:
            issue("delivery", "terminal_telemetry_mismatch", source)
        for index, record in enumerate(child):
            if record["event"] == "route_selected":
                following = child[index + 1] if index + 1 < len(child) else None
                if not following or following["event"] != "route_start" or (
                        following.get("route") != record.get("route")):
                    # A kill can occur between selection and start; no missing
                    # delivery is established if this is the confirmed prefix.
                    if following or telemetry == "complete":
                        issue("delivery", "missing_route_start", completeness_location(record))
        if telemetry == "complete" and not by_event["route_selected"]:
            issue("delivery", "missing_route_selected", source)
        case = "unknown"
        if terminal:
            if telemetry == "complete":
                case = "finished"
            elif completeness_nonnegative_int(terminal.get("signal")) and terminal["signal"] > 0 and (
                    terminal.get("reason") in (
                    "deadline", "winner_cancelled", "interrupted")):
                case = "documented_kill"
            else:
                case = "unexplained_incomplete"
        latest = terminal or (child[-1] if child else by_event["worker_spawn"][0]
                              if by_event["worker_spawn"] else group[0])
        stops = [completeness_project(r) for r in child if r["event"] == "route_stopped"]
        if terminal and terminal.get("reason") != "exit":
            stops.append(completeness_project(terminal))
        details.append({"worker": group[0]["worker"], "worker_pid": pid,
                        "telemetry": telemetry, "producer_case": case,
                        "parent_only": not child, "last_observed": completeness_project(latest),
                        "spec": [completeness_project(r) for r in by_event["worker_spec"]],
                        "parent_terminal": completeness_project(terminal) if terminal else None,
                        "routes": [completeness_project(r) for r in child
                                   if r["event"] in ("route_selected", "route_start")],
                        "declines": [completeness_project(r) for r in child if r["event"] == "decline"],
                        "stops": stops})

    winners = [r for r in lifecycle if r["event"] == "parent_winner"]
    accepted = [r for r in winners if r.get("reason") == "accepted"]
    if row["result"] in completeness_SOLVED:
        if row.get("exit_code") and row["exit_code"] != str(completeness_SOLVED[row["result"]]):
            issue("delivery", "campaign_result_exit_mismatch", directory)
        if len(accepted) != 1:
            issue("delivery", "missing_accepted_winner" if not accepted
                  else "multiple_accepted_winners", directory)
        for winner in accepted:
            if winner.get("exit_code") != completeness_SOLVED[row["result"]] or row.get("timed_out") == "true":
                issue("delivery", "winner_result_mismatch", completeness_location(winner))
            if winner.get("worker_pid") not in workers or not any(
                    r["event"] == "parent_terminal" for r in workers[winner["worker_pid"]]):
                issue("delivery", "winner_missing_parent_terminal", completeness_location(winner))
    elif winners:
        issue("delivery", "unexpected_winner", completeness_location(winners[0]))
    if len(accepted) != len(winners):
        issue("delivery", "unaccepted_winner_record", directory)
    if not workers:
        issue("producer", "worker_inventory_unknown", directory)
    checkpoints = [{**r, "pid": str(r["_emitter_pid"])} for r in records if "checkpoint" in r]
    phase = completeness_DIAG.classify_target(checkpoints)[0] if checkpoints else "unknown"
    bucket = completeness_DIAG.summarize_fixpoint_children(checkpoints)[0] if phase == "fixpoint-bound" else ""
    states = {dimension: "incomplete" if any(i["dimension"] == dimension for i in issues)
              else "complete" for dimension in ("delivery", "producer")}
    return {"campaign": row, "phase_directory": str(directory), "confirmed_records": len(records),
            **states, "complete": not issues, "issues": issues,
            "accepted_winners": [completeness_project(r) for r in accepted], "workers": details,
            "expected_worker_specs": inventory,
            "checkpoint_phase": phase, "fixpoint_bucket": bucket}


def completeness_audit_campaign(campaign, root):
    results, directories = [], set()
    required = ("solver_label", "instance", "cap_s", "result", "run_index")
    for row in completeness_WORKER.read_tsv(campaign, required):
        for field in ("cap_s", "run_index"):
            value = completeness_WORKER.number(row[field], campaign, field)
            if value != value.to_integral_value() or (field == "cap_s" and value == 0):
                raise ValueError(f"{campaign}: invalid {field}={row[field]!r}")
            row[field] = str(int(value))
        directory = completeness_phase_directory(root, row)
        if directory in directories:
            raise ValueError(f"{campaign}: duplicate phase directory {directory}")
        directories.add(directory)
        results.append(completeness_audit_row(row, directory))
    return results


def completeness_summarize(results):
    summary = {"rows": len(results),
               "results": dict(Counter(r["campaign"]["result"] for r in results)),
               "complete": sum(r["complete"] for r in results),
               "incomplete": sum(not r["complete"] for r in results)}
    for dimension in ("delivery", "producer"):
        reasons = Counter(reason for r in results for reason in {
            i["reason"] for i in r["issues"] if i["dimension"] == dimension})
        complete = sum(r[dimension] == "complete" for r in results)
        summary[dimension] = {"complete": complete, "incomplete": len(results) - complete,
                              "incomplete_by_reason": dict(sorted(reasons.items()))}
    summary["by_result"] = {result: {"rows": len(group),
        "complete": sum(r["complete"] for r in group),
        "delivery_complete": sum(r["delivery"] == "complete" for r in group),
        "producer_complete": sum(r["producer"] == "complete" for r in group)}
        for result in sorted(summary["results"])
        for group in [[r for r in results if r["campaign"]["result"] == result]]}
    summary["worker_telemetry"] = dict(Counter(
        w["telemetry"] for r in results for w in r["workers"]))
    summary["worker_cases"] = dict(Counter(
        w["producer_case"] for r in results for w in r["workers"]))
    summary["accepted_winners"] = dict(Counter(
        "/".join(str(w.get(field, "unknown")) for field in (
            "original_polarity", "requested_backend", "effective_backend", "route"))
        for r in results for w in r["accepted_winners"]))
    return summary


def completeness_write_tsv(path, results):
    fields = ("solver_label", "instance", "cap_s", "run_index", "result", "acacia_sha",
              "binary_sha256", "delivery", "producer", "complete", "delivery_reasons",
              "producer_reasons", "phase_directory", "confirmed_records", "accepted_winners",
              "routes", "declines", "stops", "workers", "issues", "checkpoint_phase",
              "fixpoint_bucket", "expected_worker_specs")
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fields, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        for result in results:
            row = {field: result["campaign"].get(field, "") for field in fields[:7]}
            row.update({field: result[field] for field in fields[7:] if field in result})
            for field in ("routes", "declines", "stops"):
                row[field] = [e for worker in result["workers"] for e in worker[field]]
            for dimension in ("delivery", "producer"):
                row[f"{dimension}_reasons"] = ";".join(sorted({
                    i["reason"] for i in result["issues"]
                                                              if i["dimension"] == dimension}))
            for field in ("accepted_winners", "routes", "declines", "stops", "workers", "issues",
                          "expected_worker_specs"):
                row[field] = json.dumps(row[field], separators=(",", ":"), sort_keys=True)
            writer.writerow(row)


def completeness_make_report(results, summary):
    lines = ["# Campaign attribution completeness", "",
             f"Rows: {summary['rows']}; complete: {summary['complete']}; "
             f"incomplete: {summary['incomplete']}.", "",
             "Delivery and producer completeness are separate. Reason counts count rows once per "
             "reason and overlap. Only parent-accepted winners are reported as winners. Unknown "
             "routes remain unknown; missing events do not become declines.", "",
             "Worker inventories use explicit --arms or the current preset registry "
             "when available; "
             "otherwise all observed worker identities and contiguous launch indices are checked. "
             "Specs and requested context are compared per launch index. Runtime settings must be "
             "booleans in specs and lifecycle events. The inventory source is not proof of "
             "binary/configuration equivalence.", "",
             f"Only regular files are read, without following symlinks, with a global "
             f"{completeness_MAX_RECORD_FILE_BYTES}-byte limit per file; rejected destinations make "
             "delivery incomplete.", "",
             "Killed workers without final events remain producer-incomplete even with "
             "trustworthy "
             "parent metadata. Missing expected worker starts remain delivery-incomplete. "
             "A contiguous prefix ending at route selection can precede a kill before "
             "route start. "
             "Checkpoint buckets reuse the existing diagnostic reader and stay unknown "
             "when absent. "
             "Complete attribution alone does not authorize smaller-cap reuse.", ""]
    lines += completeness_WORKER.markdown_table(["Dimension", "Complete", "Incomplete"],
        [[d, summary[d]["complete"], summary[d]["incomplete"]] for d in ("delivery", "producer")])
    lines += completeness_WORKER.markdown_table(["Dimension", "Incomplete reason", "Rows"],
        [[d, reason, count] for d in ("delivery", "producer")
         for reason, count in summary[d]["incomplete_by_reason"].items()])
    lines += completeness_WORKER.markdown_table(["Result", "Rows", "Delivery complete", "Producer complete",
                                    "Both complete"],
        [[result, counts["rows"], counts["delivery_complete"], counts["producer_complete"],
          counts["complete"]] for result, counts in summary["by_result"].items()])
    lines += ["## Every incomplete row", ""]
    lines += completeness_WORKER.markdown_table(
        ["Run", "Instance", "Result", "Delivery", "Producer", "Reasons"],
        [[r["campaign"]["run_index"], r["campaign"]["instance"], r["campaign"]["result"],
          r["delivery"], r["producer"], "; ".join(sorted({
              i["dimension"] + ":" + i["reason"] for i in r["issues"]}))]
         for r in results if not r["complete"]])
    lines += ["## Accepted winner attribution", ""]
    lines += completeness_WORKER.markdown_table(
        ["Original polarity / requested / effective backend / route", "Rows"],
                                  sorted(summary["accepted_winners"].items()))
    lines += ["Per-row TSV JSON cells preserve winner, route, decline and stop events, "
              "worker specs, "
              "parent metadata and path:line evidence for every issue. Inputs are read only.", ""]
    return "\n".join(lines)


def completeness_main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("campaign_tsv", type=Path)
    parser.add_argument("--phase-records-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="Per-invocation TSV")
    parser.add_argument("--report", type=Path, required=True, help="Markdown completeness report")
    parser.add_argument("--summary", type=Path, required=True, help="Machine-readable counts JSON")
    args = parser.parse_args()
    try:
        outputs = [p.resolve() for p in (args.output, args.report, args.summary)]
        if len(set(outputs)) != len(outputs):
            raise ValueError("output paths must be distinct")
        sources = {args.campaign_tsv.resolve(), *(
            p.resolve() for p in args.phase_records_dir.rglob("*.jsonl"))}
        if any(p in sources or p.is_relative_to(args.phase_records_dir.resolve())
               for p in outputs):
            raise ValueError("outputs must not overwrite campaign inputs "
                             "or lie in the phase directory")
        results = completeness_audit_campaign(args.campaign_tsv.resolve(), args.phase_records_dir.resolve())
        summary = completeness_summarize(results)
        summary.update(campaign_tsv=str(args.campaign_tsv.resolve()),
                       phase_records_dir=str(args.phase_records_dir.resolve()))
        for path in outputs:
            path.parent.mkdir(parents=True, exist_ok=True)
        completeness_write_tsv(args.output, results)
        args.report.write_text(completeness_make_report(results, summary), encoding="utf-8")
        args.summary.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n",
                                encoding="utf-8")
    except (OSError, ValueError) as error:
        parser.exit(1, f"error: {error}\n")
    print(f"rows={summary['rows']} complete={summary['complete']} "
          f"incomplete={summary['incomplete']}")
    for dimension in ("delivery", "producer"):
        print(f"{dimension}: complete={summary[dimension]['complete']} "
              f"incomplete={summary[dimension]['incomplete']}")
    return 0


MODES = {
    'completeness': completeness_main,
    'weakening': weakening_main,
}

if __name__ == '__main__':
    raise SystemExit(main())
