# Legacy route decisions

- Preserve solver paths, worker membership, proof semantics, limits and defaults.
  Add phase observations to the existing bounded nonblocking transport.
- Join physical PID, subjob, existing run identity, K and stage occurrence.
  Missing packets and killed stages remain partial/unknown/censored evidence.
- Publish complete sparse attempt counters before its completion milestone and
  snapshot independent rebuilt-row demand after verification.
- Select 28 inputs from explicit evidence with seed 207 and a structural/evidence
  sampler whose tie breaker is a source-token digest, excluding descriptive strings and comments. Preserve all observed rejection/last-stage strata, paired allowance gains,
  Acacia-only and near-cap common solves; disclose forced coverage beyond the four
  seeded both-unsolved samples. Names have no role in selection rules.
- Run no performance campaign in the implementation sandbox. P1/P2/P3 selection
  requires the external driver's phase panel and profiles; generated smoke timings
  are correctness observations only. No performance package is admitted here.

See [record schema and reader usage](../legacy-phase-records.md). Generated panel,
phase tables, build pins and validation logs stay under `_bm-logs.legacy-p0/`.

- Review follow-up: disabled observations perform no PID/clock/resource/exception samples;
  actioner and K counters cache the decision before loops. Threshold/preimage, push, decode,
  sparse search and replay counters remain guarded. Earlier dropped completions stay unknown;
  only the active occurrence is censored. Capture joins require complete invocation identities,
  conclusive status and matching stage completion; unentered verifier zeros stay absent.
- Regenerated seed-207 manifest: 28 rows cover 27 observed strata, with 10 both-unsolved,
  11 ltlsynt-only, 4 Acacia-only and 3 near-cap common solves. Parent/child chronology is
  merged and delivery uncertainty is preserved. Sources are read from the explicit source
  root, checked against metadata digests; filenames and paths cannot break ranking ties.

- Capture producer follow-up: `ACACIA_PHASE_INVOCATION` binds snapshots and history
  to the phase table's supplied invocation label through the existing capture path.
  The label is read only with capture enabled; no synchronous I/O is added. Actual
  release captures cover two decomposed sparse subjobs at K=2 and K=5, with strict
  identity, completion and stage-specific metric checks retained.

## 2026-10-08: explicit translation level, pending measurement

P0 selected one new isolated treatment: Small + BA/SBAcc at Low versus the
existing High translator. `--translation-level high|medium|low` is a runtime
option; omission and explicit `high` retain the constructor's High path without
rebuilding its simplifier. The treatment applies uniformly to the two REAL
workers and both UNREAL timing transformations, including their original
fallback, degenerate-alphabet and synthesis translations. Native workers do not
use this translator. Worker membership, preferences, defaults, proof replay and
resource limits are unchanged. Medium is supported by the same upstream API;
the external screen compares only High and Low, with no parameter grid.

The installed Spot 2.16 `translate.hh` and `postproc.hh` are byte-identical to
those in the locally retained upstream 2.16 release archive. Header SHA-256:
`6efcc5692b1c820869b319b9c5f374fe7bcd85bc42b0ffba9e14dc223d26cea1` and
`4fca78a05682360e9f1458ef6a13e95a690e4afcb61c58b0cff44e95b665316a`.
`translator::set_level` sets the inherited postprocessing level, deletes and
rebuilds an owned internal LTL simplifier with the same BDD dictionary, and,
unless explicitly overridden, sets the GF-guarantee shortcut to `level != Low`.
The existing dictionary constructor owns that simplifier. A caller-supplied
simplifier would not be rebuilt; that constructor is not used here.

The release `translate.cc` and `postproc.cc` confirm the effects for the retained
Small + BA/SBAcc configuration (the general header examples assume options we
explicitly disable):

| Setting / level-dependent behavior | High | Medium | Low |
| --- | --- | --- | --- |
| `simul`, `ba-simul`, `det-simul` | 0 | 0 | 0 |
| `tls-impl`, `wdba-minimize` | 1, 2 | 1, 2 | 1, 2 |
| Simplifier basic reductions, eventual/universal rules, syntactic implication | on | on | on |
| Simplifier containment and stronger containment | off | off | off |
| Default GF-guarantee shortcut | on | on | off |
| Default explicit propositions in FM translation | on | off | off |
| Remove unused APs when finalizing | yes | yes | no |
| Reject a larger WDBA under Small | no | yes | yes |
| Try the alternative pipeline even after successful WDBA minimization | yes | no | no |
| Default restriction of dead-end edges on nondeterministic non-WDBA results | on | off | off |
| Additional final SCC/acceptance filtering | yes | no | no |
| High cleanup mode of `ensure_ba` | yes | no | no |

`tls-impl=1` overrides the simplifier's level defaults, so lowering the level
here does **not** disable syntactic implication or newly disable containment.
`wdba-minimize=2` still attempts obligation minimization only when guaranteed to
work. The simulation switches stay zero; Low does not re-enable them. The
GF-guarantee setting is the upstream shortcut, distinct from the unrelated
`--spot-fast` option. `report_unused_options()` still rejects unknown scalar
names after every existing constructor.

The translation stage publishes `translation_level`, `translation_settings`,
`translation_type`, numeric `translation_preference` (9 for Small + SBAcc), and
`translation_gf_guarantee` through the existing nonblocking phase transport.
Spot captures publish the level, five scalars, type, state-acceptance setting,
GF-guarantee default and existing preference before translation starts. Thus a
translation killed by the external cap still has its requested effective
configuration. Disabled observations add no file I/O or graph traversal.

Correctness uses actual automaton complementation and product emptiness in both
inclusion directions for 64 seeded generated formulas, including GF objectives,
at High/Low and High/Medium. An independent explicit safety-game fixed point
checks all 16 Boolean input/output relations with current, delayed and
anticipated signals, under original Mealy and Moore quantifiers, against all
four physical legacy workers at High and Low (768 checks). Fresh independent
replay accepts translated winning certificates and rejects missing choices and
corrupted coverage at every level. Synthesis controllers are checked by the
independent AIG/monitor product. Ordinary default stdout/stderr/exit codes are
compared with a matched pre-change checked executable. Language equivalence is
not a claim of identical automata or minimal winning K.

Archived negatives remain closed: `benchmarking-root-legacy` records the direct
simulation G1 loss (10.72 s solve to 17.03 s timeout), Small+Any race rejection
(1,208 to 1,207 solves), transition acceptance rejection (125 to 124 solves,
PAR-2 2022.3 to 2046.2 s), and ineffective rank closure. The
`coverage-frontier-20260912` preference screen had 24/24 identical completed
automata; its lowering-avoidance comparison took 4,976 to 5,373 ms. Neither
preference sweeps nor transition acceptance nor a global acceptance-limit
increase is reopened by this option.

No performance campaign was run in the implementation sandbox. Admission is
pending the paired standalone formula-UNREAL 60 s screen and unchanged shipping
race screens at 17 s and 60 s, with captures, no solver deadline, and the P0
panel. Preserve the ten current panel solves and external archived loss
sentinels. Stop on a verified semantic discrepancy, preservation loss, new
memory failure, or translation savings consumed by downstream work. Require a
repeatable end-to-end gain outside the local noise before any default change.
