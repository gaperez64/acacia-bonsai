#!/usr/bin/env python3
"""Is the GR(1) winning region schema-expressible, and does it even need to be?

M4 has to produce an invariant at the target N.  Reproducing the exact winning
region `inv` is not required: any I with init subset I, I inductive under the
policy, and I strong enough to carry the ranks is an adequate certificate.  So
this asks two separate questions per seed:

  EQUAL     is inv itself equal to a conjunction of per-client (or per-pair)
            projections?  If yes, generalization is a local problem.
  ADEQUATE  if not, is the projection conjunction *still* an adequate invariant,
            i.e. does tlsfcertcheck VERIFY the same policy with inv replaced by
            it?  A yes here is what M4 actually needs, and it is strictly weaker.

Projections are taken over the client's own monitor variables plus the shared
(scalar and bus-wide) variables, since those are n-independent by construction.
"""
import argparse
import collections
import json
import os
import pathlib
import re
import subprocess
import sys

sys.path.insert(0, '/usr/local/lib64/python3.13/site-packages')
import buddy  # noqa: E402

ROOT = pathlib.Path('/home/gperez/GIT-repos/acacia-bonsai')
TT = ROOT / 'subprojects' / 'tlsf-tools'
CHECK = ROOT / 'build_scratch' / 'snap' / 'tlsfcertcheck'
CODES = {0: 'VERIFIED', 1: 'REFUTED', 2: 'ERROR', 3: 'UNKNOWN', 4: 'INVALID',
         5: 'INTERNAL', 6: 'CERT_FAILED'}


