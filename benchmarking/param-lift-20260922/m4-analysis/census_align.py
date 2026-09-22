#!/usr/bin/env python3
"""M4 prioritization census: how index-alignable is each DBA-reducible family?

For M4's anti-unification to be well posed, a predicate learned about index i at a
small n must be re-instantiable at index i at the target N.  Three things decide
whether that is possible, and they are what this measures per family:

  role_classes   distinct per-index template multisets.  1 = every index alike
                 (pure replication); a small constant = a few special roles
                 (e.g. a highest-priority client); growing with n = no alignment.
  local_frac     share of monitors that are local (single index).  Bus-wide
                 monitors must be generalized through a quantifier schema
                 instead, so a low share means more of the work is in the hard part.
  stable_from    the smallest n from which the per-index template set stops
                 changing.  Seeds below this n are in a degenerate regime and
                 must not be used for generalization.

Emits families/m4-alignment.tsv.
"""
import collections
import csv
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path('/home/gperez/GIT-repos/acacia-bonsai')
TT = ROOT / 'subprojects' / 'tlsf-tools'
MON = TT / 'scripts' / 'gr1_monitor_game.py'
WORK = ROOT / 'build_scratch' / 'm4align'
WORK.mkdir(exist_ok=True)
CENSUS = ROOT / 'benchmarking' / 'param-lift-20260922' / 'm0-census.tsv'
INSTANCES = ROOT / 'benchmarking' / 'param-lift-20260922' / 'm0-instances.tsv'


def load_instances():
    by_family = collections.defaultdict(list)
    with open(INSTANCES) as fh:
        for row in csv.DictReader(fh, delimiter='\t'):
            params = json.loads(row['parameters'])
            if len(params) != 1:
                continue
            (n,) = params.values()
            by_family[row['family_display']].append((n, row['tlsf']))
    return by_family


def provenance(fam, n, tlsf):
    out = WORK / f'{fam}_{n}.prov.json'
    if out.exists():
        try:
            return json.loads(out.read_text())
        except json.JSONDecodeError:
            pass
    cmd = ['systemd-run', '--user', '--scope', '--quiet',
           '-p', 'MemoryMax=2G', '-p', 'MemorySwapMax=0',
           '-E', 'PYTHONPATH=/usr/local/lib64/python3.13/site-packages',
           'python3.13', str(MON), str(ROOT / tlsf), '--semantics', 'exact',
           '--output', str(WORK / f'{fam}_{n}.aag'),
           '--provenance-out', str(out)]
    try:
        r = subprocess.run(cmd, text=True, capture_output=True, check=False,
                           cwd=TT, timeout=300)
    except subprocess.TimeoutExpired:
        err = {'error': 'timeout after 300s'}
        out.write_text(json.dumps(err))
        return err
    if r.returncode != 0:
        detail = (r.stderr.strip() or r.stdout.strip())[-200:]
        kind = 'oom/killed' if r.returncode in (137, 9, -9) else 'failed'
        err = {'error': f'{kind} rc={r.returncode} {detail}'}
        out.write_text(json.dumps(err))
        return err
    try:
        return json.loads(out.read_text())
    except (OSError, json.JSONDecodeError) as exc:
        return {'error': f'unreadable provenance: {exc}'}


def shape(p):
    mons = p['monitors']
    local = [m for m in mons if m['arity_kind'] == 'local']
    wide = [m for m in mons if m['arity_kind'] != 'local']
    per_index = collections.defaultdict(collections.Counter)
    for m in local:
        per_index[tuple(m['index_tuple'])][m['template']] += 1
    classes = {frozenset(c.items()) for c in per_index.values()}
    return {
        'monitors': len(mons),
        'latches': sum(len(m['latch_literals']) for m in mons),
        'local': len(local),
        'wide': len(wide),
        'role_classes': len(classes),
        'indices': len(per_index),
        'local_templates': frozenset(
            frozenset(c.items()) for c in per_index.values()),
        'wide_templates': tuple(sorted(
            re.sub(r'_\d+', '_i', m['template']) for m in wide)),
    }


