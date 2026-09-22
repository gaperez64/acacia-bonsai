#!/usr/bin/env python3
"""Alignment census for the two-parameter families, one axis at a time.

The single-parameter census skips these.  For each family, hold one parameter
fixed and vary the other, so the same question ("does the per-index template set
stop changing, and does the role count stay constant") can be asked per axis.
Which axis is index-bearing is exactly what this reveals.
"""
import collections, csv, json, pathlib, subprocess, sys
ROOT = pathlib.Path('/home/gperez/GIT-repos/acacia-bonsai')
TT = ROOT/'subprojects'/'tlsf-tools'
MON = TT/'scripts'/'gr1_monitor_game.py'
WORK = ROOT/'build_scratch'/'m4align2'; WORK.mkdir(exist_ok=True)

def prov(tag, tlsf):
    out = WORK/f'{tag}.prov.json'
    if out.exists():
        try: return json.loads(out.read_text())
        except json.JSONDecodeError: pass
    cmd = ['systemd-run','--user','--scope','--quiet','-p','MemoryMax=2G',
           '-p','MemorySwapMax=0','-E',
           'PYTHONPATH=/usr/local/lib64/python3.13/site-packages','python3.13',
           str(MON), str(ROOT/tlsf), '--semantics','exact',
           '--output', str(WORK/f'{tag}.aag'), '--provenance-out', str(out)]
    try: r = subprocess.run(cmd, capture_output=True, text=True, cwd=TT, timeout=300)
    except subprocess.TimeoutExpired:
        out.write_text('{"error":"timeout"}'); return {'error':'timeout'}
    if r.returncode != 0:
        e = {'error': f'rc={r.returncode} ' + (r.stderr.strip() or '')[-120:]}
        out.write_text(json.dumps(e)); return e
    return json.loads(out.read_text())

def shape(p):
    loc = [m for m in p['monitors'] if m['arity_kind']=='local']
    per = collections.defaultdict(collections.Counter)
    for m in loc:
        if len(m['index_tuple'])==1: per[m['index_tuple'][0]][m['template']]+=1
    return (len(p['monitors']), len(loc),
            len({frozenset(c.items()) for c in per.values()}),
            frozenset(frozenset(c.items()) for c in per.values()))

by = collections.defaultdict(list)
for r in csv.DictReader(open(ROOT/'benchmarking/param-lift-20260922/m0-instances.tsv'),delimiter='\t'):
    p = json.loads(r['parameters'])
    if len(p)==2: by[r['family_display']].append((p, r['tlsf']))
rows=[]
for fam, inst in sorted(by.items()):
    keys = sorted(inst[0][0])
    for axis in keys:
        other = [k for k in keys if k!=axis][0]
        # hold `other` at its most common value, vary `axis`
        counts = collections.Counter(p[other] for p,_ in inst)
        fixed = counts.most_common(1)[0][0]
        sel = sorted(((p[axis], t) for p,t in inst if p[other]==fixed))[:4]
        if len(sel) < 2: continue
        shapes={}
        for v,t in sel:
            d = prov(f'{fam}_{axis}{v}_{other}{fixed}', t)
            if 'error' not in d: shapes[v]=shape(d)
        if len(shapes)<2:
            print(f'{fam:30s} {axis:3s} (fix {other}={fixed})  no-data',flush=True)
            rows.append({'family':fam,'axis':axis,'fixed':f'{other}={fixed}',
                         'values':'','verdict':'no-data','detail':''}); continue
        vs=sorted(shapes)
        tmpl=[shapes[v][3] for v in vs]; rc=[shapes[v][2] for v in vs]
        stable = tmpl[-1]==tmpl[-2]
        lf = sum(shapes[v][1] for v in vs)/max(1,sum(shapes[v][0] for v in vs))
        if rc[-1]==0 or lf<0.34: verdict='weak'
        elif rc[-1]>rc[0]: verdict='growing'
        elif rc[-1]==1: verdict='replicated'
        else: verdict='roles'
        detail=f'mons={[shapes[v][0] for v in vs]} roles={rc} local={lf:.2f} tmpl_stable={stable}'
        print(f'{fam:30s} {axis:3s} (fix {other}={fixed}) {verdict:11s} {detail}',flush=True)
        rows.append({'family':fam,'axis':axis,'fixed':f'{other}={fixed}',
                     'values':','.join(map(str,vs)),'verdict':verdict,'detail':detail})
dest = ROOT/'benchmarking/param-lift-20260922/m4-alignment-2param.tsv'
with open(dest,'w',newline='') as fh:
    w=csv.DictWriter(fh,fieldnames=['family','axis','fixed','values','verdict','detail'],
                     delimiter='\t',lineterminator='\n'); w.writeheader(); w.writerows(rows)
print('->',dest)