class Aag:
    def __init__(self, path):
        lines = pathlib.Path(path).read_text().strip().split('\n')
        h = lines[0].split()
        self.M, self.I, self.L, self.O, self.A = map(int, h[1:6])
        p = 1
        self.inputs = [int(x) for x in lines[p:p + self.I]]; p += self.I
        self.latches = lines[p:p + self.L]; p += self.L
        self.outputs = [int(x) for x in lines[p:p + self.O]]; p += self.O
        self.gates = [tuple(map(int, g.split()))
                      for g in lines[p:p + self.A]]; p += self.A
        self.symbols = lines[p:]

    def out_index(self, name):
        for line in self.symbols:
            if line.startswith('o'):
                idx, _, nm = line[1:].partition(' ')
                if nm == name:
                    return int(idx)
        raise KeyError(name)

    def write(self, path, extra_gates, new_outputs):
        gates = self.gates + extra_gates
        M = max([self.M] + [g[0] // 2 for g in gates])
        body = [f'aag {M} {self.I} {self.L} {self.O} {len(gates)}']
        body += [str(x) for x in self.inputs] + list(self.latches)
        body += [str(x) for x in new_outputs]
        body += [f'{a} {b} {c}' for a, b, c in gates]
        body += self.symbols
        pathlib.Path(path).write_text('\n'.join(body) + '\n')


def aig_to_bdd(aag, lit, memo):
    if lit == 0:
        return buddy.bddfalse
    if lit == 1:
        return buddy.bddtrue
    if lit in memo:
        return memo[lit]
    if lit & 1:
        r = buddy.bdd_not(aig_to_bdd(aag, lit ^ 1, memo))
    else:
        var = lit // 2
        if var in aag._invar:
            r = buddy.bdd_ithvar(aag._invar[var])
        else:
            a, b = aag._gate[var]
            r = aig_to_bdd(aag, a, memo) & aig_to_bdd(aag, b, memo)
    memo[lit] = r
    return r


def bdd_to_aig(node, aag, emitted, memo, next_lit):
    """Shannon-expand a BDD into fresh AND gates appended to aag."""
    key = node.id() if hasattr(node, 'id') else node
    if node == buddy.bddtrue:
        return 1
    if node == buddy.bddfalse:
        return 0
    k = str(node)
    if k in memo:
        return memo[k]
    v = buddy.bdd_var(node)
    hi = bdd_to_aig(buddy.bdd_high(node), aag, emitted, memo, next_lit)
    lo = bdd_to_aig(buddy.bdd_low(node), aag, emitted, memo, next_lit)
    vlit = aag.inputs[aag._varin[v]]

    def gate(a, b):
        next_lit[0] += 2
        emitted.append((next_lit[0], a, b))
        return next_lit[0]

    # ite(v, hi, lo) = !(!(v & hi) & !(!v & lo))
    t1 = gate(vlit, hi)
    t2 = gate(vlit ^ 1, lo)
    r = gate(t1 ^ 1, t2 ^ 1) ^ 1
    memo[k] = r
    return r


def cube(vars_):
    c = buddy.bddtrue
    for v in vars_:
        c &= buddy.bdd_ithvar(v)
    return c


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--work', default=os.environ.get('GR1_WORK', 'build_scratch/gr1-seeds'))
    ap.add_argument('--align', default=str(ROOT / 'build_scratch' / 'm4align'))
    ap.add_argument('seeds', nargs='*',
                    default=['round_robin_arbiter_3', 'round_robin_arbiter_4',
                             'arbiter_3', 'arbiter_4', 'prioritized_arbiter_3'])
    args = ap.parse_args()
    work = pathlib.Path(args.work)
    align = pathlib.Path(args.align)

    buddy.bdd_init(4_000_000, 400_000)
    print(f'{"seed":26s} {"vars":>5s} {"clients":>7s} {"equal_1":>8s} '
          f'{"equal_2":>8s} {"ratio_1":>9s} {"adequate_1":>11s} {"adequate_2":>11s}')
    for seed in args.seeds:
        cert = work / f'{seed}.cert.aag'
        side = work / f'{seed}.cert.aag.json'
        game = work / f'{seed}.aag'
        pol = work / f'{seed}.policy.aag'
        if not (cert.exists() and side.exists()):
            print(f'{seed:26s} missing certificate'); continue
        meta = json.loads(side.read_text())
        m = re.match(r'(.+)_(\d+)$', seed)
        prov_path = align / f'{m.group(1)}_{m.group(2)}.prov.json'
        if not prov_path.exists():
            prov_path = work / f'{seed}.prov.json'
        prov = json.loads(prov_path.read_text())

        aag = Aag(cert)
        aag._gate = {g[0] // 2: (g[1], g[2]) for g in aag.gates}
        aag._invar = {lit // 2: k for k, lit in enumerate(aag.inputs)}
        aag._varin = {k: k for k in range(len(aag.inputs))}
        buddy.bdd_setvarnum(max(64, len(aag.inputs) + 4))

        # certificate input k <-> BDD variable k; group by owning monitor index
        mon_index = {mo['monitor']: tuple(mo['index_tuple'])
                     for mo in prov['monitors']}
        mon_local = {mo['monitor']: mo['arity_kind'] == 'local'
                     for mo in prov['monitors']}
        groups = collections.defaultdict(set)
        shared = set()
        state_vars = []
        for var in meta['variables']['state']:
            k = var['certificate_input']
            state_vars.append(k)
            mo = re.match(r'monitor_(\d+)_state_\d+', var['name'])
            if not mo:
                shared.add(k); continue
            mid = int(mo.group(1))
            idx = mon_index.get(mid, ())
            if mon_local.get(mid) and len(idx) == 1:
                groups[idx[0]].add(k)
            else:
                shared.add(k)
        allv = set(state_vars)
        nclients = len(groups)
        if nclients < 2:
            print(f'{seed:26s} {len(allv):5d} {nclients:7d}  too few clients')
            continue

        memo = {}
        inv = aig_to_bdd(aag, aag.outputs[aag.out_index('inv')], memo)

        def project_to(keep):
            drop = allv - keep
            return buddy.bdd_exist(inv, cube(drop)) if drop else inv

        p1 = buddy.bddtrue
        for i in sorted(groups):
            p1 &= project_to(groups[i] | shared)
        p2 = buddy.bddtrue
        cl = sorted(groups)
        for a in range(len(cl)):
            for b in range(a + 1, len(cl)):
                p2 &= project_to(groups[cl[a]] | groups[cl[b]] | shared)

        eq1, eq2 = (p1 == inv), (p2 == inv)
        try:
            n_inv = buddy.bdd_satcount(inv)
            n_p1 = buddy.bdd_satcount(p1)
            ratio = f'{n_p1 / n_inv:.3f}' if n_inv else 'n/a'
        except Exception:
            ratio = 'n/a'

        adequacy = {}
        for label, cand in (('1', p1), ('2', p2)):
            if cand == inv:
                adequacy[label] = 'same'
                continue
            emitted, m2, nxt = [], {}, [aag.M * 2]
            lit = bdd_to_aig(cand, aag, emitted, m2, nxt)
            outs = list(aag.outputs)
            outs[aag.out_index('inv')] = lit
            dst = work / f'{seed}.inv{label}.aag'
            aag.write(dst, emitted, outs)
            (work / f'{seed}.inv{label}.aag.json').write_text(side.read_text())
            r = subprocess.run(
                [str(CHECK), '--method', 'certificate', '--certificate',
                 str(dst), '--certificate-json', str(dst) + '.json',
                 '--timeout', '120', str(game), str(pol)],
                text=True, capture_output=True, check=False)
            adequacy[label] = CODES.get(r.returncode, str(r.returncode))

        print(f'{seed:26s} {len(allv):5d} {nclients:7d} {str(eq1):>8s} '
              f'{str(eq2):>8s} {ratio:>9s} {adequacy["1"]:>11s} '
              f'{adequacy["2"]:>11s}', flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
