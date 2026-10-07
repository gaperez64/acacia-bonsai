# Native structure-guard affordability experiment

`--native-structure-guard-scale F` is an explicit research option for native
GR(1) and lifting arms. The default is `1`. A finite positive factor scales the
six structural prechecks below; `off` disables those six prechecks. It applies
to the original target and every seed reduction, including strict retries.
The setting has no effect on legacy arms. No shipping preset changes.

| Structural counter | Global baseline |
| --- | ---: |
| Sum of conjunct syntax-tree nodes | 4,096 |
| Distinct APs in the lowered formula | 2,048 |
| Assumption plus guarantee conjunct count | 1,024 |
| Largest conjunct syntax-tree node count | 1,024 |
| Maximum temporal operator nesting depth | 16 |
| Sum of estimated monitor states | 16,777,216 |

Conjunct extraction flattens conjunctions, distributes `G` over a conjunction,
and omits `true`. Nodes count syntax-tree visits, including repeated subtrees,
rather than distinct interned DAG nodes. Temporal depth counts `X/F/G/U/R/W/M`.
The monitor estimate sums `nodes * 2^min(depth, 20)` per conjunct, with saturating
arithmetic. It is a heuristic and does not measure constructed monitor states.
The outer implication and removed conjunction connectors do not contribute to
the sum of conjunct nodes. The guard stops when any measured counter is strictly
greater than its effective threshold; equality passes.

The effective threshold is `floor(baseline * F)`, clamped to at least 1 and
saturated at `UINT64_MAX`. An already disabled baseline stays disabled. `off`
also retains checked size arithmetic. Factors are global and selected explicitly
by the researcher; neither source identity nor expected polarity selects them.

Actual monitor-state/edge limits, artifact bytes, RSS shares, the 8 GiB address
space cap, deadlines, cancellation, all BDD node/cache limits, and proof checks
retain their incumbent settings. In particular, turning guards off does not
remove the monitor limit of 10,000 states or the construction edge limit of
150,000. A standalone arm has a different incumbent RSS share from a five-arm
race, because the share remains invocation memory divided by arm count and 2.

The tlsf-tools extension uses `TlsfGr1StructureGuardOptionsV1` and versioned
`*_v2` entry points for reduction, target preparation, R lifting, and combined
solving. The old structs and entry points are unchanged. A null research options
pointer selects factor 1; an explicit zero means off. Base budgets remain live
and unmodified. Pass the same research setting to preparation and solving.

Every native lifecycle attribution event carries `native_structure_guard_scale`
as a number, or the string `"off"`. Parent records retain it when a child is
killed. Capture with the existing `ACACIA_PHASE_RECORDS` transport or the runner's
`--phase-records-dir`; its existing nonblocking delivery and completeness rules
still apply. Solver behavior does not depend on diagnostics.

The P2b census and proposed standalone screens live in the worktree's ignored
`_bm-logs.p2b-guard/REPORT.md` and `COMMANDS.md`. Screens are diagnostic, not
portfolio admission. Changing guard eligibility invalidates recycling from the
incumbent or another factor; use fresh 17 s and 60 s rows for each setting.
