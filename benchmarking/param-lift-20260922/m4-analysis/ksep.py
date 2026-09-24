#!/usr/bin/env python3
"""Separability arity of the GR(1) winning region.

The smallest k for which inv equals the conjunction of its projections onto
every k-subset of clients (plus the shared/bus variables).  That k is exactly
the template arity M4 must generalize: at k the invariant is a conjunction of
C(n,k) instances of one n-independent predicate, so learning it at a small seed
determines it at every N.  k > number of clients means no such schema exists
and the invariant is genuinely global.

Projections over-approximate, so the conjunction always contains inv; equality
is the whole question.  Also reports the over-approximation factor at each k.
"""
import collections, itertools, json, os, pathlib, re, sys
sys.path.insert(0, '/usr/local/lib64/python3.13/site-packages')
import buddy

ROOT = pathlib.Path('/home/gperez/GIT-repos/acacia-bonsai')
WORK = pathlib.Path(os.environ.get('GR1_WORK', 'build_scratch/gr1-seeds'))
ALIGN = ROOT / 'build_scratch' / 'm4align'
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from invschema import Aag, aig_to_bdd, cube

def analyse(seed, which='inv'):
    cert, side = WORK/f'{seed}.cert.aag', WORK/f'{seed}.cert.aag.json'
    if not cert.exists(): return None
    meta = json.loads(side.read_text())
    m = re.match(r'(.+)_(\d+)$', seed)
    pp = ALIGN/f'{m.group(1)}_{m.group(2)}.prov.json'
    if not pp.exists(): pp = WORK/f'{seed}.prov.json'
    prov = json.loads(pp.read_text())
    aag = Aag(cert)
    aag._gate = {g[0]//2:(g[1],g[2]) for g in aag.gates}
    aag._invar = {l//2:k for k,l in enumerate(aag.inputs)}
    aag._varin = {k:k for k in range(len(aag.inputs))}
    assert len(aag.inputs) <= 256
    mi = {mo['monitor']: (tuple(mo['index_tuple']), mo['arity_kind']) for mo in prov['monitors']}
    groups, shared, allv = collections.defaultdict(set), set(), set()
    for v in meta['variables']['state']:
        k = v['certificate_input']; allv.add(k)
        mo = re.match(r'monitor_(\d+)_state_\d+', v['name'])
        if not mo: shared.add(k); continue
        idx, kind = mi.get(int(mo.group(1)), ((), 'bus'))
        (groups[idx[0]].add(k) if kind=='local' and len(idx)==1 else shared.add(k))
    cl = sorted(groups)
    if len(cl) < 2: return None
    try:
        f = aig_to_bdd(aag, aag.outputs[aag.out_index(which)], {})
    except KeyError:
        return None
    statecube = cube(sorted(allv))
    base = buddy.bdd_satcountset(f, statecube)
    res = []
    for k in range(1, len(cl)+1):
        acc = buddy.bddtrue
        for sub in itertools.combinations(cl, k):
            keep = shared.union(*(groups[i] for i in sub))
            drop = allv - keep
            acc &= buddy.bdd_exist(f, cube(drop)) if drop else f
        cnt = buddy.bdd_satcountset(acc, statecube)
        res.append((k, acc == f, cnt/base if base else float('nan')))
        if acc == f: break
    return len(cl), res

buddy.bdd_init(6_000_000, 600_000)
buddy.bdd_setvarnum(256)
WHICH = os.environ.get('KSEP_WHICH','inv') if (os := __import__('os')) else 'inv'
seeds = sys.argv[1:] or ['arbiter_3','arbiter_4','round_robin_arbiter_3',
    'round_robin_arbiter_4','round_robin_arbiter_5','prioritized_arbiter_3',
    'load_balancer_2','lift_2']
import csv, os, re as _re
rows = []
print(f'{"seed":26s} {"clients":>7s}  min-k  per-k (k, equal, overapprox)')
for s in seeds:
    r = analyse(s, WHICH)
    if not r: print(f'{s:26s}  (skipped)'); continue
    nc, res = r
    mink = next((k for k,eq,_ in res if eq), None)
    cells = ' '.join(f'{k}:{"=" if eq else "x"}{ratio:.4f}' for k,eq,ratio in res)
    print(f'{s:26s} {nc:7d}  {str(mink):5s}  {cells}')
    mm = _re.match(r'(.+)_(\d+)$', s)
    rows.append({'family': mm.group(1), 'n': int(mm.group(2)), 'clients': nc,
                 'min_k': mink if mink is not None else '',
                 'min_k_is_n': str(mink == nc).lower(),
                 'overapprox_k1': f'{res[0][2]:.4f}',
                 'per_k': cells})
dest = os.environ.get('KSEP_TSV')
if dest:
    with open(dest, 'w', newline='') as fh:
        w = csv.DictWriter(fh, fieldnames=['family','n','clients','min_k',
            'min_k_is_n','overapprox_k1','per_k'], delimiter='\t',
            lineterminator='\n')
        w.writeheader(); w.writerows(rows)
    print(f'-> {dest}')
