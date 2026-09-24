#!/usr/bin/env python3
"""Is a min_k = n winning region an n-ary disjunction of local predicates?

`AtMostOne` over a bus is a conjunction of pairwise constraints, so it would
have shown up as min_k = 2.  min_k = n is instead the signature of an n-ary
DISJUNCTION, which no fixed-arity conjunctive template can express but a
quantifier schema `exists j. q(j)` can.

Test: let P = conjunction of the k-subset projections at the largest k < n, so
P is a strict superset of inv.  If R = P and not inv is itself a conjunction of
per-client predicates, R = AND_i b_i, then

    inv = P and not(AND_i b_i) = P and (OR_i not b_i)

i.e. exactly a pairwise-conjunctive part plus one n-ary disjunction of local
predicates -- generalizable after all, with a quantifier hole.
"""
import collections, itertools, json, os, pathlib, re, sys
sys.path.insert(0, '/usr/local/lib64/python3.13/site-packages')
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import buddy
from invschema import Aag, aig_to_bdd, cube

ROOT = pathlib.Path('/home/gperez/GIT-repos/acacia-bonsai')
WORK = pathlib.Path(os.environ['GR1_WORK'])
ALIGN = ROOT/'build_scratch'/'m4align'

def load(seed, which='inv'):
    cert, side = WORK/f'{seed}.cert.aag', WORK/f'{seed}.cert.aag.json'
    meta = json.loads(side.read_text())
    m = re.match(r'(.+)_(\d+)$', seed)
    prov = json.loads((ALIGN/f'{m.group(1)}_{m.group(2)}.prov.json').read_text())
    aag = Aag(cert)
    aag._gate = {g[0]//2:(g[1],g[2]) for g in aag.gates}
    aag._invar = {l//2:k for k,l in enumerate(aag.inputs)}
    aag._varin = {k:k for k in range(len(aag.inputs))}
    mi = {mo['monitor']:(tuple(mo['index_tuple']), mo['arity_kind']) for mo in prov['monitors']}
    groups, shared, allv = collections.defaultdict(set), set(), set()
    for v in meta['variables']['state']:
        k = v['certificate_input']; allv.add(k)
        mo = re.match(r'monitor_(\d+)_state_\d+', v['name'])
        if not mo: shared.add(k); continue
        idx, kind = mi.get(int(mo.group(1)), ((), 'bus'))
        (groups[idx[0]].add(k) if kind=='local' and len(idx)==1 else shared.add(k))
    f = aig_to_bdd(aag, aag.outputs[aag.out_index(which)], {})
    return f, groups, shared, allv

buddy.bdd_init(6_000_000, 600_000); buddy.bdd_setvarnum(256)
print(f'{"seed":26s} {"k":>2s}  {"R=P&!inv nonempty":>18s}  {"R per-client sep":>17s}  {"inv == P & OR_i !b_i":>21s}')
for seed in sys.argv[1:]:
    try:
        inv, groups, shared, allv = load(seed)
    except (FileNotFoundError, KeyError) as e:
        print(f'{seed:26s} skipped ({e})'); continue
    cl = sorted(groups)
    k = len(cl)-1
    if k < 1: print(f'{seed:26s} too few clients'); continue
    P = buddy.bddtrue
    for sub in itertools.combinations(cl, k):
        keep = shared.union(*(groups[i] for i in sub))
        drop = allv - keep
        P &= buddy.bdd_exist(inv, cube(drop)) if drop else inv
    R = P & buddy.bdd_not(inv)
    nonempty = R != buddy.bddfalse
    # is R itself a conjunction of per-client projections?
    Rsep = buddy.bddtrue
    for i in cl:
        keep = groups[i] | shared
        drop = allv - keep
        Rsep &= buddy.bdd_exist(R, cube(drop)) if drop else R
    sep = (Rsep == R)
    recon = P & buddy.bdd_not(Rsep)
    ok = (recon == inv)
    print(f'{seed:26s} {k:2d}  {str(nonempty):>18s}  {str(sep):>17s}  {str(ok):>21s}')
