# M6 cold campaign

The exact per-instance table is [`m6-campaign.tsv`](m6-campaign.tsv).  It contains only rows whose
`status_120s` is `unsolved` in `m0-instances.tsv`: 42 REAL targets from the ten measured-arity
families and 13 UNREAL targets from the four named M5 families.  No previously-solved target is
counted.

## The two closure counts

- **32 previously-unsolved instances are now known closed**: 28 REAL certificates from M4b plus
  four exact environment certificates from M6.  Their cold proof-run cost is **3,792.434 s** when
  the fresh M6 time is used for its 26 decisive rows and the retained M4b cold time is used for the
  six over-budget REAL rows that M6 necessarily times out.
- **26 of those 32 close inside the defining 120 s budget** in the fresh M6 wrapper: **22 REAL +
  4 UNREAL**, with **396.227 s** total cold cost on the decisive rows.

The whole 55-row M6 sweep, including every failed stage, costs **2,704.605 s**.  This is a cold
number: seed monitor construction and seed solves are charged on every lifted invocation.  There is
no warm column or warm-cache result.

The six known closures outside the M6 budget remain separate: `amba_decomposed_lock` n=16,
`arbiter_on_inpchange` n=7, `arbiter_with_buffer` n=9/10, and `load_balancer` n=10/11.  Their M4b
cold times range from 135.138 s to 1,860.104 s.  They are part of the first count and not the
second.

## Fresh M6 family results

| side | family | closed / targets | all-row cold cost (s) | failed target stage(s) |
|---|---|---:|---:|---|
| REAL | `abcg_arbiter` | 0 / 3 | 10.809 | anti-unify n=4,5,6 |
| REAL | `amba_decomposed_lock` | 1 / 3 | 294.552 | target check n=16; canonicalize n=30 |
| REAL | `arbiter` | 5 / 5 | 20.367 | — |
| REAL | `arbiter_on_inpchange` | 2 / 3 | 184.174 | target check n=7 |
| REAL | `arbiter_with_buffer` | 3 / 5 | 265.343 | instantiate n=9,10 |
| REAL | `arbiter_with_cancel` | 4 / 5 | 175.373 | target check n=10 |
| REAL | `collector_v1` | 0 / 5 | 290.355 | canonicalize n=11–15 |
| REAL | `load_balancer` | 2 / 4 | 326.151 | ranks n=10,11 |
| REAL | `prioritized_arbiter` | 5 / 5 | 6.650 | — |
| REAL | `simple_arbiter_with_hints` | 0 / 4 | 3.374 | anti-unify n=8,10,12,15 |
| UNREAL | `amba_case_study_unreal` | 0 / 4 | 479.449 | target check n=2; target solve n=3–5 |
| UNREAL | `load_balancer_unreal2` | 1 / 3 | 241.811 | target check n=7; target solve n=8 |
| UNREAL | `prioritized_arbiter_unreal2` | 0 / 3 | 359.240 | canonicalize n=30,60,100 |
| UNREAL | `round_robin_arbiter_unreal2` | 3 / 3 | 46.957 | — |

The `abcg_arbiter` and `simple_arbiter_with_hints` declines name the measured predicate that exceeds
the family-level arity (`x_4_2_0` and `inv`, respectively).  `collector_v1` n=13–15 reproducibly
hit the enclosing 8 GiB cgroup while constructing the target monitor game; systemd recorded an 8
GiB scope peak.  They remain UNKNOWN.  Every decisive UNREAL row has `side=environment`, exact
reduction semantics, a Moore counter-strategy, and a `tlsfcertcheck` VERIFIED result.

The UNREAL rows are deliberately labelled `direct-certified`, with measured arity and seeds marked
`not-measured`/`-`.  M5 made these certificates checkable; it did not establish a bounded-arity
environment generalizer, so they are coverage but not claimed as lifting.

## Coverage and PAR-2 on the exact target sets

The saved W1 B/S observations were subset to the exact IDs and revalidated through
`run-syntcomp26-coverage.py export-cactus`; `cactus-report.py` produced the reports and plots.

### REAL (42 rows)

| series | coverage | delta vs B/S | PAR-2 total (s) | PAR-2 mean (s) |
|---|---:|---:|---:|---:|
| M6 | 22 | +22 | 5,144.208 | 122.481 |
| ltlsynt | 18 | +18 | 5,845.514 | 139.179 |
| B | 0 | — | 10,080.000 | 240.000 |
| S | 0 | — | 10,080.000 | 240.000 |

M6 improves the B/S PAR-2 mean by 117.519 s and coverage by 22.  Against ltlsynt it has a net
coverage delta of +4 and a 16.698 s lower PAR-2 mean: eight M6-only closures and four ltlsynt-only
losses.  The mean difference exceeds the 12.1 s SYNTCOMP26 noise floor but is inside the 21.1 s
SYNTCOMP25 spread; the coverage and named loss set, rather than the PAR-2 change alone, carry the
claim.

The four REAL ltlsynt-only losses are `abcg_arbiter` n=4, `amba_decomposed_lock` n=16, and
`simple_arbiter_with_hints` n=8/10.

### UNREAL (13 rows)

| series | coverage | delta vs B/S | PAR-2 total (s) | PAR-2 mean (s) |
|---|---:|---:|---:|---:|
| M6 | 4 | +4 | 2,212.019 | 170.155 |
| ltlsynt | 4 | +4 | 2,169.482 | 166.883 |
| B | 0 | — | 3,120.000 | 240.000 |
| S | 0 | — | 3,120.000 | 240.000 |

M6 improves the B/S PAR-2 mean by 69.845 s and coverage by four.  Against ltlsynt the net coverage
delta is zero, but the sets are disjoint: M6 alone closes `load_balancer_unreal2` n=6 and
`round_robin_arbiter_unreal2` n=5/6/7; ltlsynt alone closes `amba_case_study_unreal` n=2 and
`prioritized_arbiter_unreal2` n=30/60/100.  The 3.272 s PAR-2-mean loss is inside both stated noise
floors and is not evidence on its own.

Exact generated reports: [`m6-comparison-real.md`](m6-comparison-real.md) and
[`m6-comparison-unreal.md`](m6-comparison-unreal.md).