def main():
    reducible = []
    with open(CENSUS) as fh:
        for row in csv.DictReader(fh, delimiter='\t'):
            if row['dba_reducible'] == 'true':
                reducible.append((row['family'], int(row['unsolved_120s'])))
    by_family = load_instances()

    cols = ['family', 'unsolved_120s', 'ns_probed', 'monitors_by_n',
            'latches_by_n', 'local_frac', 'role_classes', 'role_classes_grow',
            'stable_from', 'wide_frac', 'alignment', 'note']
    out_rows = []
    for fam, unsolved in sorted(reducible, key=lambda t: -t[1]):
        avail = sorted(by_family.get(fam, []))
        probe = [(n, f) for n, f in avail if isinstance(n, int)][:5]
        shapes, errors = {}, {}
        for n, tlsf in probe:
            p = provenance(fam, n, tlsf)
            if 'error' in p:
                errors[n] = p['error']
                continue
            shapes[n] = shape(p)
        if len(shapes) < 2:
            why = '; '.join(sorted({errors.get(n, '') for n in errors}))[:120]
            out_rows.append({'family': fam, 'unsolved_120s': unsolved,
                             'ns_probed': len(shapes), 'alignment': 'no-data',
                             'note': why})
            print(f'{fam:32s} no-data  {why}', flush=True)
            continue
        all_ns = sorted(shapes)
        lt = {n: shapes[n]['local_templates'] for n in all_ns}
        # The smallest n from which the per-index template set stops changing.
        # Everything below it is a degenerate regime (at n=1 a replicated family
        # has no local monitors at all, and rru2 only grows its second index
        # role at n=3), so seeds taken there describe a different game and must
        # not drive generalization -- nor should they score the family.
        stable_from = all_ns[0]
        for i in range(len(all_ns) - 1, 0, -1):
            if lt[all_ns[i]] != lt[all_ns[i - 1]]:
                stable_from = all_ns[i]
                break
        ns = [n for n in all_ns if n >= stable_from]
        rc = [shapes[n]['role_classes'] for n in ns]
        lf = sum(shapes[n]['local'] for n in ns) / max(
            1, sum(shapes[n]['monitors'] for n in ns))
        grow = rc[-1] > rc[0]
        if len(ns) < 2:
            verdict = 'unstable'
        elif grow or lf < 0.34 or rc[-1] == 0:
            verdict = 'weak'
        elif rc[-1] == 1:
            verdict = 'replicated'
        elif rc[-1] <= 4:
            verdict = 'roles'
        else:
            verdict = 'weak'
        out_rows.append({
            'family': fam, 'unsolved_120s': unsolved, 'ns_probed': len(ns),
            'monitors_by_n': ','.join(
                f'{n}:{shapes[n]["monitors"]}' for n in all_ns),
            'latches_by_n': ','.join(
                f'{n}:{shapes[n]["latches"]}' for n in all_ns),
            'local_frac': f'{lf:.2f}',
            'role_classes': ','.join(map(str, rc)),
            'role_classes_grow': str(grow).lower(),
            'stable_from': stable_from,
            'wide_frac': f'{1 - lf:.2f}',
            'alignment': verdict,
            'note': '; '.join(f'n={n}: {e}' for n, e in errors.items())[:160],
        })
        print(f'{fam:32s} {verdict:11s} roles={rc} local={lf:.2f} '
              f'stable_from={stable_from} mons={out_rows[-1]["monitors_by_n"]}',
              flush=True)

    dest = ROOT / 'benchmarking' / 'param-lift-20260922' / 'm4-alignment.tsv'
    with open(dest, 'w', newline='') as fh:
        w = csv.DictWriter(fh, fieldnames=cols, delimiter='\t',
                           lineterminator='\n', restval='')
        w.writeheader()
        w.writerows(out_rows)
    tally = collections.Counter(r['alignment'] for r in out_rows)
    print(f'\n{dict(tally)}  ->  {dest}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
