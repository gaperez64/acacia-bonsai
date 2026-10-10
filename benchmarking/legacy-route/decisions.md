# Legacy Spot-route sprint: decision record

Closed 2026-10-10 with PR #235 at `4a26e57c` admitted. Evidence IDs refer to
[evidence-index.tsv](../evidence-index.tsv); frozen executables are pinned in
[baselines.tsv](../baselines.tsv). Instance names and expected verdicts belong only
in documentation and evaluation lists, never in solver logic.

| Date | Decision | Evidence |
|---|---|---|
| 2026-10-10 | On the 28-input panel, P0 selects translation/lowering (74.3% standalone and 78.3% in-race of observed legacy-worker wall windows, including censored translation lower bounds) and oracle preimage/threshold work (27.4% and 28.8% of sampled cycles in the `g-unreal-120` and `F-G-contradiction-110` profiles, respectively; 17.0% in charging18) as the useful optimization routes. Reject P1b and P2 at diagnosis on this measured panel: millisecond costs, with push work about 1 s total. #200 MONA decode and #208 backward payload locality are not selected by P0. | `legacy20261008-screens` |
| 2026-10-10 | Reject P3 `--translation-level low`: 0/6 targets solved, with race losses on `collector_v3_pb_9` at 17 s and `08` at 60 s. PR #234 is closed; retain the existing translation default. | `legacy20261008-screens` |
| 2026-10-10 | Admit P1a grouped oracle layout as the default (#235). Same-K search plus independent-check cost is 0.54–0.67 of the incumbent on three primary targets. Fix the initial memory stop by releasing search caches before verification and applying a structural memo budget (67.2 MB in the measured configuration); scan remains selectable. | `legacy20261008-screens` |
| 2026-10-10 | Closing full-corpus campaign confirms P1a admission: fresh 17 s solves 1,207 (559 REAL + 648 UNREAL) vs 1,205, PAR-2 11,624.2 vs 11,730.2 s; validated derived 60 s solves 1,244 vs 1,240, PAR-2 35,538.3 vs 36,073.7 s, resource limits 1 vs 2. No paired losses at either cap. ltlsynt/TACAS23 solve 1,263/812 at 17 s and 1,286/854 at 60 s; Acacia still trails ltlsynt. The three near-cap UNREAL gains reproduce 3/3 in cooled repetitions, incumbent timeouts 3/3 each. At 60 s, 34 both-solved inputs improve by more than 1 s, five slow by more than 1 s, worst +3.4 s. Six exact recycling mismatches are driver-adjudicated with unchanged outcomes and hash-bound evidence. `LEGFINAL` becomes the next G1 baseline once merged; [RESULTS.md](../RESULTS.md) lists all gains and disclosures. | `legacy20261008-closing`; amendment `legacy20261008-closing-amend1` adds the candidate 60 s telemetry finding and regenerated validation/report outputs without changing coverage or PAR-2 |
| 2026-10-10 | Reject P4 lazy providers: lazy equals eager on all 28 panel inputs; the closure route hits `row_failure` at about 2 million guards, and TAA providers MEMOUT on 21/28. No provider default change. | `legacy20261008-screens` |

## Initial instrumentation decisions (before campaign)

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
