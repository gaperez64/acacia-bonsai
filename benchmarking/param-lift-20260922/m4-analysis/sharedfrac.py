#!/usr/bin/env python3
"""How much of the state does my separability metric refuse to project away?

ksep.py keeps every "shared" variable (scalar and bus-wide monitors) in every
projection.  When the shared set dominates, the projection removes almost
nothing and `inv == conjunction of projections` becomes close to vacuous -- so a
reported min_k=1 says much less than it appears to.
"""
import collections, json, os, pathlib, re, sys
W = pathlib.Path(os.environ['GR1_WORK'])
A = pathlib.Path('/home/gperez/GIT-repos/acacia-bonsai/build_scratch/m4align')
print(f'{"seed":28s} {"clients":>7s} {"local/client":>12s} {"shared":>7s} '
      f'{"total":>6s} {"shared%":>8s} {"dropped by a k=1 projection":>28s}')
for seed in sys.argv[1:]:
    cert = W / f'{seed}.cert.aag.json'
    m = re.match(r'(.+)_(\d+)$', seed)
    pp = A / f'{m.group(1)}_{m.group(2)}.prov.json'
    if not pp.exists(): pp = W / f'{seed}.prov.json'
    if not (cert.exists() and pp.exists()):
        print(f'{seed:28s} (missing)'); continue
    meta = json.loads(cert.read_text()); prov = json.loads(pp.read_text())
    mi = {x['monitor']: (tuple(x['index_tuple']), x['arity_kind']) for x in prov['monitors']}
    groups, shared = collections.defaultdict(int), 0
    for v in meta['variables']['state']:
        mo = re.match(r'monitor_(\d+)_state_\d+', v['name'])
        if not mo: shared += 1; continue
        idx, kind = mi.get(int(mo.group(1)), ((), 'bus'))
        if kind == 'local' and len(idx) == 1: groups[idx[0]] += 1
        else: shared += 1
    if not groups: print(f'{seed:28s} (no local)'); continue
    nc = len(groups); per = groups[min(groups)]
    total = sum(groups.values()) + shared
    dropped = total - (per + shared)
    print(f'{seed:28s} {nc:7d} {per:12d} {shared:7d} {total:6d} '
          f'{100*shared/total:7.0f}% {dropped:>24d} of {total}')
