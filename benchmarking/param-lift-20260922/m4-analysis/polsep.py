#!/usr/bin/env python3
"""Support locality of the exported GR(1) policy guards.

For each controllable output `controllable_X_i`, take the BDD of its guard and
ask which clients its support touches.  A guard whose support is confined to its
own client plus the shared (scalar/bus/counter/input) variables is generalizable
by re-instantiation alone; one that reads every other client is not.

Reported per seed: the share of guards that are own-client-local, and the mean
number of foreign clients read.  The counter outputs `curr_next_j` are reported
separately because the goal counter is global by construction.
"""
import collections, json, os, pathlib, re, sys
sys.path.insert(0, '/usr/local/lib64/python3.13/site-packages')
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import buddy
from invschema import Aag, aig_to_bdd

ROOT = pathlib.Path('/home/gperez/GIT-repos/acacia-bonsai')
WORK = pathlib.Path(os.environ.get('GR1_WORK', 'build_scratch/gr1-seeds'))
ALIGN = ROOT/'build_scratch'/'m4align'

def support_vars(f):
    s = buddy.bdd_support(f); out = []
    while s != buddy.bddtrue and s != buddy.bddfalse:
        out.append(buddy.bdd_var(s)); s = buddy.bdd_high(s)
    return set(out)

def run(seed):
    pol, side = WORK/f'{seed}.policy.aag', WORK/f'{seed}.policy.aag.json'
    if not pol.exists(): return None
    meta = json.loads(side.read_text())
    m = re.match(r'(.+)_(\d+)$', seed)
    pp = ALIGN/f'{m.group(1)}_{m.group(2)}.prov.json'
    if not pp.exists(): return None
    prov = json.loads(pp.read_text())
    aag = Aag(pol)
    aag._gate = {g[0]//2:(g[1],g[2]) for g in aag.gates}
    aag._invar = {l//2:k for k,l in enumerate(aag.inputs)}
    aag._varin = {k:k for k in range(len(aag.inputs))}
    mi = {mo['monitor']: (tuple(mo['index_tuple']), mo['arity_kind']) for mo in prov['monitors']}
    owner = {}
    for v in meta['inputs']['state']:
        mo = re.match(r'monitor_(\d+)_state_\d+', v['name'])
        if not mo: continue
        idx, kind = mi.get(int(mo.group(1)), ((), 'bus'))
        if kind == 'local' and len(idx) == 1: owner[v['policy_input']] = idx[0]
    memo = {}
    res, cres = [], []
    for o in meta['outputs']['controllable']:
        f = aig_to_bdd(aag, aag.outputs[o['policy_output']], memo)
        sup = support_vars(f)
        touched = {owner[v] for v in sup if v in owner}
        mo = re.search(r'_(\d+)$', o['name'])
        own = int(mo.group(1)) if mo else None
        res.append((o['name'], own, sorted(touched)))
    for o in meta['outputs'].get('counter_next', []):
        f = aig_to_bdd(aag, aag.outputs[o['policy_output']], memo)
        cres.append((o['name'], sorted({owner[v] for v in support_vars(f) if v in owner})))
    return res, cres

buddy.bdd_init(6_000_000, 600_000); buddy.bdd_setvarnum(256)
seeds = sys.argv[1:]
print(f'{"seed":26s} {"guards":>6s} {"own-local":>9s} {"mean foreign":>12s}  counter reads')
for s in seeds:
    r = run(s)
    if not r: print(f'{s:26s} (skipped)'); continue
    guards, counters = r
    loc = sum(1 for _,own,t in guards if set(t) <= {own})
    mf = sum(len(set(t)-{own}) for _,own,t in guards)/max(1,len(guards))
    cr = max((len(t) for _,t in counters), default=0)
    print(f'{s:26s} {len(guards):6d} {f"{loc}/{len(guards)}":>9s} {mf:12.2f}  max {cr} clients')
