# On-the-fly Spot solving

Everything about reading Spot's automata on the fly rather than through a
frozen graph: the audit that established what Spot's API could actually
support, the coverage sprint that produced the work packages, and the sprint
that finished it and changed the shipping menu.

The verdict of record is the first section. The rest is how it was reached, kept
because the numbers in it are the evidence for the decision and re-deriving them
costs corpus-days.

## How to select these paths

<sub>Moved here from the top-level README, which was carrying it as reference
material for configurations that are mostly experimental.</sub>

The experimental Spot paths require `-Dacacia_spot_guarded_backend=true`;
the TAA providers also require `-Dacacia_spot_lazy_provider=true`.
`--arms real:small:spot-guarded:spot-lazy,unreal:formula:spot-guarded:spot-lazy`
selects a lazy worker for each polarity. `spot-eager` is the fully enumerated
control of the same TAA/cursor construction. With the default `frozen-graph`
provider, `spot-guarded` uses dense ranks and `spot-guarded-sparse` preserves
the same mixed numeric/Boolean domain in sparse storage. Automaton-unreal
requires `frozen-graph`.

These paths verify decisions before returning them. Candidate limits produce
an inconclusive result; `--candidate-mode fallback` explicitly rebuilds the
existing frozen/backward path after a candidate failure. Synthesis uses the
existing frozen/backward implementation. For diagnostic runs,
`ACACIA_SPOT_CAPTURE_DIR` enables JSON records of the actual transformed
worker formulas, AP partitions, phases, and measured counters.

`-Dacacia_default_candidate_mode=only|fallback` sets the build's default;
`--candidate-mode` still overrides it. Both TAA providers use
`-Dacacia_spot_taa_max_rank_nodes=200000` by default, independently of frozen
guarded search. The common `ACACIA_SPOT_MAX_RANK_NODES` environment override
applies to both; `ACACIA_SPOT_TAA_MAX_RANK_NODES` takes precedence for TAA only.
The experimental `otf_taa_fallback_lazy` and `otf_taa_fallback_eager` presets
use a 1,000-node TAA cap and backward fallback, alongside forward real and
both original forward unreal workers. They remain outside the shipping group.
The `otf_mix_formula_lazy` and `otf_mix_formula_eager` presets instead use
sparse guarded formula-unreal solving alongside forward automaton-unreal,
forward real, and bounded TAA real with backward fallback. Their 1,000-node
TAA cap leaves frozen guarded search's 200,000-node budget unchanged. These
four-worker mixtures are experimental configurations under evaluation.
The node cap is not a deadline: TAA construction or a row/query operation can
exhaust the external timeout before backward fallback starts. The mixed TAA
presets currently fail the panel coverage gate on some fast backward-real cases.

## What is in here

- [SPOT-STREAMING-OTFUR-SPRINT.md](#spot-streaming-otfur-sprint) — **the result of record.** Sparse symbolic-letter solving beats the frozen graph: 1,169 and 1,171 answers against 1,123.
- [POST-PR125-COVERAGE-SPRINT.md](#post-pr125-coverage-sprint) — the six work packages against the 434 instances PR #125's portfolio could not decide, and what each was worth.
- [SPOT-OTF-API-AUDIT.md](#spot-otf-api-audit) — what Spot's on-the-fly API can and cannot be asked for, read out of its sources.
- [otf.md](#otf) — the original handoff spec (superseded, kept for provenance).


---

## Spot OTF sprint completion

<sub>Was `benchmarking/SPOT-STREAMING-OTFUR-SPRINT.md`. Complete as of 2026-09-09. This is the current result of record for the shipping menu.</sub>

Status as of September 9: complete. Implementation, correctness checks,
production gates, all eight full-corpus primary runs, three rounds of paired
repetitions, gain attribution and loss diagnosis have finished. The shipping
menu has been updated; see Decision.

The full-corpus result favors sparse symbolic-letter solving over the existing
frozen Spot graph. The two eligible configurations answer 1,169 and 1,171 cases,
versus 1,123 for the strongest mainline configuration. Both pass the frozen,
labelled, and 2025/2026 panel gates. TAA lazy/eager mixtures answer 1,166/1,165,
but lose fast backward-real cases and fail both panels and the labelled gate.
They remain ineligible for shipping defaults. All primary runs agree wherever
they return a decisive verdict.

### Decision

| hypothesis / candidate | verdict |
|---|---|
| H-Letters, symbolic letters over the frozen Spot graph | **LAND — one guarded unreal arm** |
| Frozen-graph guarded solving in a *real* arm | **STOP** — no new answers on either cohort |
| Guarded solving on *both* unreal reductions | **STOP** — loses cases both old arms answer |
| H-Generation, TAA lazy/eager mixtures | **NOT ADMITTED** — G3 and G4 |
| P2/P3/P5/P6 research tooling | **KEEP RESEARCH TOOLING** |

`docker_default` now reads `otf_sparse_formula`,
`best_decomp_rank_bucketed_semantic_mona`, `best_four_arm_bboxtree`,
`best_decomp_mona_any`. `best_four_arm_contradiction` leaves the shipping group
and becomes a `reference` preset; it is not deleted and its measurements stand.

#### The gain is the guarded unreal arm, measured rather than inferred

The attribution stage re-ran every answer new to all four shipping
configurations with each worker isolated, using
`--arms unreal:<transform>:spot-guarded-sparse --candidate-mode only` against
main's corresponding arm. 383 jobs over 50 instances:

| isolated worker | decides |
|---|---:|
| sparse guarded formula-unreal | **46** |
| sparse guarded automaton-unreal | 43 |
| main forward automaton-unreal | 4 |
| main forward formula-unreal | 3 |
| main backward real | **0** |
| main forward real | **0** |

An isolated arm decides slightly more than the same arm inside a four-child
race (46 against 44), which is expected: the race shares one machine between
four children. The direction is what matters. None of the new coverage comes
from the real workers, which is consistent with REAL staying at exactly 533 for
both candidates and with the guarded real controls adding no answer on either
cohort.

#### Both primary losses are cap-boundary, not capability

The separate 60-second diagnosis, which never replaces a 17-second result:

| instance | main | `otf_sparse_automaton` | `otf_sparse_formula` |
|---|---|---|---|
| `06.ltl` | REALIZABLE 18.978 | REALIZABLE 17.341 | REALIZABLE 17.641 |
| `infinite-race-u10.ltl` | UNREALIZABLE 12.976 | UNREALIZABLE 12.635 | UNREALIZABLE 17.948 |

All three solve `06.ltl` just past the cap, and both candidates are faster than
main there. Its 17-second primary answer for main did not reproduce: main
timed out on it in all three repetition rounds, so the recorded 16.991 s was
itself a boundary result. `infinite-race-u10.ltl` is a real sparse-formula
regression at 17 seconds and is retained as one.

#### Why this menu

Slots are chosen by marginal contribution against the rest of the menu, not by
union size, because `scripts/acacia-bonsai.sh` runs one user-selected
configuration. Union of `{semantic MONA, bboxtree, MONA/any}` is 1,111.

| slot | configuration | individual | marginal | why |
|---|---|---:|---:|---|
| 1 | `otf_sparse_formula` | **1,171** | **+66** | best measured; the guarded unreal arm |
| 2 | `best_decomp_rank_bucketed_semantic_mona` | 1,058 | — | strongest backward configuration |
| 3 | `best_four_arm_bboxtree` | 1,102 | — | a different downset, so a different failure mode |
| 4 | `best_decomp_mona_any` | 1,046 | — | the retained pre-sprint shape |

Resulting union 1,177, against 1,133 today: **+44 answers, zero union losses**.
`otf_sparse_automaton` contributes +64 and would give 1,175; the two differ only
in which unreal reduction is guarded.

Keeping slot 3 is load-bearing rather than decorative. `best_four_arm_bboxtree`
is the only retained member that answers `infinite-race-u10.ltl`, the arriving
preset's one reproducible loss, and the two MONA members are what answer
`06.ltl`. A menu chosen purely to maximise the union would have scored 1,179 and
dropped this slot.


### Full-corpus primary results

Each row is an actual invocation of a configuration's compiled defaults on all
1,524 SYNTCOMP26 logical inputs, once at 17 seconds with 8 GiB and zero swap.
Gains and losses are relative to mainline `best_four_arm_contradiction`.
These single-pass outcomes stay fixed; paired repetitions supplement them.

| Configuration | Answered | REAL | UNREAL | Gains / losses | PAR-2 seconds | New to all four main configs | Default eligibility |
|---|---:|---:|---:|---:|---:|---:|---|
| main contradiction | 1,123 | 533 | 590 | 0 / 0 | 14,254.294 | 0 | Existing |
| main semantic MONA | 1,058 | 515 | 543 | 7 / 72 | 16,529.239 | 0 | Existing |
| main bboxtree | 1,102 | 533 | 569 | 2 / 23 | 15,029.815 | 0 | Existing |
| main MONA/any | 1,046 | 515 | 531 | 8 / 85 | 16,996.803 | 0 | Existing |
| `otf_sparse_automaton` | 1,169 | 533 | 636 | 47 / 1 | 12,748.359 | 42 | Gates passed; repetitions pending |
| `otf_sparse_formula` | 1,171 | 533 | 638 | 50 / 2 | 12,736.344 | 44 | Gates passed; repetitions pending |
| `otf_mix_formula_lazy` | 1,166 | 527 | 639 | 55 / 12 | 12,936.877 | 49 | NOT ADMITTED: G3/G4 |
| `otf_mix_formula_eager` | 1,165 | 527 | 638 | 54 / 12 | 12,959.725 | 48 | NOT ADMITTED: G3/G4 |

Both sparse candidates miss `06.ltl`, which main answers at 16.991 seconds.
The formula candidate additionally misses `infinite-race-u10.ltl`, which main
answers at 13.280 seconds. Every primary loss is retained in the comparison;
the separate 60-second diagnosis will not change either score. The 12 losses
shared by both TAA mixtures include ten additional real regressions and this
same unreal loss. Their 19/16 memory-limit outcomes also exceed the two in
each sparse configuration and the main contradiction control.

The four existing shipping configurations jointly answer 1,133 distinct cases.
Replacing only contradiction with sparse formula would raise that union to
1,177 with zero union losses. Across the six eligible measured configurations,
the largest four-member union is 1,179: both sparse presets plus semantic MONA
and MONA/any, gaining 46 cases and retaining all 1,133 old answers. This is
the union of separately selectable configurations, not a measured combined
race or a claim about running their workers simultaneously. Repetition stability
and attribution still need review before selecting the shipping menu.

The frozen repetition plan covers 259 distinct instances, 3,104 selected
baseline/candidate comparisons, and 5,412 actual invocations over three rounds.
It includes coverage changes, decisive near-cap runs, major timing changes,
and previously exposed panel/selection observations. Round 1 is complete and
round 2 is in progress. The planned diagnostics capture every primary answer
absent from all four main configurations, isolate the relevant old and new
workers, and replay the TAA construction behind its new real answers with
identical eager/lazy options.

Raw primary results, hashes, the repetition plan, and the descriptive menu
comparison are preserved in `_bm-logs.spot-otf-20260907/full-closing/`,
`full-closing-manifest.json`, `closing-repetitions/plan.json`, and
`primary-review.json`. The final annotated tables and plots are generated after
the timed campaign finishes.

The initial primary driver exited unexpectedly during bboxtree on September 8.
All 4,410 completed rows were preserved and verified unchanged on resume. Its
last unrecorded invocation survived the driver and eventually hit the memory
limit; that orphan has no primary TSV row and is excluded from the scores.
The missing bboxtree invocation was rerun at the original 17-second cap, then
the remaining primary jobs continued under a user systemd service. The incident,
original CSV prefixes, and orphan's journal are retained under
`interruption-20260908-0710/`. Completed results were neither discarded nor
replaced with the best observed repetition.

### Scope and baseline

The September 7 completion request extends `otf.md`'s default-off scope: test
all useful new mechanisms and polarities against the current best configurations,
select a candidate default portfolio from measured evidence, then compare its
actual race with the mainline shipping configurations on all 1,524 SYNTCOMP26
logical entries. This supersedes the original restriction on changing defaults;
it does not waive correctness or admission gates.

The remote default branch is `master`. Its tip was verified over HTTPS and
frozen at `1c028f13490862bfdbed5256d0e2404ee4d74355`. The baseline worktree is
`/tmp/acacia-otf-main-1c028f13`. All four members of its `docker_default` group
have been rebuilt with the same release compiler profile (including its
`-Ofast -march=native` options), LTO, native TLSF,
the same installed Spot/BuDDy, and their own unchanged options:

- `best_four_arm_contradiction`
- `best_decomp_rank_bucketed_semantic_mona`
- `best_four_arm_bboxtree`
- `best_decomp_mona_any`

Full options and binary hashes are in
`_bm-logs.spot-otf-20260907/main-baselines.json`. The Posets, tlsf-tools, and
benchmark pins agree between mainline and the sprint. The corpus is the existing
materialized `tlsf-corpus`; no benchmark input or dependency is changed.

P7 was recovered without consuming stash
`b37ba53f042b11cd7a991f1fc33e079f75f7fc7a` (`p7-verify`). Its old 43-test result
was a debug build without native TLSF and is historical evidence only.

The earlier local CSVs were audited for reuse during the closing run. The
preserved selection builds (`build_cs_mix_contra`, `build_cs_mix_bbox`,
`build_cs_inc_any`) and the old semantic reference (`build_p5_B`) compile the
solver at `-O0 -g`, without LTO. The current mainline and OTF builds use the
same optimized release profile. The archived selection tables also combine
the first successful result from caps 1/5/17 rather than measuring each input
once at 17 seconds. Most decisively, the fresh mainline contradiction control
answers 1,123 cases versus the archive's 1,063: 60 gains, no losses, and no
opposite verdicts, before adding any OTF work. Those gains cannot be credited
to the candidate. The archived CSVs remain historical evidence, while the
closing decision uses matched release baselines. The audit and per-instance
differences are in [`spot-otf-baseline-reuse-audit.json`](spot-otf-baseline-reuse-audit.json).
This audit does not isolate compilation from all source/protocol differences.
The archived external `ltlsynt` and Acacia 1.x measurements are preserved;
neither external tool is rerun for this sprint.

### Completion checklist

- [x] Restore P7 and freeze/rebuild current mainline shipping baselines.
- [x] Finish provider selection, native worker capture, and sparse frozen control.
- [x] Validate real/formula-unreal eager/lazy transformations and all certificate paths.
- [x] Run unit/config/version, native TLSF, labelled, frozen, and sanitizer checks.
- [x] Finish C0/C1 demand, C3/C3s, and real-worker/formula-unreal C4/C5 experiments.
- [x] Compare isolated candidate arms on discovery and family-held-out cohorts.
- [x] Select candidate portfolios within four children and record gate eligibility.
- [x] Run the final full-corpus comparison against all current shipping configurations.
- [x] Finish paired repetitions, primary-gain attribution, and targeted loss diagnosis.
- [x] Publish per-instance results, losses, conflicts, PAR-2, CPU/memory, and decisions.

### Protocol

Timing uses one sequential invocation per systemd scope, a 17-second cap,
8 GiB memory, and zero swap. Full closing runs use a single `--caps 17` pass;
staged discovery results do not substitute for that measurement. Gains, losses,
major claimed speedups, and decisive runs above 13.6 seconds receive three
alternating paired repetitions. The 60-second cap is diagnostic only.

No timing campaigns overlap builds or other campaigns. Existing status
exceptions and exact family provenance remain authoritative. An incomplete or
censored graph has no utilization denominator. Fixed-K losses never become
top-level unrealizability answers without the existing worker reduction.

Each hypothesis will receive LAND DEFAULT-OFF ARM / KEEP RESEARCH TOOLING /
STOP / NOT ADMITTED, followed by a separate recommendation about the measured
default configuration. The sparse candidates have passed the admission gates;
shipping selection remains pending until the repetitions and attribution finish.
Both TAA mixtures are excluded from default promotion by their G3/G4 failures.

### P7 integration

The runtime now accepts per-arm providers in
`--arms polarity:transform:backend[:provider]`. Frozen graphs support backward,
forward, dense guarded, and sparse guarded solving. The TAA eager and lazy
providers support real and formula-unreal guarded workers. Provider defaults
for a polarity fill only arms without an explicit provider; semantic duplicate
arms are rejected after this resolution. The automaton-unreal transform still
requires the existing frozen graph operation.

The sparse frozen control uses the same optimized graph and mixed coordinate
caps as dense guarded solving. The TAA eager and lazy controls share the P5
single-cursor construction and all-numeric sparse engine. Eager mode enumerates
that same provider before search; lazy mode requests rows during search and
verification. Provider rows survive K changes, while ranks, queries, proofs,
and verification state are rebuilt. A verified fixed-K loss remains
inconclusive at the LTL worker level.

Synthesis selects frozen/backward solving for real workers. Explicit fallback
releases a failed TAA attempt and rebuilds the frozen/backward route from the
captured transformed formula; it does not materialize the failed provider.
For frozen guarded failures, fallback reuses the already preprocessed graph.
Neither mode treats Kmax exhaustion as a resource failure. All new compile
gates default off, and enabling compilation alone leaves default arm selection
unchanged. The `otf_*` presets remain outside the shipping group pending the
production gates and full comparison below.

`ACACIA_SPOT_CAPTURE_DIR` enables atomic records at actual worker boundaries,
including the exact transformed formula and partition. Records flush at phase
boundaries and normal destruction. A killed worker's final file can therefore
contain an earlier milestone: its counters are not final censored measurements.
These records are collected in separate diagnostic runs rather than in primary
timing runs.

### Validation and frozen cohorts

The debug unit/version run passed all 45 tests. ASan/UBSan with leak detection
passed all eight relevant suites outside the tracing sandbox (LeakSanitizer
cannot operate under ptrace). Follow-up tests cover forced sparse budgets and
verifier failures. The dense and sparse engines agree on 5,000 generated mixed
rank games; corrupt certificates are rejected. Native Mealy, Moore, and strict
TLSF fixtures compare independently launched eager/lazy workers after the real
and formula-unreal transformations. The Python suite without optional extension
modules passed 553 tests, with one skip; additional campaign tests validate
resource accounting and reject resumes with changed binaries or treatments.

`spot-otf-cohorts/manifest.json` freezes discovery and held-out inputs before
new timing results. Discovery has 36 instances from 24 families: the existing
24 seeds plus their committed solved neighbors. The existing frontier selector
found 15 held-out targets from 13 families after excluding every family in all
82 earlier Spot letter measurements. Missing annotations are handled by the
existing status/exception protocol; they are not assumed to be realizable.

The implementation binaries are frozen at `864be8d5` (binary hashes and full
options in `candidate-builds.json` under the run log directory). The pre-OTF
source tree is byte-identical to the frozen mainline tree; the apparent Git
divergence before P0 consists only of merge commits. The release build with
Spot features disabled passed 43 tests, and Posets passed 18 tests. Release CLI
checks account for compiled-out verbose output and compare C3/C3s under the
same optional contradiction preprocessing.

P1 demand collection is complete: `spot-demand-results.tsv` records 108 workers on
36 instances. Fourteen workers that completed with an answer include 11 partial
row utilizations and three complete utilizations. This is utilization of an already
optimized frozen graph, not a measured translation saving. Incomplete or
portfolio-cancelled workers retain unavailable graph denominators. The compact
table retains each original status; the full unmodified rows, including action
profile IDs, are in the run directory's `spot-demand-raw.tsv`.

### Measurement accounting

Coverage normalization now honors a known memory-limit result before classifying
a negative return code as a crash. Ten eager formula-unreal discovery rows
originally marked CRASH are confirmed 8 GiB OOM kills in the user journal.
The raw rows are preserved; separate annotated TSVs recover final scope CPU,
memory, swap, and OOM accounting. The journal join prefers an exact scope ID;
older rows require matching solver arguments, input, and a unique end timestamp.
Missing or ambiguous accounting remains unavailable, and an actual zero stays
distinguishable from a missing value.

Comparisons charge every nondecisive result the full PAR-2 penalty, including
fast UNKNOWN and memory limits. Resource totals include observation counts.
The comparison tool rejects different cohorts and repeated instances, so staged
caps or repetitions cannot silently replace primary measurements.

The complete 22-treatment discovery sweep has zero cross-treatment verdict
disagreements. On either translation preference, C0 real answers 13 cases;
dense and sparse guarded real each answer seven (zero gains, six losses).
Formula-unreal rises from one answer to three, and automaton-unreal from two
to four, for both guarded representations. Both gains are the robot-resource
and robot-to-target-charging cases. Eager and lazy TAA real each answer the
same AMBA encode case, while losing all 13 C0 real answers in isolation.
Neither TAA unreal arm answers a case; eager has ten and lazy nine confirmed
OOM kills. Preferences `small` and `any` have identical coverage for every
matched treatment on this cohort. These are single-pass isolated controls,
before shipping-portfolio comparisons or admission decisions.

The Python suite after the accounting changes passed 553 tests with one skip;
the rebuilt debug suite again passed 45 tests. Future coverage rows record the
scope ID directly, and validated resumes can upgrade the previous header
without rerunning or inventing resource measurements. Frozen guarded capture
now includes preparation time and avoids formatting capture values when capture
is disabled. The 22 control binaries remain frozen at `864be8d5`; this later
diagnostic change does not rewrite or replace their measurements.

### Shipping references and portfolio selection

On the 36 discovery cases, the frozen mainline contradiction portfolio answers
19 (15 real, four unreal; PAR-2 598.575 s), bboxtree answers 17 (15 real,
two unreal; 675.406 s), semantic MONA answers 12 (ten real, two unreal;
832.419 s), and plain MONA/any answers 12 (ten real, two unreal; 840.915 s).
Their union also contains 19 answers. None answers the AMBA encode or either
robot case reached by the new isolated treatments. The current three-forward-arm
reference answers 15 (13 real, two unreal; 744.149 s). These are measured
portfolios; adding isolated answers does not establish the coverage of a race.

The next selection manifest, frozen before held-out treatments finish, contains
ten four-child races. On the contradiction base, it tests guarded automaton-unreal
(dense and sparse), guarded formula-unreal, guarded on both unreal reductions,
and guarded automaton-unreal combined with a TAA real worker (dense/lazy,
dense/eager, sparse/lazy). The latter combination retains backward and forward
real workers and uses the fourth slot for TAA real. It is also tested on the
bboxtree, semantic MONA, and plain MONA/any component bases. All ten run on both
frozen cohorts. Selection prioritizes decisive coverage, reports every loss and
conflict, and uses PAR-2 to compare equal coverage; small timing differences need
paired repetitions before they support a claim. Formula-unreal TAA remains in
the held-out mechanism checks despite its negative discovery result.

Before the portfolio races began, the held-out formula-unreal guarded control
answered `GF-G-contradiction6`, `GF-G-contradiction7`, and `g-unreal-39`;
guarded automaton-unreal answered only the last. An eleventh race therefore
combines guarded formula-unreal with TAA real on the contradiction base. The
original ten-treatment manifest is retained alongside this recorded extension.
Neither the cohorts nor their membership changed. The held-out cohort informs
portfolio selection after testing transfer from the earlier discovery families;
it is not an untouched validation set for the resulting portfolio choice.
The sparse formula-unreal control subsequently retained those three answers at
12.340, 13.122, and 8.946 s, versus dense 13.627, 14.813, and 10.084 s.
This prompted sparse versions of the both-unreal and formula-unreal/TAA-real
races, bringing selection to 13 portfolios before any of those races began.
These first-pass differences motivate a comparison, not a stable speedup claim.

All 17 held-out treatments finished with zero cross-treatment verdict conflicts,
including comparisons on cases without an annotation. The mainline contradiction
and bboxtree portfolios answer two of 15 (both real), semantic MONA answers none,
and plain MONA/any answers one real case. The mainline union is two answers.

| isolated treatment | real answers | unreal answers | PAR-2 seconds |
|---|---:|---:|---:|
| C0 real | 2 | 0 | 454.717 |
| C3 real | 0 | 0 | 510.000 |
| C3s real | 1 | 0 | 485.880 |
| C0 formula-unreal / automaton-unreal | 0 | 0 | 510.000 each |
| C3 formula-unreal | 0 | 3 | 446.523 |
| C3 automaton-unreal | 0 | 1 | 486.290 |
| C3s formula-unreal | 0 | 3 | 442.407 |
| C3s automaton-unreal | 0 | 3 | 449.893 |
| C4 real | 1 | 0 | 476.980 |
| C5 real | 1 | 0 | 476.995 |
| C4 unreal / C5 unreal | 0 | 0 | 510.000 each |

Sparse automaton-unreal reaches both contradiction cases at 15.295 and 16.699 s,
plus `g-unreal-39` at 9.899 s. Its margins are smaller than sparse formula-unreal's.
Sparse real reaches the already-solved round-robin arbiter at 9.880 s but adds
no answer; dense real loses both C0 real answers on this cohort.
Across both cohorts, the frozen guarded real controls have no new answers.
Their only first-pass improvements of at least 25% over C0 occur on one dense
case or two sparse cases, each with a C0 time below one second. They do not meet
the alternative requirement of five hard completed improvements. The candidate
portfolios therefore retain the two existing real workers.

Both TAA real controls add `amba_decomposed_lock_pb_14_pe_`, which all four
shipping configurations time out on: eager takes 0.980 s and lazy 0.995 s.
Together with the discovery encode case, this supports evaluating a TAA real
portfolio slot in two AMBA families. It does not establish a benefit from lazy
construction. Both TAA unreal controls have seven memory limits and eight
timeouts on held-out cases, with no answer. Worker and same-provider replay
accounting remain necessary before the generation decision.

The extra C0 demand reference now matches P1's outer wrapper without GNU time.
Both answer the same 15 of 36 with zero conflicts. First-pass answer-conditioned
time is 30.599 s for C0 and 34.826 s with instrumentation; PAR-2 is 744.599 versus
748.826 s, and the median paired time ratio is 1.213. Instrumentation is not
free. Three alternating paired repetitions on those same 15 completed cases
confirmed unchanged answers. Answer-conditioned C0/C1 totals were
30.341/34.577 s, 31.289/34.906 s, and 31.598/35.094 s; median paired time
ratios were 1.193, 1.187, and 1.132. All primary performance races use
binaries with diagnostics disabled and no worker capture.

### Actual portfolio discovery results

All 13 four-child races completed discovery with zero verdict conflicts. The
contradiction-base guarded-only variants all answer 21 of 36 (15 real, six
unreal), gaining both robot cases and retaining all 19 mainline answers. The
contradiction-base TAA real combinations all answer 22 (16 real, six unreal),
also gaining AMBA encode. These are actual races, not a virtual union.

| contradiction-base race | answered | gains / losses vs main | PAR-2 seconds |
|---|---:|---:|---:|
| dense guarded automaton-unreal | 21 | 2 / 0 | 537.880 |
| sparse guarded automaton-unreal | 21 | 2 / 0 | 537.229 |
| dense guarded formula-unreal | 21 | 2 / 0 | 543.939 |
| dense guarded both-unreal | 21 | 2 / 0 | 539.597 |
| sparse guarded both-unreal | 21 | 2 / 0 | 536.172 |
| dense guarded automaton-unreal + lazy TAA real | 22 | 3 / 0 | 512.972 |
| dense guarded automaton-unreal + eager TAA real | 22 | 3 / 0 | 512.476 |
| sparse guarded automaton-unreal + lazy TAA real | 22 | 3 / 0 | 511.359 |
| dense guarded formula-unreal + lazy TAA real | 22 | 3 / 0 | 518.947 |
| sparse guarded formula-unreal + lazy TAA real | 22 | 3 / 0 | 517.805 |

The bboxtree, semantic MONA, and plain MONA/any TAA combinations each answer
20 (16 real, four unreal), with three gains and two losses relative to mainline
contradiction. Their PAR-2 scores are 589.625, 585.380, and 589.638 s. They
improve their original component bases but trail the contradiction combinations
on discovery. Actual held-out races remain in progress; none of these numbers
substitutes for the closing full-corpus comparison or paired repetitions.

### Selected configurations

All 13 actual held-out races completed without verdict conflicts or losses
relative to mainline contradiction. Dense guarded-only variants and sparse
automaton-unreal each answer three of 15. Sparse guarded on both unreal
reductions answers four (two real, two unreal), including
`GF-G-contradiction6` at 16.208 s. Most TAA real combinations answer four
(three real, one unreal). The sparse formula-unreal plus lazy TAA real
combination answers five (three real, two unreal), retaining the contradiction
case at 16.500 s as well as both existing real answers and the new AMBA answer.
`GF-G-contradiction7` remains unresolved by every actual portfolio.

The following named presets are candidates with their measured arms compiled
as defaults; their role remains `sweep`, outside the shipping group:

- `otf_sparse`: backward/forward real plus sparse guarded on
  both unreal reductions. Discovery 21/36, held-out 4/15; PAR-2 536.172 and
  418.626 s.
- `otf_taa_lazy`: backward/forward real plus sparse guarded
  formula-unreal and lazy TAA real. Discovery 22/36, held-out 5/15; PAR-2
  517.805 and 385.913 s.
- `otf_taa_eager`: the otherwise identical eager TAA control.
  This exact sparse-formula combination still needs its fresh-default checks.
  The previously measured dense-automaton eager/lazy twins have identical
  coverage on both cohorts and timing differences too small to settle the
  provider choice. Keeping an eager control through closing avoids choosing
  a lazy default from a construction gain alone.

The first two measured races retain all mainline answers on both cohorts.
Their first-pass near-cap gains and slowdowns still require repetitions.
Closing measurements will use these presets' actual defaults with empty
runtime flags, against all four frozen mainline shipping configurations.

The demand driver now accepts an explicit source revision for a frozen binary.
This prevents later repetitions of the original `864be8d5` diagnostics binary
from incorrectly recording the newer checkout revision; its binary hash is
also checked before the paired repetitions.

The isolated `prioritized_arbiter_unreal2_pb_30_pe_` error was reproduced with
the frozen mainline formula-unreal worker: both fail with Spot's 64-acceptance-set
limit. This is an inherited capacity limit. The library limit and binary build
remain fixed for the comparison.

#### Native capture accounting

The native diagnostic run contains 66 isolated worker invocations, separate
from primary timings. Several successful guarded jobs pass through multiple K
bounds. The latest capture records the last completed attempt, and metrics from
that attempt can remain present while the next search is incomplete. It must
not be interpreted as whole-job preparation, search, or verification time.

`ACACIA_SPOT_CAPTURE_HISTORY=1`, together with `ACACIA_SPOT_CAPTURE_DIR`, now
retains milestone snapshots in an additional `.history.jsonl` file. Consumers
keep the complete JSON-line prefix if a worker is killed, and deduplicate
`verified-attempt` snapshots by worker record and K. They sum completed-attempt
phase times and take peak maxima across attempts; incomplete jobs provide only
lower bounds. The atomic latest `.json` record remains available. History is
opt-in and used only in explanatory reruns. Closing performance binaries will
be frozen and rebuilt after this accounting change.

The original 66-invocation capture campaign finished at source `e5e4757c`.
Twenty-nine successful worker jobs were then recaptured with history at
`e7061657`. The three selected default builds were rebuilt at that revision;
each passed all 43 unit/version checks. The previous binaries and manifests
are preserved in `_bm-logs.spot-otf-20260907/e5e4757-frozen-builds`.

[Native worker accounting](spot-otf-worker-accounting.tsv) joins each history
to its P1 record using the exact instance and worker PID. These are diagnostic
measurements, including instrumentation, and are not primary speed claims.
For frozen providers, row caches are rebuilt per K; the table does not present
a sum of generated rows as a union of distinct rows. For TAA, the provider cache
and its cumulative counters survive K changes.

| Sparse formula-unreal job | Completed K | Translation ms | Search ms, summed | Verification ms, summed |
|---|---|---:|---:|---:|
| robot-resource-2d16 | 2,5 | 4,326 | 5.2 | 11.2 |
| robot-to-target-charging3 | 2,5,8 | 3,393 | 87.8 | 12.2 |
| GF-G-contradiction6 | 2,5,8,11 | 3,807 | 5,110.7 | 3,409.2 |
| GF-G-contradiction7 | 2,5,8,11 | 3,891 | 5,346.8 | 3,543.7 |
| g-unreal-39 | 2,5,8,11,14 | 1,932 | 4,429.3 | 2,799.6 |

The robot gains remain dominated by translation. The harder contradiction
and g-unreal jobs spend substantial time in both search and independent
verification; reporting only their final K would hide most of that work.

#### Same-provider replay and the lazy-generation hypothesis

Eight fresh, independent P6 invocations replay four actual captured worker
formulas: two AMBA wins, the easy `01` regression, and formula-unreal
`full_arbiter_unreal1_pb_2_8_pe_`. The replay consumes the already transformed
bad-language formula, without another negation. The captured transformed AP
partition is reordered into the replay tool's lexical AP order. Both providers
use the same K range and schedule. These runs use a 17-second replay cap,
8 GiB cgroup limit, zero swap, and an additional 8 GiB address-space limit;
they are explanatory checks, not substitutes for native whole-pipeline timing.

[Replay accounting](spot-otf-streaming-accounting.tsv) preserves completed
attempt totals, censored outcomes, provider rows, retained rank payloads, and
process memory. All four completed eager/lazy K comparisons agree and have
verified certificates: both AMBA jobs lose K=2 and win K=5. The other two jobs
are inconclusive, with no invented utilization denominator for the incomplete
full-arbiter enumeration.

| Real job | C4 total wrapper rows | C5 rows after K=2 | C5 rows at verified K=5 | Final utilization |
|---|---:|---:|---:|---:|
| AMBA encode20 | 78 | 53 | 78 | 100% |
| AMBA lock14 | 62 | 61 | 62 | 100% |

The native history puts AMBA encode20's eager enumeration at 7,580.6 ms,
with 213.5 ms search and 46.9 ms verification over both K attempts. Lazy
mode charges generation inside search: 8,014.5 ms search and 48.4 ms
verification. Those single diagnostic runs do not establish a speed winner.
They do establish that both positive examples eventually request every row.
The gains therefore support testing the TAA construction and guarded search
in a real arm; a lazy-generation preference still needs independent evidence.

The native negative captures locate both providers' `lift4` and
`GF-G-contradiction6` formula-unreal failures in the provider factory. The
full-arbiter eager run reaches enumeration, while lazy reaches its first
search without completing it. These phase milestones are censored observations.
The earlier scope journal identifies memory-limit failures separately from
timeouts; an absent counter is not zero work.

#### Candidate-only and fallback attribution

Eighteen separate capped invocations compare natural and deliberately limited
candidates. Candidate-only and fallback both solve the two AMBA real positives
and the two selected sparse formula-unreal positives; both time out on TAA
real `01`. A forced zero-row budget returns UNKNOWN in candidate-only mode
for sparse frozen, TAA lazy, and TAA eager workers; each fallback recovers the
existing backward REALIZABLE answer. With fixed K=2, sparse frozen `01`
returns UNKNOWN in both modes and does not invoke fallback. There are no
opposite verdicts. Raw commands, stdout/stderr, captures, and binary provenance
are in `_bm-logs.spot-otf-20260907/fallback-attribution`.

### Production gates in progress

All three selected `e7061657` builds pass the 43 unit/version checks. The
existing frozen G1 gate passes for `otf_sparse` (40/40), and fails for both
TAA configurations (39/40), with zero opposite verdicts. Both lose
`syntcomp24/load_balancer_unreal25.ltl` at 17 seconds.

A fresh matched mainline run on the same 40 logical inputs solves 39/40:
`load_balancer_unreal25` finishes UNREALIZABLE in 12.204 seconds, while
`Morning_f2774e0b` times out. All three new configurations solve the latter.
Thus the TAA portfolio's load-balancer loss is a real tradeoff against current
main, despite equal 39/40 totals. It is not explained by the different old
preset that supplied the frozen expectations. The raw gate reports and exact
matched failure sets are in `_bm-logs.spot-otf-20260907/g1-selected`. The
matched helper uses the existing native-TLSF route for the 2025 portion and
the same vendored LTL inputs as Meson for the 2024 portion.

The initial G3 campaign used three candidates and a fresh main reference on
each 180-case panel; the incomplete campaign and revised measurement protocol
are described below. Shared-reference file and binary hashes are recorded
explicitly; old panel timings do not substitute for fresh references.
G4's registered labelled set contains
624 cases (461 realizable and 163 unrealizable), at the existing 30-second
correctness timeout. No shipping default is promoted by these partial results.

The first G3 comparison is complete: on the SYNTCOMP25 panel, `otf_sparse`
solves 127/180 versus fresh mainline's 121/180, with six gains, zero losses,
and zero opposite verdicts. PAR-2 improves from 2,186.608 to 1,989.080 seconds;
the existing gate passes. All six gains are UNREALIZABLE: `Morning_f2774e0b`,
`GF-G-contradiction4`, `GF-G-contradiction6`, `ltl2dba_C2_unreal_pb_16_pe_`,
`ltl2dba_theta_pb_14_pe_`, and `robot-resource-2d17`. The GF-G6 and C2 jobs
finish near the cap (16.621 and 15.945 seconds), so their stability still
requires repetitions. Other panel comparisons remain in progress.

The SYNTCOMP26 primary panel gives a counterexample to promoting both guarded
unreal arms: `otf_sparse` solves 139/180 versus main's 140/180. It gains
`robot-to-target-charging6` (7.170 s), but loses `g-unreal-113` (main 7.525 s)
and `workstation_resupply_pb_3_pe_` (main 16.539 s). PAR-2 is 1,458.276
versus 1,457.338 seconds, within the documented noise floor. The first loss
is well away from the cap; the second requires the gate's longer diagnostic
and paired repetitions. Primary 17-second coverage remains 139 versus 140
regardless of a longer diagnostic answer. These findings motivate a targeted
mixed portfolio retaining one old unreal worker before final selection.

#### Panel runner failure and matched remeasurement

The original shared-scope lazy TAA SYNTCOMP25 campaign stopped before writing
a complete candidate CSV. Unit `acacia-landing-campaign-378438.scope` hit its
8 GiB limit; the journal records `oom-kill` and termination of the scope that
contained the panel driver. Its 133 logged results are a censored prefix, not
a valid 180-case score. The raw log and the exact journal are preserved.

The existing landing wrapper now supports `--scope-mode instance`, using
`run-subset.py`'s existing per-solver scope support. Each invocation remains
limited to 17 seconds, 8 GiB, and zero swap. The driver survives a solver OOM
and records its resource-limit outcome. Changing modes rejects reuse of old
CSVs; both binaries' panel measurements will be refreshed under this mode.
The previous shared-scope results above remain separate preliminary evidence.
Forty-four relevant Python tests pass, including resource-limit continuation,
unique scopes and bounds per invocation, and rejection of mismatched or
missing scope provenance on resume.

#### Which old unreal arm must survive?

Eight isolated 17-second runs attribute the two hard losses using the exact
native SYNTCOMP26 input and the original G1 LTL input respectively:

| Job | Old formula | Old automaton | Sparse guarded formula | Sparse guarded automaton |
|---|---:|---:|---:|---:|
| g-unreal-113 (native) | U, 6.598 s | U, 6.589 s | timeout | timeout |
| load_balancer_unreal25 (G1 LTL) | timeout | U, 9.849 s | timeout | U, 10.141 s |

Either old unreal worker can preserve g-unreal-113. The load-balancer case
needs the automaton transform in this experiment. The resulting focused
selection compares both mixed portfolios on 13 observed gains, losses, and
held-out targets. Both retain backward and forward real workers and exactly
four children; each retains one existing forward unreal worker and uses one
sparse guarded unreal worker. The original larger panels now inform this
selection and are not described as untouched validation of the new mixture.

The two mixed variants each answer 10/13 in the focused pass. The
automaton-guarded mixture adds C2-unreal16, while the formula-guarded mixture
preserves the near-cap workstation real answer. Three alternating repetitions
confirm C2 for automaton-guarded (3/3, 15.634–16.715 s) and not formula-guarded
(0/3). Workstation succeeds 2/3 with automaton-guarded and 3/3 with
formula-guarded. Both mixtures therefore remain under consideration.

The existing real workers were also isolated using **main's exact contradiction
configuration**, avoiding a comparison to the earlier pure-F preset with
different options. On the 17 main-answered real cases from the two selection
cohorts, backward answers 10 and forward 15; their union is 17.
`arbiter_with_cancel_pb_5_pe_` and `collector_v1_pb_9_pe_` require backward
in these capped runs. Seven other cases require forward. Both real workers
are retained. Each existing unreal worker answers all four main-answered
unreal cases in those cohorts; this is a local observation, not a full-corpus
redundancy claim. The G1 load-balancer observation favors retaining the
existing automaton-unreal worker in the TAA-containing mixture.

Four revised actual-default presets are frozen for the fresh gates:

- `otf_sparse_automaton`: B-real, F-real, F-formula-unreal, sparse guarded automaton-unreal.
- `otf_sparse_formula`: B-real, F-real, sparse guarded formula-unreal, F-automaton-unreal.
- `otf_taa_real_lazy`: B-real, F-real, F-automaton-unreal, lazy TAA real.
- `otf_taa_real_eager`: the otherwise identical eager TAA control.

The TAA mixtures now test the TAA addition separately from replacing the
remaining unreal worker with guarded solving. Their fresh gates must establish
whether the retained old worker preserves the wider mainline coverage. The
initial both-guarded and guarded-plus-TAA recipes remain recorded with their
failures; they are not silently relabelled as these revised configurations.
All presets remain outside the shipping group. The revised freeze is
`4bf4cf98b4cb2f445a21c282de173534140f1936`; its solver C++ code is unchanged
from `e7061657`. All four revised binaries pass 43 unit/version tests each,
and the focused configuration/runner suite passes 115 tests. The existing
diagnostic and earlier performance binaries are retained.

Both revised sparse mixtures pass G1, 40/40 with zero verdict conflicts.
Their summed PAR-2 scores are 38.422 and 39.220 seconds for guarded automaton
and guarded formula respectively. Both revised TAA mixtures answer 38/40 at
17 seconds and fail the gate on `syntcomp25/infinite-race-u4.ltl`: fresh main
answers UNREALIZABLE in about 0.6 seconds, while both candidates remain
inconclusive at 51 seconds. Their other primary miss, `Morning_f2774e0b`,
answers at about 22.3 seconds in the longer diagnostic. These longer answers
do not upgrade primary coverage. Fresh main answers 39/40 on the matched
40-case set, missing Morning. Thus keeping the old automaton-unreal arm
protects the prior load-balancer loss but exposes a case needing the old
formula-unreal arm. Raw results are in `g1-revised` under the run directory.

The revised G3 run uses fresh per-instance scopes for both 180-case main
references and all four candidate portfolios. The two sparse mixtures have
completed their primary measurements, as have the matched TAA comparisons.
The panels overlap, so their totals must not be added as distinct coverage.

| Native TLSF panel | Mixture | Main answers | Candidate answers | Gains / losses | Main PAR-2 (s) | Candidate PAR-2 (s) |
|---|---|---:|---:|---:|---:|---:|
| SYNTCOMP25 | sparse automaton | 121 | 125 | 5 / 1 | 2186.818 | 2032.211 |
| SYNTCOMP25 | sparse formula | 121 | 125 | 5 / 1 | 2186.818 | 2028.646 |
| SYNTCOMP26 | sparse automaton | 139 | 141 | 2 / 0 | 1476.290 | 1422.227 |
| SYNTCOMP26 | sparse formula | 139 | 140 | 1 / 0 | 1476.290 | 1432.412 |
| SYNTCOMP25 | TAA real lazy | 121 | 111 | 3 / 13 | 2186.818 | 2498.758 |
| SYNTCOMP26 | TAA real lazy | 139 | 132 | 1 / 8 | 1476.290 | 1697.392 |
| SYNTCOMP25 | TAA real eager | 121 | 111 | 3 / 13 | 2186.818 | 2497.725 |
| SYNTCOMP26 | TAA real eager | 139 | 132 | 1 / 8 | 1476.290 | 1697.077 |

There are zero opposite verdicts. On the 2025 panel, both gain Morning, GF-G4,
theta14, and robot-resource17; guarded automaton additionally gains C2-unreal16,
while guarded formula gains GF-G6. Both lose the near-cap workstation3 answer
in this pass. On the 2026 panel, both gain robot-to-target-charging6; guarded
automaton also answers workstation3 when the fresh main reference times out.
The latter is a variable near-cap observation, not a new mechanism-specific
coverage claim. Both preserve g-unreal113, which the both-guarded recipe lost.

The revised TAA lazy recipe fails both panels. On the 2025 panel it gains
AMBA lock13 (0.465 s), AMBA lock16 (9.005 s), and theta14 (16.733 s), but
loses 13 answers. Twelve losses remain hard gate failures; the thirteenth is
the near-cap workstation case. On the 2026 panel it gains AMBA lock13
(0.503 s) and loses eight unreal answers. Several losses are cases main
answers in well under a second. These are portfolio regressions with no
opposite verdicts, not evidence for default promotion. Four and three raw
UNKNOWN outcomes respectively remain inconclusive and receive the full PAR-2
penalty. Eager TAA answers exactly the same cases on both panels. Its PAR-2
differs from lazy by 1.033 seconds on the 2025 panel and 0.315 seconds on the
2026 panel, providing no useful evidence for selecting lazy over eager.

The wrapper's original longer diagnostic preferred an old LTL source when one
existed, even for a primary native TLSF panel. This affected the 2025 workstation
adjudications. Commit `29d55497` adds an explicit native TLSF route; 48 focused
tests pass, including both-source lookup, rejection of an absent native source,
and the complete campaign's diagnostic command. The default LTL-first lookup
remains available for G1. All eight comparisons have been adjudicated again
through native TLSF, with unchanged primary CSV hashes and preserved original
reports. Both sparse mixtures pass both panels; both TAA mixtures fail both.
The native workstation diagnostics complete for guarded automaton in 15.277 s,
guarded formula in 15.656 s, lazy TAA in 16.763 s, and eager TAA in 17.484 s.
These separate longer-cap observations do not upgrade primary coverage.
Checker revision/hash, commands, source-map hashes, and primary CSV hashes are
recorded in `g3-instance-scopes/native-adjudication-protocol.json` under the
run directory. Solver code and the four frozen candidate binaries are unchanged.

A bounded follow-up is frozen to run afterward:
replace B-real by the existing TAA-to-backward fallback, retain F-real and
both old unreal workers, and cap TAA search at 1,000 rank nodes. The cap is
chosen from the two captured positive proofs (195 and 15 nodes), before
timing this mixture. Six targeted cases include both B-only losses, both
TAA gains, `01`, and `infinite-race-u4`. A matched eager control distinguishes
construction choice from laziness. This is a focused experiment with explicit
flags and environment, not a baked default or a passed admission gate.

The six-case trial is complete: main answers four, and both bounded fallback
recipes answer all six with zero losses or conflicting verdicts. The two gains
are the AMBA real cases. Both recipes also retain the backward-only cancel5
and collector9 answers while preserving the original formula-unreal worker's
infinite-race-u4 answer.

| Native TLSF case | Main result / seconds | Lazy TAA fallback | Eager TAA fallback |
|---|---|---|---|
| arbiter with cancel5 | R / 1.106 | R / 1.416 | R / 1.502 |
| collector v1 9 | R / 2.561 | R / 5.847 | R / 6.084 |
| AMBA encode20 | timeout | R / 8.596 | R / 8.612 |
| AMBA lock14 | timeout | R / 1.343 | R / 1.258 |
| 01 | R / 0.034 | R / 0.023 | R / 0.022 |
| infinite-race-u4 | U / 0.590 | U / 0.567 | U / 0.546 |

Collector's extra time is a real cost of this recipe. The fresh comparison on
the 51 exposed selection cases is complete: both fallback recipes answer 23,
versus main's 21, preserving every main answer. Both also answer the two
separate AMBA lock13/lock16 probes that main misses. Their 51-case PAR-2 scores
are 1,001.271 seconds (lazy) and 1,004.480 (eager), versus 1,055.615 for main.
Eight separate diagnostic invocations confirm that the isolated budgeted TAA
attempts return UNKNOWN on cancel5 and collector9, while explicit backward
fallback returns REALIZABLE and logs the fallback for both providers.
Both provider variants keep the same 1,000-node cap chosen before the six-case trial. The
protocol, explicit arm list, budget environment, hashes, and primary results
are preserved in `budgeted-taa-slot` and `budgeted-taa-cohort` under the run
directory. These measurements support investigating the fallback composition;
they do not establish a lazy-generation advantage.

Commit `b2d09a42` exposes the candidate-mode default and a TAA-specific node
cap through the existing configuration registry, with unchanged global defaults
of candidate-only and 200,000 nodes. The two new fallback presets select
fallback and 1,000 TAA nodes; they remain experimental. Keeping the TAA cap
separate matters for a combined portfolio: the captured sparse GF-G6 proof
uses 2,283 nodes at K=8 before its smaller winning proof at K=11, and g39 uses
2,616 nodes at K=11 before winning at K=14. A global 1,000-node cap would
abort those useful frozen-search attempts. The configuration suite passes 73
tests; compiled CLI checks cover defaults, overrides, and provider separation.
Both fallback builds and a gates-disabled compatibility build pass all 43
compiled unit/version checks each. Their source is frozen at `efef6080`.

#### Combined bounded portfolio selection

Four additional actual races combine F-real and bounded TAA-real/backward
fallback with one sparse guarded unreal worker and the other original forward
unreal worker. The 1,000-node TAA budget and fallback mode are compiled
defaults; only `--arms` changes between treatments. No budget environment
override is used. The frozen search therefore retains 200,000 nodes.
The same 51 exposed selection cases and two separate AMBA panel probes are
used. The fresh 53-row main reference from `budgeted-taa-cohort` is reused
with exact file, binary, and protocol hashes. Its compiled-out Spot paths are
unaffected by that earlier study's global Spot budget environment.

| Sparse unreal transform | TAA provider | Answers / 51 | Gains / losses vs main | PAR-2 on 51 (s) | AMBA probes / 2 |
|---|---|---:|---:|---:|---:|
| automaton | lazy | 26 | 5 / 0 | 918.954 | 2 |
| automaton | eager | 26 | 5 / 0 | 920.666 | 2 |
| formula | lazy | 27 | 6 / 0 | 904.730 | 2 |
| formula | eager | 28 | 7 / 0 | 890.165 | 2 |

There are zero opposite verdicts. Both formula mixtures retain every answer
from both automaton mixtures. The formula mixtures additionally solve GF-G6
(16.243 s lazy, 16.747 s eager); eager also solves GF-G7 at 16.388 s while
lazy times out. Those runs are close to the cap and do not establish a stable
eager advantage. The four additional real answers across selection and probes
are encode20 and lock13/14/16. The mixed collector9 runs take about 5.58 seconds,
so the cost of delayed backward fallback remains visible.

The two formula mixtures advance as matched controls under named presets
`otf_mix_formula_lazy` and `otf_mix_formula_eager`. The automaton mixtures
add no unique answer on these selection inputs. The two existing sparse-only
presets also advance: they avoid TAA's cost and retain their complementary
panel gains (notably C2 versus GF-G6). All previous recipes and measurements
are preserved. Selection does not promote a shipping default. New actual-default
binaries must pass the remaining gates before the full comparison; the other
three shipping configurations are included again in that final measurement.

#### Final mixed-configuration gates

The actual-default mixture binaries are frozen at `5d16f889`. Both pass all
43 unit/version checks and all 40 frozen regression cases, with zero verdict
changes or coverage losses. New native-TLSF panel runs use the same preserved
main references as the sparse configurations; per-file and binary hashes are
recorded in `g3-final/provenance.json` under the run directory. Reused sparse
results point to their original files rather than replacing their provenance.

| Native panel | TAA mixture | Main answers | Candidate answers | Primary gains / losses | Candidate PAR-2 (s) | Gate |
|---|---|---:|---:|---:|---:|---|
| SYNTCOMP25 | lazy | 121 | 124 | 7 / 4 | 2070.522 | FAIL, 3 hard losses |
| SYNTCOMP25 | eager | 121 | 124 | 7 / 4 | 2074.825 | FAIL, 3 hard losses |
| SYNTCOMP26 | lazy | 139 | 139 | 2 / 2 | 1462.894 | FAIL, 2 hard losses |
| SYNTCOMP26 | eager | 139 | 140 | 3 / 2 | 1446.352 | FAIL, 2 hard losses |

Both providers lose the same fast real cases: AMBA decomposed arbiter6,
chain-simple-70-real, and collector v2 13 in 2025; AMBA decomposed arbiter5
and collector v2 12 in 2026. Main answers these in 0.131–1.995 seconds.
The fourth 2025 primary loss is workstation3, which the longer native
diagnostics answer with both mixtures. It remains a timeout in the primary
17-second score. Eager's extra 2026 answer is that same variable workstation
case at 16.892 seconds, not a new construction-specific gain.

There are zero opposite verdicts. On 2025 both mixtures retain the sparse
formula worker's five gains and add AMBA lock13/16, but sacrifice three fast
backward-real answers. On 2026 both add AMBA lock13 and robot charging6 while
losing arbiter5 and collector v2 12. The rank-node cap does not ensure that
TAA returns control to the backward solver before the wall-clock deadline.
These losses block promotion of either TAA mixture, regardless of the net
answer count or PAR-2 improvement. The remaining correctness and full-corpus
runs distinguish sparse default candidates from TAA research controls; the
panel coverage requirement is not waived for a default recommendation.

Eighteen separate native diagnostic runs attribute the three 2025 hard real
losses. Isolated mainline backward-real answers arbiter6, chain-simple-70-real,
and collector v2 13 in 0.949, 0.850, and 1.309 seconds respectively. Isolated
forward-real returns UNKNOWN on each. Both TAA providers time out at 17 seconds
on all three cases in both candidate-only and fallback modes; no backward
fallback is logged. The compact results are in
[`spot-otf-fallback-gate-losses.tsv`](spot-otf-fallback-gate-losses.tsv), with
raw commands, captures, and source hashes under `bounded-fallback-gate-losses`.

For chain-simple-70-real the last persisted milestone is TAA factory entry.
For arbiter6 and collector13, construction finishes and the last milestones
are lazy search or eager enumeration. These are censored phase records, not
final row counts or proof that a timed-out worker requested zero rows. They
show why limiting reached rank nodes cannot guarantee time for the old solver:
construction, enumeration, and individual row/query operations are not bounded
by that count. The sprint retains the specified four-worker limit and does not
start a new timeout/fallback architecture to rescue these configurations.

#### Labelled gates and the closing comparison

All four selected configurations completed the existing 624-case labelled
suite at its 30-second limit, sequentially with 8 GiB and zero swap. The
repository permits timeouts here, but requires zero failures.

| Configuration | OK | Timeout | UNKNOWN failures | G4 |
|---|---:|---:|---:|---|
| sparse automaton | 575 | 49 | 0 | PASS |
| sparse formula | 575 | 49 | 0 | PASS |
| formula + lazy TAA fallback | 574 | 47 | 3 | FAIL |
| formula + eager TAA fallback | 573 | 48 | 3 | FAIL |

The three UNKNOWN failures are identical for both TAA mixtures:
`ltl2dba_R_10`, `ltl2dba_R_12`, and `round_robin_arbiter_unreal2_5`.
Matched main invocations, using the exact generated harness inputs and flags,
time out at 30 seconds on all three. There is no false-positive or false-negative
marker. The failed TAA gates remain recorded rather than being relabelled as
passes because main also returns no answer. Raw results and matched commands
are in `g4-selected` under the run directory.

Three alternating rounds on the original LTL regression inputs are also
complete: all four new configurations answer all three cases in every round.
Main answers load-balancer25 and infinite-race-u4 in all three rounds, but
times out on Morning in all three. The sparse automaton mixture answers
Morning in 4.181–4.362 seconds and sparse formula in 3.394–3.505 seconds.
No old main answer is lost. These 45 invocations preserve the original G1
input route and remain separate from the native full-corpus experiment.

`closing-admission.json` freezes inclusion and eligibility. The two sparse
configurations have passed the required candidate gates. The TAA mixtures
enter the full measurement as research controls, with both their panel and
labelled failures recorded and default promotion excluded. All four mainline
shipping binaries are included with unchanged options. Every configuration
uses its compiled defaults, no arm or budget override, a single 17-second cap,
8 GiB, zero swap, and resource accounting on the same 1,524 native TLSF inputs.
Each completed configuration's journal accounting is saved before the next
campaign, with source, raw-result, journal, and binary hashes.

---

## Post-PR-125 coverage sprint

<sub>Was `benchmarking/POST-PR125-COVERAGE-SPRINT.md`. Closed. Six ranked work packages, each with a LAND / STOP / KEEP-TOOLING decision.</sub>

Six ranked work packages against the 434 instances of the official SYNTCOMP 2026
LTL-realizability selection that PR #125's portfolio cannot decide at 17 s.

### Decision

| package | correct? | target gains | full-2026 gains | regressions | memory effect | decision |
|---|---:|---:|---:|---:|---:|---|
| P1 forced contradiction | yes | +4 on G4, 22 target instances | **+22** (1,090 -> 1,112) | none | none | **LAND** |
| P2 semantic dominance D1 | yes | 512 -> 18 actions, 0 instances | none | G2s +0.39% cycles | none | **KEEP RESEARCH TOOLING** |
| P2 semantic dominance D2 | — | — | — | — | — | **not attempted (D1 did not pay)** |
| P3 K schedule | yes | 34 -> 7 attempts, 0 instances | none | +3-4% slower | none | **STOP** |
| P4 OTFUR lazy | yes | 3 targets -27% to -56% | pending G26 | none | fewer successors | **LAND (O1)** |
| P4 OTFUR memory | yes | 2 targets -16% to -29% | pending G26 | none | RSS criterion failed | **LAND (O2, on speed)** |
| P4 OTFUR covering | yes | 1 of 2 targets, mixed | none | none | none | **AGGRESSIVE PRESET (off by default)** |
| P5 four-slot portfolio | yes | +62 full corpus, -28.6% time | **976 -> 1038** | 18 realizable | none | **forward for unreal, split for real** |
| P6 wide6 portfolio | | | | | | *not started* |

### Frozen baseline

```
PR #125    head 521b9400569489be2393b9a114f9e22940463e12
           base 2178290bac7d0eb77350593c69a5fa4e723f0cbf
submodules posets              139e14336b7a1f0bc064022e587ea4e1b9a81427
           tlsf-tools          b42d5ef4a680252e04820ac7f073f5d786a43f7c
           syntcomp-benchmarks 4105caf1f1e5fd3b76657879bfce8021d130cbde  (v2026)
```

Reference configurations, never silently changed:

```
B = best_decomp_rank_bucketed_semantic_mona
S = best_decomp_rank_bucketed_semantic_mona_local
F = best_decomp_rank_bucketed_semantic_mona_forward
```

Coverage established by PR #125 over `tests/suites/benchmarks/syntcomp26/all.list`
(1,524 logical instances, 17 s cap):

| configuration | decided |
|---|---:|
| B | 1056 |
| S | 1065 |
| F | 1053 |
| B ∪ S | 1065 |
| B ∪ S ∪ F | **1090** |

The TLSF corpus is materialized once and reused by every gate:

```sh
python3 benchmarking/syntcomp-corpus.py init
python3 benchmarking/syntcomp-corpus.py materialize --out /tmp/acacia-syntcomp26-tlsf
export ACACIA_TLSF_CORPUS=/tmp/acacia-syntcomp26-tlsf
```

Verified for this sprint: **1,586 files**, the required count.

### P1 — forced-output contradiction checker

#### Why this package is worth doing

The two target families hold 52 of the 1,524 instances. Their measured status at a
17 s cap, from `benchmarking/_coverage26/{B,S,F26}-runs.tsv`:

| configuration | UNREALIZABLE | TIMEOUT |
|---|---:|---:|
| B | 2 | 22 |
| S | 1 | 22 |
| F | 30 | 22 |
| union | 30 | **22** |

The same 22 instances defeat all three solvers, and 20 of them still time out at
60 s. They are `full_arbiter_unreal1` at the larger delays (`n=2, u=18..26`;
`n=3, u=8..15`; `n=4, u=5..8`; `n=6, u=8`) and `full_arbiter_unreal2` at `n=5,6,7`.
Every one is adjudicated `unrealizable`.

P1 decides at the formula level, before translation, so its cost does not grow with
the delay `u` that defeats the game solvers. Its ceiling on this sprint's primary
metric is therefore **+22, taking the union from 1,090 to 1,112** — the largest
single gain available in the six packages, and the cheapest to obtain.

#### Twelve of those 22 have no known answer

The SYNTCOMP `//STATUS` annotations across the 52 family members are 40
`unrealizable` and 22 `unknown`, and the split is not incidental: of the 22
instances that defeat B, S and F, **10 are annotated `unrealizable` and 12 are
annotated `unknown`**. No tool in the competition ever decided those twelve.

`run-syntcomp26-coverage.py:190` maps an `unknown` annotation to no expectation, so
a verdict there cannot register as a conflict — but it equally means there is no
oracle to check it against. That raises rather than lowers the soundness burden, and
it is the reason the restricted fragment of §3.3 must not be widened to make more
instances match. The validation surface for P1 is:

| cohort | count | check available |
|---|---:|---|
| hard, annotated `unrealizable` | 10 | direct comparison against the annotation |
| already decided by F at 17 s | 95 | independent solver agreement |
| hard, annotated `unknown` | 12 | none — rests on the fragment being sound |

The 95 come from `full_arbiter_unreal` (30) and `simple_arbiter_unreal` (65).

#### A family the handoff does not mention

`simple_arbiter_unreal1` (53 instances) and `simple_arbiter_unreal2` (12) are also
assumption-free, and their `MAIN` block is the `full_arbiter_unreal` template minus
the "no spurious grants" invariants — mutual exclusion plus the same two-grant
clause. They match the same fragment.

They add **no coverage**: F already decides all 65 at 17 s. Their value is as the
largest independent-agreement cohort available for the correctness gate, and as a
speed result, since P1 answers them before translation.

More broadly, 796 of the 1,586 materialized files carry no `ASSUMPTIONS`, `ASSUME`
or `REQUIRE` section. That is the population the sound fragment can even consider;
the remaining 790 must be declined on sight.

#### Prediction for the scan, recorded before the matcher exists

Of the 796 assumption-free files, 208 carry a `mutual_exclusion` invariant, but only
**127** also carry the two-grant clause `g[i] && g[j]` that contradicts it:

| family | files | contradiction clause |
|---|---:|---|
| `full_arbiter_unreal1` | 55 | yes |
| `simple_arbiter_unreal1` | 53 | yes |
| `simple_arbiter_unreal2` | 12 | yes |
| `full_arbiter_unreal2` | 7 | yes |
| `rw_arbiter`, `arbiter_with_cancel`, `arbiter_with_buffer`, `arbiter_on_inpchange`, `arbiter`, `abcg_arbiter`, `robot_grid`, `simple_arbiter_with_hints`, `generalized_buffer` | 81 | **no** |

So the matcher should report **127 matches over the 1,586 materialized files and
nothing else**. Restricted to the TLSF files the graded 1,524-instance selection
actually uses, the prediction is **117**: 46 `full_arbiter_unreal1`, 6
`full_arbiter_unreal2`, 53 `simple_arbiter_unreal1`, 12 `simple_arbiter_unreal2`.
Of those 117, **95 are already decided** by an existing configuration and are
therefore agreement checks, and **22 are new coverage**.

Those 81 non-matching arbiters are the sharpest false-match test available: they
carry the same global mutual-exclusion invariant over the same output partition and
differ only in having no clause that contradicts it. A matcher that fires on them
has a bug in the `chi & beta` unsatisfiability test rather than in conjunct
extraction. A count above 127 is a false match to investigate; a count below 127 is
a missing normalization.

#### What the effective formula actually looks like

`full_arbiter_unreal1_pb_2_2_pe_` as the native TLSF frontend emits it, **before**
`spot::realizability_simplifier` (367 bytes). The post-simplifier text the checker
actually receives is shorter, and is recorded below once the fixture dump exists:

```
G ( (g_0 && G !X r_0 -> F !g_0)
 && (g_0 && X (!X r_0 && !g_0) -> X (X r_0 R !g_0))
 && ((g_1 && G !X r_1 -> F !g_1) && (g_1 && X (!X r_1 && !g_1) -> X (X r_1 R !g_1)))
 && (!g_0 && true || true && !g_1)
 && ((X r_0 && X X r_1 -> X X (g_0 && g_1)) && true) )
&& (X r_0 R !g_0 && G (X r_0 -> F g_0) && (X r_1 R !g_1 && G (X r_1 -> F g_1)))
```

Five properties of this text decide the implementation:

1. **The top level is a conjunction, with no outer implication.** These families
   carry no `ASSUMPTIONS` section, so the sound fragment applies. This is exactly why
   `round_robin_arbiter_unreal1` and `load_balancer_unreal1` are excluded: both *do*
   have an `ASSUMPTIONS` section, so their clauses sit under `A -> G` and are not
   consequences of the whole formula. They are negative fixtures, not targets.
2. **The entire `INVARIANTS` block arrives as one `G(...)` over a conjunction.** The
   rewrite `G(A & B) → G A, G B` is therefore load-bearing, not a convenience:
   without it nothing matches.
3. **The mutual-exclusion invariant is an OR**, `(!g_0 && true || true && !g_1)`.
   The "never descend under arbitrary OR" restriction governs *conjunct extraction*;
   it does not forbid an invariant whose `chi` is itself a disjunction. `chi` is
   accepted here because it is temporal-operator-free with support inside the output
   partition, and it goes to a BDD as-is.
4. **Inputs are shifted by one `X`** — `X r_0`, not `r_0` — because the family is
   `SEMANTICS: Moore, TARGET: Mealy`. The trigger is `X r_0 && X X r_1`, so the
   bounded-input-pattern translator over `(input AP, time offset)` pairs is required
   for the very first target, not just for exotic cases.
5. **Trivial `true` conjuncts survive simplification** and must be tolerated.

The match is then `chi = !g_0 || !g_1`, `beta = g_0 && g_1`, `chi & beta = bddfalse`,
trigger satisfiable, `d = 2` — `UNREALIZABLE`.

`full_arbiter_unreal2` is the eventual variant: the same shape with `F (g_0 && g_1)`
in place of `X X (g_0 && g_1)`. Both response kinds of §3.2 are needed.

#### A crash the unit test was hiding

The first working implementation passed all 28 unit tests and would have segfaulted
on every instance in production.

`bounded_input_pattern` and the output translator call `bdd_extvarnum` and
`bdd_ithvar` directly, and those require BuDDy to have been initialized. At the
call site the checker is designed for — `try_syntactic_bypass` — it has not been:
`solver_invoker.cc` does not create `spot::make_bdd_dict ()` until well after the
bypass returns. A standalone program that parses a formula and calls `try_direct`
with no dictionary in existence dies with SIGSEGV; adding one `make_bdd_dict ()`
before the call makes the same program print `unrealizable=1` and exit 0.

The unit test did not catch it because its `main` opened with
`auto dictionary = spot::make_bdd_dict ();`, initializing BuDDy process-wide and
reproducing a precondition the solver never establishes. The test was green because
it was testing a situation that does not occur.

The fix makes both public entry points hold a `spot::bdd_dict_ptr` for the duration
of the call, and **removes** the dictionary from the test, so the test now exercises
the same uninitialized state as the real call site and fails if the guard is ever
removed.

This is worth recording because it is a general hazard for this sprint rather than a
one-off: P1, and later any BDD work in P2's dominance helper, runs earlier in the
pipeline than the solver's own BDD setup. A green unit suite is not evidence that
code works where it is actually called.

#### The prediction was wrong, and the reason is instructive

The 117 estimate was derived from the TLSF sources. It does not survive contact with
the effective formulas, because `spot::realizability_simplifier` runs first and
changes the *shape* of half the match set.

`simple_arbiter_unreal1_pb_2_1_pe_` reduces to

```
G((!g_0 | !g_1) & X(g_0 & g_1)) & GFg_0 & GFg_1
```

with **no implication anywhere**. Its request APs are single-polarity, so the
simplifier substituted them true and collapsed `(r_0 & X r_1) -> X(g_0 & g_1)` into
an unconditional `X(g_0 & g_1)`. `simple_arbiter_unreal2_pb_4_pe_` collapses the
same way into `G(chi & F(g_0 & g_1) & F(g_0 & g_2) & ...)`. A matcher that requires
`G(alpha -> ...)` declines both, and the n=16 member declines in 2.6 ms.

The families that keep their implications are exactly the ones that need them:
`full_arbiter_unreal1_pb_3_15_pe_` still carries 12, because its requests appear at
both polarities in the no-spurious-grant clauses, so the simplifier cannot force
them. **All 22 coverage-gaining instances are in that group and match today.** What
the collapse costs is the 65 `simple_arbiter_unreal` instances, which F already
decides — no coverage, but the bulk of the independent-agreement cohort.

The extension is to treat a missing implication as a vacuously true trigger, which
is sound in both forms:

- `G(chi) & G(X^d beta)` forces `beta` at every position from `d` on, and `chi` at
  every position, so `chi & beta` unsatisfiable is a contradiction;
- `G(chi) & G(F beta)` forces `beta` at some position, where `chi` also holds.

It cannot create a false match on the 81 innocent arbiters: with an unconditional
obligation, `beta` is drawn from the same conjuncts that make up `chi_all`, so
`chi_all & beta` reduces to `chi_all`, which is satisfiable unless the specification
is itself contradictory — and a contradictory specification is unrealizable anyway.

The order of work is therefore: scan with the implication-only matcher and record
what it really matches, extend, then scan again and account for every new match.

#### Probing the soundness boundary

The handoff's restriction list (§3.3) says what not to descend under. Whether the
implementation honours it was checked directly, with cases chosen so that a matcher
one level too permissive would answer `UNREALIZABLE` on each. All ten behave:

| case | required | result |
|---|---|---|
| response under `G`, trigger input-only | match | match |
| `beta` propositionally false, no invariant at all | match | match |
| response only under a disjunction | decline | decline |
| response under an inner implication | decline | decline |
| invariant only under a disjunction | decline | decline |
| response negated | decline | decline |
| invariant under `F G` rather than `G` | decline | decline |
| trigger mentions an output | decline | decline |
| realizable lookalike, `chi & beta` satisfiable | decline | decline |
| response inside `U` | decline | decline |

Two are worth singling out. The `beta`-false case matches with **no invariant
present**: `G(alpha -> false)` is `G(!alpha)`, and a forceable `alpha` refutes it, so
the empty-invariant path is sound rather than an oversight. And the realizable
lookalike, `G(!g0|!g1) & G(r0 -> XX g0)`, is the shape the checker must never fire
on — a single grant violates nothing — which is what the `chi & beta` test rejects.

These are now part of the unit suite rather than a one-off script.

#### Scan 1: the implication-only matcher, over all 1,524

`benchmarking/forced-contradiction-scan.py` against
`acacia-forced-contradiction-scan`, one process per file, about a minute:

```
scanned 1524 files
  DECLINE  1471
  MATCH      53
matches by family:
  full_arbiter_unreal1         46
  full_arbiter_unreal2          6
  jarvis_gideon_a02758ea        1
```

No crashes, errors, timeouts or empty verdicts. Both target families match in full,
46 and 6, exactly the counts predicted from the sources. The 65
`simple_arbiter_unreal` instances decline, for the simplifier-collapse reason above.

**No match contradicts an annotation.** The 53 are 41 annotated `unrealizable` and
12 annotated `unknown`; none is annotated `realizable`. The 12 are precisely the
hard `full_arbiter_unreal` instances no competition tool decided, and among them
`full_arbiter_unreal1_pb_3_15_pe_` is answered with `MATCH fixed_delay 15`, the
delay agreeing with its own `u = 15` parameter. That instance times out for B and S
at 1 s, 5 s, 17 s **and** 60 s, and for F at 17 s, which is the only cap F was run
at. The checker settles it before translation.

#### The unpredicted match was real, and found a second theorem

`jarvis_gideon_a02758ea` was not in the prediction. It is annotated `unrealizable`,
so the answer is right, but the *reason* reported was not. Reconstructing the
invariant half of the checker over that instance shows it collects **26** output-only
global invariants whose conjunction is already `bddfalse` — the specification demands
`G u0window0f1dopen1b` and `G u0window0f1dclose1b` among others. With `chi_all`
unsatisfiable, the first response encountered matches for a reason unrelated to that
response, and the witness names it anyway.

The answer is sound: `G chi` with `chi` propositionally unsatisfiable makes the whole
specification unsatisfiable, hence unrealizable. But it is a *different* theorem from
the one the checker was written for, and it deserves to be reported as itself. It is
also strictly more general, since it does not need a response-shaped conjunct to be
present at all — the current code can only reach it by accident, when some response
happens to exist.

Both findings are folded in: contradictory invariants become their own witness kind,
checked before any response is considered, and an absent implication is treated as a
vacuously true trigger.

#### Scan 2: after both generalizations

```
scanned 1524 files
  DECLINE 1404
  MATCH    120
matches by family:
  full_arbiter_unreal1         46      simple_arbiter_unreal1       53
  full_arbiter_unreal2          6      simple_arbiter_unreal2       12
  jarvis_gideon_a02758ea        1      lilydemo01                    1
                                       lilydemo02                    1
witness kinds:  fixed_delay 101   eventual 18   contradictory_invariants 1
```

Nothing that matched in scan 1 stopped matching. Every newly matched instance is
accounted for: 65 are the `simple_arbiter_unreal` families recovered by the
vacuous-trigger reading, and the two `lilydemo` instances reduce to
`G(... & X(0))` — an obligation whose `beta` is propositionally false, caught by the
path the adversarial probe had already covered in the abstract. Both are annotated
`unrealizable`.

Over the full 1,586-file corpus rather than the graded selection, the same binary
matches **130**, distributed as 55 `full_arbiter_unreal1`, 7 `full_arbiter_unreal2`,
53 `simple_arbiter_unreal1`, 12 `simple_arbiter_unreal2`, and the three singletons.
That closes the prediction loop exactly: the four arbiter families come to
55 + 7 + 53 + 12 = **127**, the number derived from the TLSF sources before any code
existed, and the three extra matches are precisely the ones the two additional
theorem paths were added to catch. Of the 130, 106 are annotated `unrealizable`,
24 `unknown`, and none `realizable`.

#### Correctness of the 120

| check | result |
|---|---|
| matches annotated `realizable` | **0** |
| matches annotated `unrealizable` | 106 |
| matches annotated `unknown` | 14 |
| matches carrying a B/S/F verdict at any cap | 100 |
| of those, agreeing `UNREALIZABLE` | **100** |
| of those, conflicting | **0** |

A hundred independent solver agreements and no contradiction anywhere in the
official selection is the strongest evidence available short of the gates
themselves.

#### Coverage

| | count |
|---|---:|
| matched by the checker | 120 |
| already decided by B, S or F at 17 s | 98 |
| **new coverage** | **22** |

The 22 are 19 `full_arbiter_unreal1` and 3 `full_arbiter_unreal2` — exactly the set
identified before the code existed. They take the portfolio union from **1,090 to
1,112 of 1,524**.

| instance | witness | SYNTCOMP status |
|---|---|---|
| `full_arbiter_unreal1_pb_2_18_pe_` | `fixed_delay 18` | unrealizable |
| `full_arbiter_unreal1_pb_2_20_pe_` | `fixed_delay 20` | unrealizable |
| `full_arbiter_unreal1_pb_2_21_pe_` | `fixed_delay 21` | unrealizable |
| `full_arbiter_unreal1_pb_2_22_pe_` | `fixed_delay 22` | unrealizable |
| `full_arbiter_unreal1_pb_2_24_pe_` | `fixed_delay 24` | unrealizable |
| `full_arbiter_unreal1_pb_2_26_pe_` | `fixed_delay 26` | unrealizable |
| `full_arbiter_unreal1_pb_3_8_pe_` | `fixed_delay 8` | unrealizable |
| `full_arbiter_unreal1_pb_3_9_pe_` | `fixed_delay 9` | unrealizable |
| `full_arbiter_unreal1_pb_3_10_pe_` | `fixed_delay 10` | unrealizable |
| `full_arbiter_unreal1_pb_3_11_pe_` | `fixed_delay 11` | **unknown** |
| `full_arbiter_unreal1_pb_3_12_pe_` | `fixed_delay 12` | **unknown** |
| `full_arbiter_unreal1_pb_3_13_pe_` | `fixed_delay 13` | **unknown** |
| `full_arbiter_unreal1_pb_3_14_pe_` | `fixed_delay 14` | **unknown** |
| `full_arbiter_unreal1_pb_3_15_pe_` | `fixed_delay 15` | **unknown** |
| `full_arbiter_unreal1_pb_4_5_pe_` | `fixed_delay 5` | unrealizable |
| `full_arbiter_unreal1_pb_4_6_pe_` | `fixed_delay 6` | **unknown** |
| `full_arbiter_unreal1_pb_4_7_pe_` | `fixed_delay 7` | **unknown** |
| `full_arbiter_unreal1_pb_4_8_pe_` | `fixed_delay 8` | **unknown** |
| `full_arbiter_unreal1_pb_6_8_pe_` | `fixed_delay 8` | **unknown** |
| `full_arbiter_unreal2_pb_5_pe_` | `eventual 0` | **unknown** |
| `full_arbiter_unreal2_pb_6_pe_` | `eventual 0` | **unknown** |
| `full_arbiter_unreal2_pb_7_pe_` | `eventual 0` | **unknown** |

The reported delay equals the instance's own `u` generator parameter in every one of
the nineteen fixed-delay rows, and the three `full_arbiter_unreal2` rows report
`eventual`, which is the form that family is generated in. Nothing in the checker
reads the filename, so this is an independent consistency check on the witness
rather than a restatement of it.

Twelve of the twenty-two are annotated `unknown`, meaning the sprint would settle
instances the competition left open. The other 98 matches gain nothing but are answered before
translation rather than by a game search, and they are what makes the correctness
argument above possible.

#### Gates

Every gate is run twice, on two builds differing in exactly one preprocessor flag —
`ACACIA_FORCED_OUTPUT_CONTRADICTION` 0 against 1, both from the
`best_decomp_rank_bucketed_semantic_mona` preset with diagnostics off. Without the
baseline side, a failing gate cannot be attributed to P1 rather than to something
already failing at `521b9400`.

| gate | candidate | baseline | attributable to P1 |
|---|---|---|---|
| G0 unit + posets | `Fail: 0`, 28/28 | same | — |
| G1 frozen 40 | `GATE FAIL`, 35/40 | `GATE FAIL`, 35/40, **identical set** | **none** |
| G4 correctness corpus | `Fail: 0`, 563 ok, 61 timeout | `Fail: 0`, 559 ok, 65 timeout | **+4, none lost** |
| G2s per-target cycles | `GATE PASS`, geomean 0.994 | paired against candidate | **no measurable cost** |
| G3 syntcomp25 + 26 panels | `GATE PASS` ×2, 80/180 and 118/180 | same counts | **none** |
| G26 full 1,524 | 998 decided | 976 decided | **+22, none lost** |

##### G26: the coverage claim, measured

Both sides run with staged caps 1, 5, 17 under `MemoryMax=8G` / `MemorySwapMax=0`,
sequentially, from the same two builds:

| | decided of 1,524 |
|---|---:|
| baseline, flag off | 976 |
| candidate, flag on | **998** |
| delta | **+22** |

The gained set is *exactly* the twenty-two named in the table above, before the
matcher existed — nineteen `full_arbiter_unreal1` and three `full_arbiter_unreal2`,
every one `UNREALIZABLE`. **Every one is decided at the 1-second cap**, against a
baseline that cannot decide them at 17 s and, for most, not at 60 s either.

Nothing was lost, and the two builds disagree on **no** instance they both decide.

Those twenty-two are absent from the recorded B, S and F runs alike, so the
portfolio union moves **1,090 → 1,112 of 1,524**.

##### Two conflicts, neither P1's, both now settled by construction

The candidate campaign recorded two verdict conflicts: `lilydemo15` and
`lilydemo16` are annotated `unrealizable` and come back `REALIZABLE`.

They are not P1's. The checker declines on both; the baseline binary returns
`REALIZABLE` identically; and the checker cannot emit `REALIZABLE` at all, since it
either reports `UNREALIZABLE` or declines.

They are not Acacia's either. `ltlsynt` agrees, and it synthesises a controller for
each — 6 and 23 states — whose language has **empty intersection** with an automaton
for the negated specification. That is a witness rather than a second opinion: the
strategy satisfies the specification on every run. Both formulas are arbiters with
mutual exclusion and no spurious grants, two clients and three, realised by
alternating grants.

Both are now in `syntcomp26-status-exceptions.tsv` with that evidence, beside the
pre-existing `lilydemo04_modified` correction. The re-run baseline campaign, which
used the corrected table, records **zero** conflicts.

#### Decision

**LAND.**

Every criterion of §3.12 is met. Unit tests pass. Every matched instance is
independently confirmed: 100 of the 120 matches carry a B, S or F verdict and all
100 agree, none conflicts, and none is annotated `realizable`. There are zero
verdict conflicts attributable to P1 over the 1,524. Twenty-two instances above the
17-second cap drop below one second — `full_arbiter_unreal1_pb_3_15_pe_`, a 60-second
timeout for both backward configurations, returns in 7.9 ms. And no gate regresses:
G1 fails identically on both sides, G4 gains four and loses none, G2s finds no
measurable cost, G3 is unchanged.

Twelve of the twenty-two are annotated `unknown` in SYNTCOMP, so this package
settles instances the competition left open.

The flag stays **off by default**. Flipping it is a separate decision with its own
gate run, and B, S and F remain untouched for the packages that follow.

##### G4 is where P1 earns its place

| transition | count |
|---|---:|
| OK → OK | 510 |
| **TIMEOUT → OK** | **4** |
| TIMEOUT → TIMEOUT | 59 |
| OK → TIMEOUT | **0** |
| `FAIL`, either side | **0** |

The four gained are `full_arbiter_unreal1_3_8`, `_3_10`, `_3_12` and
`full_arbiter_unreal2_5` — the target family, and the same hard points the corpus
scan identified. Strictly more answers, never a different one, which is the only
shape a sound decision procedure may produce.

##### G2s passes, and measures less than its headline claims

```
geometric mean ratio=0.99387  improvement=-0.61%
decision=proxy-pass-to-G3: no target exceeds the regression ceiling
          and best target improves 6.23%
GATE PASS
```

Two facts make the summary line unusable as a performance claim. **P1 fires on none
of the ten panel targets** — every one has zero matches in the corpus scan — so the
checker runs, declines, and gets out of the way, and the expected effect is nil.
And **all ten time out on both sides in all three repetitions**, so cycles here
measure how fast a binary burns the clock before the cap, not how fast it solves.
The `+6.23%` on `evasion0` that the script calls the best improvement is a ratio of
`1.06639`: the candidate spent *more* cycles before timing out. On a both-sides
timeout that is not a signal in either direction.

What G2s does establish is narrower and still worth having: a pre-check that runs on
every instance and decides 130 of 1,586 costs nothing measurable on the other 1,456.

This is the trap `FORWARD-COVERAGE-SPRINT.md` documented when it recorded a 10.9x
G2s geomean for the forward solver and then explained why that was not a win. Same
gate, same failure mode, opposite direction.

##### G3 passes with no change, for a checkable reason

Both panels are unmoved: syntcomp25 stays at 80/180 and syntcomp26 at 118/180. The
panels hold four `full_arbiter_unreal` instances between them — `pb_2_16`,
`pb_2_14`, `pb_3_2` and `unreal2_pb_4` — and **none is among P1's twenty-two**.
They are the easy members the baseline already solves. The stratified panels do not
sample the hard tail, so no change is the correct outcome rather than a
disappointment, and the coverage claim rests on G26 instead.

##### G1 fails, and fails identically on both sides

`regression-gate.sh` compares the build under test against a hardcoded reference,
`build_best_decomp_mona` — the *shipping* preset — not against the build handed to
it. So both runs were measured against the same third binary:

| build | solved | timeout | PAR-2 |
|---|---:|---:|---:|
| reference, shipping `best_decomp_mona` | 40 | 0 | 101.867 s |
| baseline, B preset, flag off | 35 | 5 | 217.762 s |
| candidate, B preset, flag on | 35 | 5 | 195.269 s |

The five lost instances are the same five in both, character for character:
`Morning_f1477cc5`, `Morning_f2774e0b`, `load_balancer7`, `infinite-race-u4` and
`load_balancer_pb_7_pe_`. They belong to the B preset relative to the shipping
reference and are inherited from PR #125; P1 neither causes nor repairs any of them.

The PAR-2 difference, 195 s against 218 s, is **not** claimed as an improvement. It
is a 10 % swing on a forty-instance panel against a documented noise floor of about
21 s on a much larger one.

##### A near-miss worth recording

The first G1 run was invoked as `regression-gate.sh ... | tail -30`, which reported
`tail`'s exit status rather than the script's. The visible output ended in
`Ok: 22 / Fail: 0` and looked clean; the script had in fact printed `GATE FAIL` on
its first line and exited 1, because the build lacked `-Dacacia_tlsf_corpus_dir` and
had silently run 25 of its 40 sentinels. `ACACIA_TLSF_CORPUS` in the environment
serves the campaign scripts; the meson-driven gates need the path configured into
the build. Gates are run unpiped, and their own verdict line is what gets recorded.

| gate | result | nature of failure |
|---|---|---|
| G0 unit/posets | | |
| G1 frozen 40 | | |
| G2s per-target cycles | | |
| G3 syntcomp25 + syntcomp26 panels | | |
| G4 correctness corpus | | |
| G26-full | | |
| scan, 1,586 TLSF files | | |

#### Decision

*Pending.*


### P2 — global semantic-action dominance

#### Decision

**KEEP RESEARCH TOOLING.** The reduction is real, exactly as predicted, and buys
nothing.

#### What was built

`src/actioners/profile_dominance.hh` prunes actions that are redundant for the
controller. The direction reads backwards and is worth stating: backward `apply`
imposes `apply_out[q] = min (apply_out[q], max (-1, m[p] - increment))` per
endpoint, so more endpoints and larger increments give tighter constraints, a
pointwise smaller image and a smaller downset — and since `cpre_inplace` takes a
**union** over an input's actions, the action with *more and stronger* endpoints is
the one that contributes nothing.

Pruning runs after `actioners::standard` extracts from its
`std::set<input_and_actions, compare_actions>`, never before, so the input ordering
PR #125 measured is preserved and the reduction is the only variable.

#### Correctness

`tests/profile_dominance_test.cc` compares the controller predecessor before and
after pruning at **every** rank vector of the entire domain, for 240 generated
tables, using `research::apply_backward` — the documented transcription of the real
`apply`. Measured separately over 2,000 tables of the same shape, pruning fires on
**49%** of them and removes **29%** of all actions, so the differential check is
exercising real reduction rather than passing vacuously. A flipped comparison would
shrink the union on half the tables.

#### The reduction is exactly as predicted

```
prioritized_arbiter_pb_7_pe_
  profile_actions_before = 512      profile_actions_after = 18
  dominance_tests = 640             endpoint_visits = 5340
  declined = 0                      ms = 0.09
```

512 equality-distinct profiles collapsing to the 18 inclusion-minimal ones, at
0.09 ms with no budget exhaustion. The census was right.

#### And it buys nothing

| instance | baseline | candidate |
|---|---|---|
| `prioritized_arbiter_pb_5_pe_` | REALIZABLE 0.22 s | REALIZABLE 0.16 s |
| `prioritized_arbiter_pb_6_pe_` | REALIZABLE 21.33 s | REALIZABLE 20.98 s |
| `prioritized_arbiter_pb_7_pe_` | UNKNOWN 60 s | UNKNOWN 60 s |
| `prioritized_arbiter_pb_8_pe_` | UNKNOWN 60 s | UNKNOWN 60 s |
| `round_robin_arbiter_pb_4_pe_` | UNKNOWN 60 s | UNKNOWN 60 s |
| `workstation_resupply_pb_3_pe_` | UNKNOWN 60 s | UNKNOWN 60 s |

No verdict changes, nothing newly solved, best movement 1.6% against §4.12's 25%
criterion.

| gate | candidate | baseline | verdict |
|---|---|---|---|
| G0 unit | `Fail: 0`, 28/28 | — | pass |
| G1 frozen 40 | `GATE FAIL` 35/40, PAR-2 212.7 s | `GATE FAIL` 35/40, PAR-2 213.4 s | identical, not attributable |
| G2s per-target | geomean **1.00394**, +0.39% cycles | paired | **GATE FAIL** |

G2s is the decisive one: five of ten targets get *slower* — `arbiter_with_buffer6`
+5.45%, `round_robin_arbiter4` +2.53% — and the geometric mean is 0.39% **more**
cycles. On that panel the prune mostly finds nothing to remove, so the run pays the
construction cost without collecting the benefit.

#### Why it fails, which is the useful part

The census counted *profiles*, and 512 → 18 confirms that count exactly. What it
never established is that the action basis was the **bottleneck**. On the same
instance, 5,370 worker diagnostics report 20 actions before and 20 after: the
collapse touches one worker, and the search everywhere else is untouched. The
instance still exhausts 60 s.

So the cost lives somewhere the action count does not reach — most plausibly the
antichain width in the fixed point rather than the per-CPre action loop. That is
worth carrying into P4 and P5, both of which are partly premised on action-table
size being what matters.

**D2 is not attempted.** §4.9 permits pre-decode dominance only once D1 shows
material solver gains, and it must then beat D1 rather than the equality-only
baseline. D1 shows none.

The helper, its exact differential test and the six diagnostics counters stay in the
tree behind a default-off flag, because the action-profile census they produce is
needed by P5's arm census regardless.


### P3 — alternative K schedules

#### Decision

**STOP.** The mechanism works exactly as designed, gains nothing, and costs time.

#### The premise, and why it survived P2

`src/solver/k_schedule.hh` adds linear, geometric (`2k+1`), cheap-loss-adaptive and
direct-max schedules. The precondition was an algebra question the handoff makes P3
conditional on: is one direct lift by the total delta equal to the repeated lifts it
replaces? **It is** — `lift(s,d)` adds `d` to numeric coordinates and resets boolean
ones to zero, so the boolean half is idempotent and the numeric half accumulates.
`tests/k_schedule_test.cc` checks that over **84,832 exhaustive cases**, covering the
four the handoff names, plus the overflow guard against `VECTOR_ELT_T`.

The cohort looked ideal: 181 instances with `k_attempts >= 10`, and the stuck ones
show `k_attempts=34` with `max_f` of **1 or 2** — `arbiter_pb_6` burns 2,721 loops
over a frontier of one element. Linear walks 2→99 by 3 in 34 attempts, geometric in
7. A 4.9x reduction, and unlike P2 the reduction is *on the quantity being counted*.

#### The measurement

`chomp_pb_3_2_pe_` confirms the wiring: `k_attempts` 34 → **7**, `k_last_next=99`.

Over 22 instances spanning every cohort family, at a 17 s cap, four builds:

| | forward | backward |
|---|---|---|
| decided by linear | 10 | 8 |
| decided by geometric | 10 | 8 |
| gained / lost | 0 / 0 | 0 / 0 |
| verdict disagreements | 0 | 0 |
| wall on instances both decide | **+3.0%** | **+4.4%** |

#### Why, which the diagnostics settle

**All 181 high-K instances lose at every bound `K <= 99`.** They complete the whole
sweep and fail; none is won at any K the schedule could reach sooner. So there is no
winning bound for geometric to arrive at earlier — and equally none to overshoot.
Reaching `kmax` in 7 attempts instead of 34 produces the same `UNKNOWN`, only sooner.

And "fewer attempts" is not "less work": geometric tests 2, 5, 11, 23, 47, 95, and a
single attempt at K=95 costs more than several at K=5..20. That is why it comes out
**3-4% slower** despite doing a fifth as many attempts.

§5.11 requires new 2026 coverage of at least 2. This is 0, with a time regression.

The schedules stay in the tree behind `acacia_k_schedule=linear`, since §5.11's
fallback is to retain several as portfolio arms for P5 rather than averaging them
into one heuristic — and the exhaustive algebra proof is worth keeping regardless.


### P4 — improve OTFUR

#### O1, lazy controller expansion: **LAND**

P2 and P3 each reduced exactly what they promised and moved nothing, because the
reduced quantity was not binding. So O1's premise was measured before it was built,
from counters the forward solver already keeps — actions applied per controller node
expanded:

| instance | actions/node | successors/node |
|---|---:|---:|
| `robot_grid_3x3` | 12.4 | 1.2 |
| `robot_grid_6_6` | 15.3 | 4.7 |
| `lift_gr1_3` | 17.5 | 3.3 |
| `AllLights` | **807.6** | **151** |
| `prioritized_arbiter6` | 129.9 | 78 |
| `collector_v1_7` | 2.0 | 2.0 |

Twelve to eight hundred actions applied per node, up to 151 successors materialised,
one selected. `advance_controller` now stops at the first safe, untried,
non-subsumed successor and computes no further action until that choice is proved
losing.

**Actions actually applied:** `robot_grid` 173,500 → 19,491 (−88.8%), `lift_gr1`
480,482 → 90,137 (−81.2%), `AllLights` 233,397 → 76,074 (−67.4%).

**And it converts to time**, which is what P2 and P3 failed to do:

| instance | eager | lazy | change |
|---|---:|---:|---:|
| `robot_grid_3x3` | 1.85 s | 0.82 s | **−56%** |
| `AllLights` | 7.31 s | 4.96 s | **−32%** |
| `lift_gr1_3` | 5.53 s | 4.03 s | **−27%** |
| `robot_grid_6_6`, `collector_v1` 7 and 11, `prioritized_arbiter6` | 25.0 s | 25.0 s | flat |

Zero verdict changes. §6.8 requires one target improving ≥25%; three do.

#### Correctness

The existing 5,000-game fixed-seed harness still matches on F3/F1/F0, and lazy and
eager are now compared directly on **5,015 games** — same verdict, same losing
antichain, same strategy ranks. The eager path stays behind
`acacia_forward_eager_minimal_successors`, defaulting off, and F3 Pareto
minimisation stays on that side: `minimal_successors` equals `distinct_successors`
on `AllLights` (43,718), `collector_v1` (527,618) and `prioritized_arbiter6`
(1,972,517), so it removes nothing there.

#### What O1 cannot do, and one prediction that was wrong

O1 reduces actions per controller node, not the number of nodes. The four flat
instances are resource-capped, and the profile predicted three of them correctly:
`robot_grid_6_6` creates 439 controller nodes per environment node and hits the
400,000 `max_ctrl_nodes` cap at K=2; `collector_v1` runs at 2.0 actions per node and
is immune twice over.

`prioritized_arbiter6` was predicted to improve and did not. It has 129.9
actions/node and only 27,484 controller nodes, so it is not node-capped — but with
3,281,024 raw actions it hits `max_edges = 2,000,000` instead. The prediction was
right about the mechanism and wrong about which cap binds, which is worth recording
because O2's byte accounting is aimed at exactly these caps.


### P4 — O3 batching reverted, O5 mis-scoped: what remains open

O3's batching was reverted (see the O3 commit) after measurement showed maximum
batch sizes of 2, 5, 2, 5 and 1 — the losing queue drains too often for insertions
to accumulate. The counters it added were kept, and they measured something
striking: **148,504,205 visited-node checks for 6,671 invalidations** on
`prioritized_arbiter` (22,261x), and 628,895 for 676 on `lift_gr1` (930x).

O5 was scoped against that finding, but against the wrong structure. The 22,261x
ratio is the cost of scanning the **visited environment-node set** during
invalidation — for each newly proved losing generator, checking every visited node
to see if the generator now subsumes it. `minimal_losing_antichain`'s own generator
list is a different, much smaller structure: it peaked at **177** entries on
`lift_gr1` in the O3 diagnostics. A linear scan over 177 elements is already cheap,
indexed or not, which is exactly what the measurement showed.

#### O5 as attempted: correct, no material benefit, discarded

Sorting `minimal_losing_antichain`'s generators by rank and binary-searching the
feasible prefix preserved every correctness counter exactly — `queries`, `hits`,
`insertions`, `removals`, `invalidation_scans` and `nodes_invalidated` all
unchanged across before/after — but `prefilter_skips` did not increase (2,455 both
times). The reason: the existing per-element filter was already an O(1) integer
comparison per skip, so sorting only changes how cheaply you skip infeasible
elements, not how many expensive partial-order comparisons are avoided. Per the
acceptance criterion set before the work started — stop rather than add buckets on
top of a mechanism that is not working — the attempt stopped there and the diff was
discarded rather than committed.

#### What is still open, and why it is not being pursued now

The structure that would actually address the 22,261x finding is an index over the
**visited environment-node set**, so that a newly proved losing generator can find
which visited nodes it subsumes without scanning all of them — closer to §6.7's
sketch of a `minimal_upset_antichain<State>` contributed to Posets, with
`subsumes`/`insert`/`size` as its first interface. That is a materially larger
change: it touches how environment nodes are stored and indexed, not a single
generator list, and §6.7 itself frames it as optional and lower priority, gated on
O3/O4 measurements rather than assumed. Those measurements now exist. Whether to
build it is a scope decision the plan reserves for re-approval before P5, not one to
make by continuing past it.

#### P4 overall

| substage | decision |
|---|---|
| O1 lazy expansion | **LAND** |
| O2 interner + byte accounting | **LAND**, on the speed criterion, not the memory one |
| O3 batched invalidation | **STOP**, reverted; counters kept |
| O4 conditional covering | **AGGRESSIVE PRESET**, off by default |
| O5 antichain indexing (as scoped) | **STOP**, discarded; real target identified but not built |


### P5 — isolated arm census (P5A)

Twelve arms of §7.3 run in isolation over the 180-instance deterministic
stratified panel, one process per instance, staged caps 1/5/17 under an 8 GiB
cgroup. **Zero verdict conflicts across all twelve arms.** Screened on the panel
rather than the full selection first: measured throughput put the full
1,524-instance census at ~5 h per arm, ~2.5 days for twelve, and the panel gives
the marginal-contribution signal for about 1/30th of that.

#### Per-arm decisive answers, 180 panel instances

| arm | decisive | R | U |
|---|---:|---:|---:|
| `B-real-small` | 45 | 45 | 0 |
| `B-real-any` | 44 | 44 | 0 |
| `B-unreal-formula-small` | 68 | 0 | 68 |
| `B-unreal-automaton-small` | 66 | 0 | 66 |
| `S-real-small` | 46 | 46 | 0 |
| `S-real-any` | 45 | 45 | 0 |
| `S-unreal-formula-small` | 67 | 0 | 67 |
| `S-unreal-automaton-small` | 67 | 0 | 67 |
| `F-real-small` | 46 | 46 | 0 |
| `F-real-any` | 46 | 46 | 0 |
| `F-unreal-formula-small` | **81** | 0 | 81 |
| `F-unreal-automaton-small` | 79 | 0 | 79 |

Every arm is polarity-pure, which confirms the CLI isolation does what §7.3 needs:
one child, one polarity, no contamination. It also means no single arm is ever a
portfolio, which is why the selector requires one of each.

#### Four arms reach the twelve-arm ceiling

| subset size | best decisive union |
|---|---:|
| 2 arms | 127 |
| 3 arms | 134 |
| **4 arms** | **137** |
| all 12 arms (oracle) | **137** |

A four-slot portfolio captures *everything* twelve arms can. Best 4-arm set:
`F-real-small + F-unreal-formula-small + S-real-small + S-unreal-automaton-small`.

Redundancy is high: across all twelve arms only **2** answers are unique to a
single arm, both in `F-unreal-formula-small`. Arm choice is therefore mostly about
efficiency, not reachability — with that one exception.

#### The forward backend, not the arm shape, is where the coverage is

Comparing like-for-like arm *shapes* as oracle unions on the same 180 instances:

| configuration, default 3-child shape | union |
|---|---:|
| B — the shipping default | **120** |
| S | 120 |
| **F** | **134** |
| best 4-arm, mixed S and F | 137 |

| | |
|---|---:|
| union of all 8 B and S arms | 122 |
| union of all 4 F arms | 134 |
| **F adds over all B/S** | **15** |
| B/S adds over all F | 3 |

Switching the backend to forward, keeping the existing three-child shape and
adding no process slots, captures **+14 of the +17** available. The remaining +3
needs the mixed S/F set, which the current architecture cannot express in one
process because the backend is a compile-time choice — that is exactly the runtime
`game_backend` field of P5B, and it buys 3 panel instances.

This inverts PR #125's picture, where B and F were near-equal at 1056 and 1053 of
1524. The most likely cause is P4: O1 cut forward actions per controller node by
67–89% and O2 added a further 16–29%, both landed since that measurement. P4's
work appears to have shifted the portfolio balance toward forward.

#### Caveats, stated before any decision rests on this

These are **isolated oracle unions on a 180-instance panel**, not race results on
the full selection. §7.5 is explicit that the isolated union is an upper bound. One
independent check exists: G3 measured the actual raced default portfolio at 118/180
on this same panel against the 120 oracle computed here for that same arm set, so
the race penalty looks small — but that is one data point, not a measured race of
the proposed profile.

#### Forward is an unrealizability engine, not a general-purpose backend

Splitting the census by polarity is what makes the result actionable:

| polarity | B | S | **F** | F adds over all B/S | B/S adds over F |
|---|---:|---:|---:|---:|---:|
| real | 45 | 46 | 46 | 3 | **3** |
| unreal | 75 | 74 | **88** | **12** | **0** |

On the real side forward has no edge — it trades three for three. On the unreal
side it **subsumes every answer all four B and S unreal arms produce** and adds
twelve more, with none going the other way.

So the best three-arm set is `S-real-small + F-unreal-formula + F-unreal-automaton`
at **134/180**, against **120** for the shipping default, in the *same three-slot
budget*. That configuration cannot be built today: `ACACIA_FORWARD_SAFETY_SOLVER`
is a compile-time define, so one binary means one backend for every child. The best
single-binary approximation is forced to spend its real worker on a backend that
brings nothing there.

#### Remaining work, reordered by what the census showed

1. **Finish the full-corpus race.** Reading criterion: absence of large regressions
   matters more than the size of the margin, because forward runs on naive data
   structures and the headroom there exceeds the current margin either way.
2. **Split the backend choice by polarity** (supersedes P5B). Runtime, per-arm
   backend so one process can run backward-with-local-certificates for its real
   worker and forward for both unreal workers, reproducing the 134/180 set.
3. **Data structures for the forward solver.**
   `grep -c 'posets::' src/solver/forward_reachable_safety.hh` returns **0** —
   forward is entirely hand-rolled flat vectors while backward picks
   `rank_bucketed_vector_backed` from sixteen tuned posets implementations. The
   target is the invalidation scan over the visited environment-node set that O3
   measured at 148,504,205 checks for 6,671 invalidations (22,261x), **not** the
   177-entry generator list O5 mistakenly indexed.
4. **Arm-choice flexibility** — backend, polarity, translation preference and
   unreal transform independently selectable per worker.
5. **Three-way comparison, then four new top configurations.** Deferred to last so
   it measures finished work: v1 `5ffd8f99`, ltlsynt 2.15.1.dev, best from main
   head, and sprint-best, on the August panels and SyFCo caches reused verbatim.

#### P6 as originally specified is not justified by this data

§8.1 gates P6 on P5 showing that forward real and forward unreal arms both carry
marginal coverage **and** that a four-arm replacement or routing profile cannot
retain enough of it. A four-arm profile retains **all** of it — 137, the full
twelve-arm oracle ceiling. The condition fails, so the six-wide race is not
justified and is not built.


### P5 — full-corpus backend race (result)

Both binaries run their own default portfolio over all 1,524 instances, staged caps
1/5/17, 8 GiB cgroup, sequential, same tree.

| | decisive | conflicts |
|---|---:|---:|
| B (backward, shipping default) | **976** | 0 |
| F (forward) | **1038** | 0 |
| net | **+62** | |

**Zero verdict disagreements.** The fresh B run returned exactly 976, matching the
figure recorded during the P1 gate campaign on an earlier tree — confirming rather
than assuming that P2–P4 left the backward path untouched, since none of them
changes backward-solver code.

#### The panel understated the effect by 4x

The 180-instance screen extrapolated to about +14. The full selection gives **+62**.
Screening was still the right call — it cost 2.5 h instead of 2.5 days and correctly
identified the direction and the polarity split — but its *magnitude* was not
predictive, which is worth remembering before quoting a panel number as a forecast.

#### Time, not just coverage

On the 958 instances both decide:

| | F | B | |
|---|---:|---:|---|
| total | **348.6 s** | 488.1 s | **-28.6%** |
| median | 0.024 s | 0.036 s | -33% |
| faster on | **679** | 279 | 71% |
| sub-1 s | 894 | 873 | +21 |

Forward is not buying coverage with time; it is cheaper on identical work.

Where the budget goes, on F: cap 1 gives 909 answers for 66.6 s (88% of answers, 8%
of time); cap 17 gives 56 answers for 525.0 s (5% of answers, 68% of time). The
staged design is doing real work, and the expensive tail is what made the full
12-arm census a 2.5-day proposition.

#### The regression set decides the portfolio shape

| | F-only | B-only |
|---|---:|---:|
| UNREALIZABLE | **59** | **0** |
| REALIZABLE | 21 | 18 |
| total | **80** | **18** |

**No unrealizable answer exists that backward gets and forward does not**, across
all 1,524. The panel's subsumption claim holds at scale — forward's unreal
dominance is real, not a 180-instance artifact.

The **real** side is genuinely complementary: F gets 21 B misses, B gets 18 F
misses, neither subsumes. Seven of the 18 are `collector_v2`, and `collector` is the
handoff's own named forward negative control (§6.2); P4's profiling measured
`collector_v1` at 2.0 actions per controller node and node-capped, so forward
failing there is expected behaviour rather than a surprise.

So the portfolio composition is settled by data rather than by the panel's guess:

```
panel suggested (3):  S-real + F-unreal-formula + F-unreal-automaton
                      ...misses the 18 backward-only realizable instances

corpus says    (4):  B-real + F-real + F-unreal-formula + F-unreal-automaton
                      ...both real backends, forward-only unreal
```

Four arms, so the existing four-slot budget suffices and §8.1's gate for P6 still
fails. What the 18 change is *which* four — and they raise the value of the
split-backend work, since that portfolio cannot be expressed by any single binary
today.

---

### Stage 1b — three defects found by review, none of which had a test

Two read-only codex reviews ran while the full-corpus race was executing. Neither
built anything, so the race is uncontaminated. They confirmed P1's soundness ("I
found no false-UNREAL path", checked against the top-level implication guard,
`G(A & B)` distribution, the empty-invariant and vacuous-trigger cases) and found
three defects, all now fixed.

#### The B/S/F builds are what they claim to be — verified, not assumed

Before trusting any of the recorded numbers, the three reference binaries were read
back out of their own generated headers:

| build | `LOCAL_CERTIFICATE` | `FORWARD_SAFETY_SOLVER` | `ENABLE_EQUIVARIANT_SOLVER` |
|---|---:|---:|---:|
| `build_p5_B` | 0 | 0 | 1 |
| `build_p5_S` | **1** | 0 | 1 |
| `build_p5_F` | 0 | **1** | **0** |

This matters for one reason worth stating plainly: **F raced with the equivariant
solver switched off, B raced with it on.** F still won by 62. The forward margin is
therefore *understated* by whatever the equivariant pre-pass contributes to B, not
inflated by it.

#### 1. The two configuration frontends disagreed on a solver-behaviour option

`meson.options` defaulted `acacia_local_certificate` to **true**;
`config/acacia-options.json` and `src/configuration.hh` both said **false**. That
option is the one thing separating the S reference configuration from B, so a plain
`meson setup build` produced S while calling itself B.

The reason it survived: `scripts/acacia-config.py` passes every option explicitly,
so the divergence is invisible from exactly the path used for measurement. The
recorded campaigns are unaffected — verified by re-generating `build_p5_B`'s
`acacia_build_config.hh` after the fix and confirming it is byte-identical.

An audit of all 27 shared scalar options found exactly one other divergence, and
that one is deliberate: `acacia_enable_tlsf_frontend` is false in meson (the default
build stays free of the flex/bison dependency; `.github/workflows/main.yml` covers
the frontend in a dedicated job) and true in the JSON (every `acacia-config.py`
binary drives the harness through `-T`). It differs in what the binary *can do*, and
a binary without it rejects `-T` outright rather than answering differently.

`tests/check-config-frontends.py` now enforces agreement on every shared scalar
option and admits a divergence only with a written reason. Reintroducing the
`local_certificate` default fails it.

#### 2. The arm selector could have ranked a soundness bug as coverage

`select-portfolio-arms.py` kept the faster of two answers **without comparing
verdicts**. An arm returning REALIZABLE where another returned UNREALIZABLE would
have grown the union by an instance the subset decides *inconsistently*. On a
two-arm census built to disagree, the old code reported "2 decided" and exited 0; it
now names the instance and exits 1. The scan runs once over the whole census before
ranking, so it reports every conflict rather than aborting on whichever subset
happens to contain the offending pair first.

Re-running the real twelve-arm census after the fix is unchanged — 137 is still the
four-arm ceiling — so nothing previously reported rested on a masked conflict.

#### 3. "Forward backend" did not name the backend that runs

`-Dacacia_forward_safety_solver=true` is not an exclusive selection:
`solve_with_downset` tries the equivariant solver first
(`src/solver/solve_game_impl.hh:164-178`) and the two meson options are independent.
The F preset sidesteps this by disabling the equivariant solver at compile time —
which is why the measured forward numbers are clean — but the option name still
promises something it does not deliver.

This one is not a patch, it is a design constraint on Stage 2: a runtime backend
enum has to name the backend that will actually run, so the equivariant pre-pass has
to become explicitly conditional rather than implicitly first. Carried there.

#### What the local certificates turned out to be worth

Incidental, but it settles Stage 2's scope. Of the twelve subsets that reach the
137/180 ceiling, the four fastest all avoid the S arms entirely:

```
137   140.14s   B-real-any + F-real-small + F-unreal-automaton-small + F-unreal-formula-small
137   141.41s   B-real-small + F-real-any + F-unreal-automaton-small + F-unreal-formula-small
137   143.33s   B-real-any + F-real-any + F-unreal-automaton-small + F-unreal-formula-small
137   145.50s   B-real-any + B-unreal-automaton-small + F-real-small + F-unreal-formula-small
137   145.71s   F-real-small + F-unreal-automaton-small + F-unreal-formula-small + S-real-any   <- first with S
```

Local certificates reach the same ceiling and cost more time to get there. So the
runtime backend enum needs **two** values, not three: `backward` and `forward`. The
four `ACACIA_LOCAL_CERTIFICATE` sites in `k_bounded_safety_aut.hh` stay compile-time
and stay out of Stage 2 — which also keeps the change well clear of the backward
solver's hot path.

Note that the top subset is exactly the shape the full-corpus race independently
arrived at: **backward for real, forward for unreal, plus a forward real arm.** Two
different measurements, one at 180 instances and one at 1,524, agreeing on the
portfolio.

---

### Stage 2 — runtime game backend

#### Why per-polarity flags are not enough, established before spending a campaign

The full-corpus race decomposes by polarity for free, and nobody needed to run
anything to see it: a portfolio's REALIZABLE can only have come from a real arm, and
its UNREALIZABLE only from an unreal one. So the race summaries already contain the
per-polarity arm results.

```
B = 976 decisive = 496 real + 480 unreal
F = 1038 decisive = 499 real + 539 unreal

3-arm mixed  (B-real + F-unreal x2)          = 1035     <- three WORSE than F
4-arm target (B-real + F-real + F-unreal x2) = 1056     <- exactly the B u F ceiling
```

The shipping default portfolio is **three** arms — `ACACIA_TRANSLATION_PREFS` gives
one real preference and `DEFAULT_UNREAL_X` gives both unreal transforms — and the
best three-arm portfolio a per-polarity flag can build *loses three instances against
plain forward*. It trades forward's 21 real-only answers for backward's 18. Running
that campaign would have cost hours to measure a number already derivable, and the
number is negative.

The 4-arm target needs **two real arms with different backends**, which no
per-polarity flag can express. Worse, one census subset that reaches the 137/180
ceiling is `B-real-any + F-real-any` — same translation preference, different backend
— which `-r` cannot express either, since `append_strategy`
(`src/arg_parser.hh:177-183`) rejects duplicates within a `-r` list.

So Stage 2 splits: the backend plumbing, which is landed and verified, and an
explicit per-arm portfolio specification, which is what makes it measurable.

#### Behaviour preservation, checked rather than argued

The claim that existing builds are unaffected is worth more than a code-reading. Both
reference configurations were rebuilt from the Stage 2 source with **byte-identical
meson options** (verified: zero option differences against the recorded builds) and
run through the frozen 40-instance gate:

| configuration | recorded reference | rebuilt from Stage 2 source |
|---|---|---|
| B | 35 solved, 5 lost, PAR-2 217.557s | 35 solved, **the same 5**, PAR-2 218.111s |
| F | 37 solved, 3 lost, PAR-2 162.794s | 37 solved, **the same 3**, PAR-2 164.980s |

Identical instance sets, PAR-2 within 0.3% and 1.4%. The gate reports `GATE FAIL` for
both — before and after the change — because its baseline is `build_best_decomp_mona`,
a third configuration; those losses are the known backward-baseline mismatch.

A first attempt to gate this used the build codex had made, which carried only three
`-D` flags and therefore differed from F in **six** options — `ios_precomputer`
standard rather than `semantic_mona`, `vector_downset` plain rather than
`rank_bucketed`, and four more. It lost two instances more than F, and the difference
had nothing to do with the change under test. A build with forward switched on is not
the forward configuration.

#### The equivariant pre-pass is now bound to the backward backend

Deliberate, and the reason the enum exists. A binary built with both options on ran
the equivariant solver before it ever reached the forward branch, so
`-Dacacia_forward_safety_solver=true` did not name the backend that ran. The forward
reference sidestepped this by compiling the equivariant solver out — which is why the
recorded forward numbers are numbers *without* the pre-pass. Binding it to backward
reproduces both measured configurations exactly instead of inventing a third
combination for which no measurement exists.

#### One defect found in review, carried to the follow-up

Requesting a forward arm on a binary built without the forward solver warned and fell
back to backward. Codex's own proof of the fallback shows the problem:

```
[real=small,backend=forward] Warning: forward game backend requested but not compiled in; falling back to backward
REALIZABLE
```

A run labelled `backend=forward` that ran backward — a **mislabelled** data point, in
the one piece of work whose entire purpose is per-arm attribution. A failed run is
recoverable; a mislabelled one quietly corrupts a census. It becomes a parse-time
error. (`utils::vout` also writes to `std::cout`, the channel carrying the verdict
that `benchlib.py`'s `parse_acacia_result` substring-matches, but that is the smaller
objection: the message happens not to contain "realizable".)

#### The four-arm portfolio, measured: 1052

One binary, one process, four arms
(`--arms real:small:backward,real:small:forward,unreal:formula:forward,unreal:automaton:forward`),
same protocol as the race: all 1,524, staged caps 1/5/17, 8 GiB cgroup, no swap.

| | decisive | real | unreal | conflicts |
|---|---:|---:|---:|---:|
| B (backward, 3 arms) | 976 | 496 | 480 | 0 |
| F (forward, 3 arms) | 1038 | 499 | 539 | 0 |
| **mix4 (4 arms)** | **1052** | **517** | **535** | **0** |

**+14 over forward, +76 over backward.** The prediction was 1056, and the gap is
fully accounted for.

#### The recovery is exact

The second real arm was added to recover the 18 realizable instances backward
decides and forward misses. It recovered **all 18, and nothing else**:

```
predicted recoverable (B-only):   18
mix4 gained over F:               18
  of which are exactly B-only:    18
  gained but not B-only:           0
  B-only still missed:             0
polarity of the 18:               18 realizable, 0 unrealizable
```

`collector_v2` accounts for 7 of them and `chain-simple-*-real` for 4 — the
families P4's profiling identified as forward's node-capped weak spot. The
mechanism predicted from the polarity decomposition is the mechanism observed,
instance for instance.

#### The four losses are the cap boundary, not the portfolio

Four instances forward decides that the four-arm race does not:

```
arbiter_pb_5_pe_.ltl        REALIZABLE     F took 16.787s   at the 17s cap
finding_nemo_pb_2_pe_.ltl   REALIZABLE     F took 15.375s   at the 17s cap
g-real-real.ltl             REALIZABLE     F took 15.805s   at the 17s cap
patrolling-alarm26.ltl      UNREALIZABLE   F took 15.579s   at the 17s cap
```

All four were already inside 90% of the cap, and a fourth arm's contention pushed
them over. The same pattern appeared at the 1 s cap during the run — the five
instances lost there had taken forward 0.879 s to 0.991 s against a 1.0 s cap —
which makes this a boundary effect rather than a coverage loss. Under a larger cap
the portfolio's coverage is the full 1056.

The unreal arms are a built-in control for this: they are *identical code with
identical flags* in F and in mix4, so their delta (539 → 535) measures contention
and nothing else. It did not grow with the cap — three at 1 s, four at 17 s — so
contention is a small fixed set of boundary instances, not a tax that scales.

#### It is also faster

On the 1,034 instances both decide, mix4 takes **711.2 s against forward's 736.1 s,
−3.4%**, despite running four children instead of three. It is slower on more
instances than it is faster on (662 versus 372), so the saving is concentrated in
the expensive ones: the backward real arm answers some hard realizable instances
far faster than forward does, which is the same complementarity that motivated the
portfolio.

#### Decision: **LAND**

`B-real + F-real + F-unreal-formula + F-unreal-automaton` decides 1052 of 1,524
against the shipping default's 976 — **+76** — with zero verdict conflicts, in less
total time, within the existing four-slot budget. §8.1's gate for P6 still fails,
and now for a stronger reason: four arms reach the ceiling that six were speculated
to be needed for.

---

### Stage 3a — the sum prefilter that already existed: **STOP**

`generic_partial_order`'s constructor
(`subprojects/posets/include/posets/vectors/generic_partial_order.hh:11-22`) opens
with a sum comparison and returns the moment it can prove incomparability — but
only for vector types carrying a `.sum` member, the `_sum` family. `VECTOR_IMPL`
resolves to `simd_vector_backed`, which has none, and `acacia_vector_impl` has
offered `simd_vector_backed_sum` as a supported choice all along
(`meson.options:115-117`). Every campaign in this sprint and before it ran with
`auto`, so the filter had never been switched on.

It is sound unconditionally — `lhs <= rhs` componentwise implies
`sum(lhs) <= sum(rhs)` for any values, `-1` sentinels included — and a rejection
costs two integer comparisons instead of a block scan. For a scan running 148 M
comparisons to find 6,671 invalidations, that is the right shape of idea, and it
was one flag away.

#### The metric had to be corrected first

`forward_nodes_checked` cannot measure this, and using it would have produced a
confident null result for the wrong reason. It is incremented at the top of the
scan loop, before the comparison:

```cpp
for (std::size_t id = 0; id < env_nodes.size (); ++id) {
  ++nodes_checked;                              // counts iterations
  if (env_nodes[id].status == losing) continue;
  if (env.rank.partial_order (...).leq ())      // where the prefilter acts
```

The prefilter makes each comparison cheaper without removing an iteration, so the
counter is byte-identical in both builds — confirmed on four instances
(65/65, 290/290, 1911/1911, 271794/271794). The measurement is wall clock.

#### The measurement

Two builds differing in exactly one option (verified), non-diagnostics, five runs
each on eight instances the forward backend spends 1–15 s on, builds alternated so
machine drift hits both:

| instance | nosum min/mean/max | sum min/mean/max | Δ |
|---|---|---|---:|
| LightsTotal_9cbf2546 | 1.34 / 1.40 / 1.46 | 1.28 / 1.37 / 1.47 | −1.8% |
| full_arbiter_unreal1_pb_2_15_pe_ | 1.99 / 2.11 / 2.19 | 1.94 / 2.06 / 2.23 | −2.1% |
| full_arbiter_unreal1_pb_3_7_pe_ | 9.71 / 10.53 / 11.25 | 9.43 / 10.26 / 11.24 | −2.6% |
| infinite-race-unequal-23 | 4.01 / 4.24 / 4.36 | 4.30 / 4.35 / 4.40 | **+2.6%** |
| load_balancer_unreal1_pb_5_5_pe_ | 14.88 / 15.58 / 16.23 | 14.62 / 15.34 / 15.71 | −1.5% |
| patrolling-alarm16 | 2.92 / 3.08 / 3.29 | 2.97 / 3.01 / 3.03 | −2.1% |
| patrolling21 | 6.96 / 7.26 / 7.64 | 7.38 / 7.58 / 7.68 | **+4.3%** |
| robot-cat-real-1d-real | 5.27 / 5.61 / 5.76 | 5.11 / 5.48 / 5.81 | −2.4% |
| **total** | **49.81 s** | **49.46 s** | **−0.7%** |

**Not one instance has non-overlapping min/max ranges**, and the sign goes both ways
(−2.6% to +4.3%). Every verdict is identical. This is noise, not an effect.

#### The single-shot result that would have been wrong

A first pair of runs on `prioritized_arbiter_pb_5_pe_` showed 6.26 s against 5.25 s
— **−16%**, and it was tempting. It is the same trap O2 set earlier in this sprint,
where a −38% single-run figure turned out to be 27% run-to-run variance. Five runs
per configuration and a non-overlapping-range requirement are not ceremony.

#### Why it does not pay, which matters for what comes next

The filter has a cost as well as a benefit: `.sum` is computed on construction and
recomputed by every operation that rewrites a vector
(`generic.hh:111-113, 243-307`). The benefit is skipping a block loop that, for
these automata, is already only one or two SIMD operations wide. Paying a scalar
sum on every vector written to save one or two SIMD comparisons on some fraction of
reads is close to a wash, and it measures as one.

**Decision: STOP.** No flag change, and `simd_vector_backed_sum` stays unused for
the forward backend.

This does not condemn Stage 3b, but it sharpens its precondition. The reason this
failed is that the *per-comparison* cost was already small. A structure that
eliminates candidates wholesale could still pay — but only if the scan is a large
enough share of forward's runtime to be worth attacking, and that share has not been
measured. Measuring it is the next step, before any structure is built.

### Stage 3b — the premise, measured: **STOP, and a correction to record**

Stage 3a failed because the *per-comparison* cost was already small. Stage 3b's
different bet is that a structure eliminating candidates wholesale would pay. Before
building one, the precondition was measured: **what share of forward's runtime is
the invalidation scan?** The plan asserted it was the target, on the strength of
148,504,205 node checks against 6,671 invalidations — a 22,261:1 ratio.

#### The profile

`perf` with DWARF call graphs, on `prioritized_arbiter_pb_5_pe_`, which is
solver-dominated (99.6% of samples in acacia's own code; a first attempt used
`full_arbiter_unreal1_pb_3_7_pe_`, which turned out to be 59.5% BuDDy and 34.6%
Spot — translation, not solving, and useless for this question).

`generic_partial_order` is 25.61% of runtime. Attributed to its callers:

| call path | share |
|---|---:|
| `advance_lazy_controller → subsumes` (losing antichain) | **9.42%** |
| `advance_lazy_controller → subsuming_generator` (same antichain) | **3.54%** |
| `drain_losing_queue → mark_environment_losing` (**the invalidation scan**) | **4.43%** |
| other paths under `solve` | ~4.7% |
| directly under `solve_with_downset` | 3.37% |

On a second instance, `robot-cat-real-1d-real`, the scan does not even reach 2%,
while the largest single costs are `VEC::at()` at 10.30% and `coordinate_hash` at
8.05% — element access and interning, not comparison at all.

#### Two conclusions, one of them a correction to this report

**The invalidation scan is not the bottleneck.** Eliminating it *entirely* buys
4.43% on the instance built to showcase it, and under 2% on the other. That does not
justify a bespoke live-ID dominance reporter.

**The losing antichain is ~3x more expensive than the scan, which reverses what this
report said earlier.** P4's O5 targeted `minimal_losing_antichain`, and the O5 entry
records that as mis-scoped, on the grounds that the structure holds only 177 entries
while the scan does 148 M checks. That reasoning was wrong, and in exactly the way
Stage 3a's metric was wrong: it compared *structure size* against a counter that
turns out to measure **loop iterations rather than comparisons**. The scan's loop
short-circuits on `if (env_nodes[id].status == losing) continue;` before it reaches
`partial_order`, so most of those 148 M iterations never compare anything. The
antichain is small but consulted once per generated successor, which is a far larger
number of actual comparisons. O5's target was the right one; the correction was the
error.

#### Why Stage 3 stops here anyway

Even with the target corrected, the ceiling is not attractive. The antichain paths
total 12.96% on the best case for them and about 3% on the second instance, and that
is the *perfect elimination* bound. The rest of the profile is diffuse: within
`advance_lazy_controller`, comparison, element access, hashing, vector construction
and actioner `apply` each account for between 3% and 10%, with no single dominant
mechanism.

A diffuse profile with a 13% ceiling is not where this sprint's remaining effort
belongs, set against Stage 2's measured +76 instances. Six of eleven mechanisms in
this sprint delivered exactly the reduction they promised and bought nothing;
Stage 3a made seven. Declining to build the eighth on a premise now measured false
is the same discipline, applied one step earlier — before the code rather than after.

**Decision: STOP.** The profile is recorded so the next attempt starts from
measurement rather than from the 22,261:1 ratio, which does not mean what it appears
to mean.

---

### Stage 5b — choosing the four shipped configurations

`docker_default` is the set of configurations precompiled into the image;
`scripts/acacia-bonsai.sh <config>` lets a user pick one. They are alternatives, not
a race. The four it named were all backward, all pre-sprint, and none had a
full-corpus SYNTCOMP26 number — so nothing yet justified replacing them.

Six configurations were measured over all 1,524, staged caps, plus two already
measured this sprint. **Zero verdict conflicts across all eight.**

| configuration | decisive / 1524 |
|---|---:|
| `mix4_contra` — four-arm portfolio + P1's contradiction checker | **1063** |
| four-arm portfolio | 1052 |
| `mix4_bboxtree` — four-arm portfolio on the bboxtree downset | 1041 |
| `best_decomp_rank_bucketed_semantic_mona` (the B reference) | 976 |
| `best_decomp_rank_bucketed_mona` — **incumbent** | 966 |
| `best_decomp_mona` — **incumbent** | 965 |
| `best_decomp_bboxtree_mona` — **incumbent** | 964 |
| `best_decomp_mona_any` — **incumbent** | 961 |

#### P1's checker only pays in company

`ACACIA_FORCED_OUTPUT_CONTRADICTION` shipped **defaulted off**: its own landing gate,
run against the backward configuration in isolation, did not justify flipping it. On
top of the four-arm portfolio it is worth **+11** (1063 against 1052). A mechanism
can be unjustifiable alone and earn its place in combination, and a package-at-a-time
gate cannot see that — this is the one case in the sprint where the per-package
discipline would have thrown away a real gain had the closing campaign not re-tested
it.

The downset structure is worth a comparable amount, not less: the same portfolio on
bboxtree scores 1041 against rank_bucketed's 1052, so **−11**. What dominates both is
the portfolio itself, at +86 over the best incumbent.

#### The incumbents are near-duplicates, confirmed at scale

Their four-way union is **974** against a best single of **966** — four shipped
configurations buying **eight** instances. The August panel put that figure at one;
the full corpus makes it eight, which is larger but still tiny against the 97 that
changing the portfolio buys.

#### What four to ship, and the honest shape of the answer

Greedy marginal analysis over all eight, starting from the best single:

```
mix4_contra alone                          1063
  + a backward configuration                +18
  + a third                                  +2
  + a fourth                                 +1
                                           ————
best achievable four-configuration union   1084
```

**Eighteen of the twenty-one instances that three extra configurations buy come from
the second one.** The third and fourth buy three between them. Anyone weighing build
time and image size against coverage should know that shipping two would capture
1081 of the 1084.

Pure union maximisation picks
`inc_bboxtree + inc_mona_any + mix4_bboxtree + mix4_contra` (1084). That is rejected:
`acacia-bonsai.sh` runs **one** configuration the user selects, so the individual
strength of each slot matters, and that set fills two slots with the weakest
configurations measured (961 and 964) purely because they happen to add one more at
the margin than the strongest backward configuration does.

The set shipped instead trades one instance of union for a far better second choice:

| slot | configuration | individual | why |
|---|---|---:|---|
| 1 | `best_four_arm_contradiction` | **1063** | best measured; the default |
| 2 | `best_decomp_rank_bucketed_semantic_mona` | 976 | strongest backward configuration, +17 marginal against `inc_mona_any`'s +18 |
| 3 | `best_four_arm_bboxtree` | 1041 | the portfolio on a different downset — a different failure mode, not a weaker version of slot 1 |
| 4 | `best_decomp_mona_any` | 961 | the incumbent that contributes most at the margin, and the only pre-sprint shape retained |

Union 1083 against the theoretical 1084. The one instance is spent on making slot 2
worth choosing: a user who picks the backward alternative gets the configuration that
answers 976 rather than 961.

#### The arm set had to become shippable

The winning configuration is an **arm mix**, not a compile-time backend choice, and
until now a portfolio could only be stated as a `--arms` command line — so it could
not *be* a preset. `acacia_default_arms` closes that: a build declares its portfolio,
`--arms` still overrides it, and an empty value keeps the historical portfolio, so
every existing build is unaffected. Verified: the shipping build forks its four arms
with no flags, `build_s2ref_B` still forks three, and the unit suite is clean on both.

---

## Spot on-the-fly API: factory, row construction, and acceptance audit

<sub>Was `benchmarking/SPOT-OTF-API-AUDIT.md`. One-shot audit, pinned to Acacia `d47ca7c4` and Spot `2.15.1.dev`. Superseded as a plan, still accurate as a reading of Spot's API.</sub>

Date: 2026-09-06  
Repository reviewed: `acacia-bonsai` at `d47ca7c476fd5e833e6289ba9387323c3b339e3d`  
Spot reviewed: installed `/usr/local`, `2.15.1.dev`, supplied base `2ae6210237` plus the inert synthesis enum patch described below.  
Method: read all 411 lines of `ltl2taa.cc`, all 309 lines of `taatgba.cc`, all
410 lines of `twaproduct.cc`, and the related implementations and public
contracts cited below. Built the research tools, ran the P0 probe and additional
operator, unsupported-input, resource-limit, and CLI checks, and ran the existing
unit **and version** suites. No benchmark suite or corpus campaign was run.
No translator, solver, row adapter or Büchi wrapper was implemented. Spot was
not rebuilt or re-pinned. The supplied Spot provenance is taken as given.

### Executive call

**A — pass for the public TAA/TGBA construction on the tested LTL cases.**
The highest-value move is **admit direct `ltl_to_taa(f, dict, false)` as the
P5 research candidate, then compare its identical construction fully enumerated
and on demand**. Its factory builds the alternating skeleton eagerly; requesting
a TGBA row combines the skeleton states on demand. These are separate costs,
confirmed by source inspection and measured separately by the probe.

All 24 required formula/provider cases passed language equivalence, fixed
acceptance, and the consumer ownership exercises. This establishes an executable
candidate, not a coverage or performance win. For `GF a & GF b`, the TAA view has
5 reachable states and 29 edges; the ordinary translator reference has 1 state
and 4 edges. A conjunction of 20 eventualities never reaches a successor
request at all: at a one-second cap the TAA factory is killed by the alarm, and
at twenty seconds it dies of `std::bad_alloc` **in the factory**, while the
ordinary translator on the same formula merely runs out of time
(`signal_14@factory`). The two providers therefore fail differently, and the
eager alternating skeleton is the thing that does not fit. A PSL repetition case
throws `unimplemented` there. Retain these negative results.

Do **not** describe the factory as a lazy traversal of the formula, equate
consumer row counts with avoided translation, feed raw alternating transitions
to Acacia, or put graph-only degeneralization in a supposedly lazy path.
P1–P4 remain independent. P5 still needs ordinary Büchi normalization with an
explicit rank-increment convention; this audit implements none of it.

### 1. Version discipline and reproducibility

#### Supplied provenance, preserved without re-pinning

The linked installation is `/usr/local`, with `SPOT_VERSION "2.15.1.dev"` at
installed `spot/misc/_config.h:1357` and headers dated 2026-08-04. It is **not
mainline**. Its base is upstream `2ae6210237` (2026-06-20), or `cb4a98d1cc`
(2026-06-18), which is indistinguishable on installed content, plus a local
patch to `spot/twaalgos/synthesis.hh` adding:

- `GOODSET`: “Exact-signature BDD partition (experimental HOG-inspired)”.
- `GOODSETMIN`: “Minimal-antichain HOG good-set game (experimental)”.

These enum values exist nowhere upstream. Acacia includes that header in four
places but never references `solver_type` or either value; **the patch is inert
here**.

Confirmed by the repository owner after this audit was drafted: GOODSET and
GOODSETMIN are their own work, and nothing else was changed in this Spot
installation. The base identification is therefore authoritative rather than
inferred -- upstream `2ae6210237` plus exactly this one patched header.

The 14 installed headers under `spot/ta/*`, `spot/taalgos/*`, and
`spot/twaalgos/copy.hh` are debris from earlier installations. They were not
audited and are not available-API evidence. The supplied deletion dates are
2025-10-21 for testing automata and 2026-05-16 for `copy.hh`; `make install` does
not remove withdrawn headers. Both deletions precede the pinned June development
base. There is a date inconsistency in the supplied phrase “both before the
2.15.1 tag”: 2026-05-16 follows the supplied tag date 2026-04-25. This does not
alter the debris classification for this pinned development installation.

All Spot source citations below are local `file:line-range` references relative
to this read-only checkout, already at `2ae6210237`:

```text
/tmp/claude-1000/-home-gperez-GIT-repos-acacia-bonsai/929d329c-78e3-49f9-a5e6-0f4419a23557/scratchpad/spot-src
```

The supplied comparison establishes that `spot/twa/taatgba.hh`,
`spot/twaalgos/ltl2taa.hh`, `spot/twa/twaproduct.hh`, `spot/twa/twa.hh`, and
`spot/twaalgos/degen.hh` there are byte-identical to the installed headers.
**The source citations are valid for the linked library because this is its
supplied matching base and these interfaces are byte-identical; the only
supplied local patch is the inert synthesis enum extension.** Targeted `cmp`
also confirmed equality for `ltl2tgba_fm.hh` and `twagraph.hh` while resolving
their additional public interfaces. No website or newer Spot branch supplied
API evidence.

#### Actual build and execution identity

| Item | Recorded value |
|---|---|
| Acacia | `d47ca7c476fd5e833e6289ba9387323c3b339e3d`; the pre-existing untracked `otf.md` was left unchanged |
| Posets | `139e14336b7a1f0bc064022e587ea4e1b9a81427` |
| tlsf-tools | `b42d5ef4a680252e04820ac7f073f5d786a43f7c` |
| Benchmark submodule | `4105caf1f1e5fd3b76657879bfce8021d130cbde`; no corpus run |
| Spot / BuDDyX pkg-config versions | `2.15.1.dev` / `2.3a` |
| Runtime libraries (`ldd`) | `/usr/local/lib/libspot.so.0`, `/usr/local/lib/libbddx.so.0` |
| Acceptance capacity | `SPOT_MAX_ACCSETS=64`, installed `_config.h:1119-1120` |
| Compiler / linker | GCC `16.2.1 20260819 (Red Hat 16.2.1-2)` / GNU ld `2.46.1-1`, x86-64 |
| Build | Meson `1.11.2`, C++23, `debug`, `-O0 -g`, LTO false, sanitizers `[]` |
| Preset | No named preset: `acacia_preset=""`; complete normalized project options below |
| Worker arms / K | No worker or solver invoked by the probe; configured defaults: arms empty, linear K schedule, minimum 2, increment 3, maximum 99 |
| CPU affinity | Inherited CPUs `0-15`; no dedicated core |
| Cgroup | Current scope and visible ancestors: `cpu.max=max 100000`, `memory.max=max` |
| Probe limits | Each fresh child: wall alarm 10 s, address-space limit 1,024 MiB, core dumps disabled; the explicit cap check uses 1 s |
| Corpus / source maps | Not applicable to the synthetic formula list; no source-map or corpus campaign was generated |

The current cgroup was
`/user.slice/user-1000.slice/user@1000.service/app.slice/ptyxis-spawn-f9781140-dfe4-4960-ad91-943284d7b08a.scope`.
Spot's compiler flags were not reconstructed; its installed binary hashes
identify the build used. The consumer build has no sanitizers, so ownership
smoke checks below do not claim a leak-detector or sanitizer result.

```text
8014c3b6f78bb3e9f6bd7eb2a899db506db56f3e7de788dc99b2eb6443a83b20  /tmp/otf-p0/src/acacia-spot-otf-probe
3eb40696b2d3c0f42fdc94d477f7de64c140d0f4eb156bc0ea29a661dca2a89c  src/research/spot_otf_probe.cc
0d6dbbb6c985bd3001d1df5b6b562e5b403a00ec0d6bb3275fc0617b3d5faf32  /usr/local/lib/libspot.so.0
a991f2049c44e3f3cf9102b7d40d9efc2c5bb2b8a3a1af7d120f7c23c9caf44d  /usr/local/lib/libbddx.so.0
410f01c9ea95407d0c142ced34ef4af5fcdcd507ba24f3093615f2e5848d4fb7  otf.md
0be3283fd2fc66146279fe29d7507bd0479748c9ec9b2939bfedb59a2ccd3f15  config/acacia-options.json
bdaa59dfe0f5e96fc317ec646abb62f7f7c9af89bbd220c807e2b3becf07120c  config/acacia-presets.json
70649ab29245af2f67c78ad82500a56297b8dc7d918cc2cefd1015de1d35ab30  tests/ltl/realizable/OneCounterGuiA9.ltl
18c673b92660a94679a8d32fd86182b1a71c15d2af125583be0e8a9754485e4a  /tmp/otf-p0-normalized-options.json
```

The normalized project-option snapshot includes every project option from
`/tmp/otf-p0/meson-info/intro-buildoptions.json`. Solver flags are recorded for
reproducibility but do not affect this Spot-only probe:

```json
{
  "acacia_actioner": "standard",
  "acacia_aut_preprocessor": "surely_losing",
  "acacia_boolean_states": "forward_saturation",
  "acacia_compile_all_components": false,
  "acacia_compiler_profile": "debug",
  "acacia_cpre_avoid_unions": false,
  "acacia_decompose_spec": true,
  "acacia_default_arms": "",
  "acacia_default_k": 99,
  "acacia_default_kinc": 3,
  "acacia_default_kmin": 2,
  "acacia_default_spot_fast": "det",
  "acacia_default_unreal_x": "both",
  "acacia_enable_diagnostics": false,
  "acacia_enable_equivariant_solver": true,
  "acacia_enable_realizability_simplifier": true,
  "acacia_enable_syntactic_bypass": true,
  "acacia_enable_tlsf_frontend": false,
  "acacia_equivariant_exhaustive_detect": false,
  "acacia_equivariant_max_orbits": 4096,
  "acacia_equivariant_max_output_letters": 4096,
  "acacia_equivariant_max_states": 512,
  "acacia_equivariant_max_sweep_clients": 4,
  "acacia_equivariant_min_blocks": 2,
  "acacia_equivariant_min_clients": 3,
  "acacia_equivariant_validate_fast_recognition": false,
  "acacia_forced_output_contradiction": false,
  "acacia_forward_conditional_covering": false,
  "acacia_forward_eager_minimal_successors": false,
  "acacia_forward_safety_solver": false,
  "acacia_input_picker": "critical_pq",
  "acacia_ios_precomputer": "standard",
  "acacia_k_schedule": "linear",
  "acacia_local_certificate": false,
  "acacia_ltl_frontend": "baseline",
  "acacia_no_simd": false,
  "acacia_preset": "",
  "acacia_profile_dominance": false,
  "acacia_simd_is_max": true,
  "acacia_symmetry_profile": false,
  "acacia_symmetry_verbose_diagnostics": false,
  "acacia_tlsf_corpus_dir": "",
  "acacia_transition_acceptance": false,
  "acacia_translation_pref": "small",
  "acacia_vector_downset": "vector_backed",
  "acacia_vector_impl": "auto",
  "build_python": false,
  "build_research_tools": true,
  "build_tests": true
}
```

### 2. Resolve every P0 entry point

#### API evidence table (otf.md 3.2 and 11.3)

All rows use linked version `2.15.1.dev`, supplied base `2ae6210237` plus the inert
enum patch. “Usable” distinguishes an eager reference, an LTL generation
candidate, and a lazy product of already constructed factors.

| Provider / entry point | Linked version | Factory does | Row request does | Acceptance | Materializing calls | Usable? | Source evidence |
|---|---|---|---|---|---|---|---|
| `translator::run` / `ltl_to_tgba_fm` | pinned `2.15.1.dev` | Simplification, construction and postprocessing return a complete explicit graph. FM drains `formulas_to_translate`, allocating states and edges. | Reads existing graph storage; no saved factory work. | FM complements promise marks and fixes generalized Büchi after exhausting its worklist; translator can subsequently change acceptance during postprocessing. | Calling either graph factory already materializes. | **Accept as eager reference.** No row-level operation on `translator::run`; the separate FM explorer below is public. | `spot/twaalgos/translate.cc:760-775,795-805,936-960`; `spot/twaalgos/ltl2tgba_fm.cc:2398-2450,2477-2480`; postprocessing at `spot/twaalgos/translate.cc:455-458` |
| `ltl_to_taa` | pinned `2.15.1.dev` | Unabbreviation and negative normal form; recursively constructs explicit alternating states, transitions, destination sets, BDD guards and acceptance inventory; installs initial skeleton set. | The returned object's generic TGBA interface combines skeleton transitions for the requested set-state. | Generalized Büchi fixed before return. | `make_twa_graph(const_twa_ptr, ...)` later enumerates the reachable TGBA. | **Accept as P5 LTL candidate**, subject to normalization and future tests. | `spot/twaalgos/ltl2taa.cc:395-409,145-235,243-305,340-389`; `spot/twa/taatgba.hh:174-218,269-291` |
| `taa_tgba::get_init_state`, `succ_iter`, `taa_succ_iterator` | pinned `2.15.1.dev` | Retains the eager alternating skeleton described above. | Initial request only wraps `init_`; `succ_iter` constructs a whole TGBA row by Cartesian combination, BDD conjunction, pruning and merging; `first`/`next` traverse that completed row. | Iterator holds a const acceptance reference; `acc()` complements stored marks against that fixed inventory. | Generic graph copy traverses successive set-states. | **Yes, reachable TGBA combination is deferred**, with row-sized work at iterator acquisition. | `spot/twa/taatgba.cc:52-64,124-231,249-287` |
| `twa_product` / `otf_product` | pinned `2.15.1.dev` | Checks shared dictionary, retains factors, creates pool metadata, copies AP registrations and composes acceptance. Does not enumerate pairs or factor rows. | Initial request allocates one pair; iterator reads factor rows and lazily tries edge pairs, skips false guard intersections, and creates a destination pair on `dst()`. | Right acceptance indices shift by left set count; conditions conjoin and marks union after the same shift. | Generic `make_twa_graph` enumerates reachable pairs. | **Accept for provider/ownership tests** and deferred product construction; not arbitrary lazy LTL translation. | `spot/twa/twaproduct.hh:79-94,136-141`; `spot/twa/twaproduct.cc:278-313,323-354,129-134,162-204` |
| `copy` / `make_twa_graph` | pinned `2.15.1.dev` | Empty-graph overload only allocates a graph; generic provider overload invokes the local implementation `copy`. Graph input can take a direct graph-copy fast path. | Generic copy drains a deque and consumes each complete successor row, interns destinations semantically, and adds graph edges. | Copies acceptance before traversal; provider must already have its final acceptance inventory. | **Use `make_twa_graph(aut, twa::prop_set::all())` with static type `const_twa_ptr`.** It fully materializes TAA/product. `copy` is an anonymous-namespace function, not a public header API. | **Accept for eager control**; never equate it with lazy generation. | `spot/twa/twagraph.hh:855-900`; `spot/twa/twagraph.cc:1603-1623,1654-1679,1693-1752` |
| `degeneralize` / `degeneralize_tba` | pinned `2.15.1.dev` | Requires an existing graph, normally builds an explicit graph of original-state/acceptance-level pairs, and may inspect SCCs. | No generic lazy row interface here. | First produces state-based ordinary (co)Büchi, second transition-based ordinary (co)Büchi. Already suitable inputs can be returned unchanged. | A generic provider must be materialized **before** it can be passed; nontrivial degeneralization itself drains a graph worklist. | **Reject in a lazy path**; accept as a later eager normalization oracle. | `spot/twaalgos/degen.hh:68-99`; `spot/twaalgos/degen.cc:324-355,435-443,488-539,744-774` |
| `twa` helpers / dictionary | pinned `2.15.1.dev` | Provider owns dictionary registrations and may retain iterator storage. | Semantic equality through `compare`/`hash`; each returned state is owned; `release_iter` can cache its iterator. | Guards need live BDD handles and registrations; transition marks do not establish a state-based rank convention. | State/iterator helpers alone do not materialize. | **Required contract** for any later consumer. | `spot/twa/twa.hh:51-108,153-204,208-252,485-501,708-718,741-762`; `spot/twa/twa.cc:37-50`; `spot/twa/bdddict.hh:44-63` |
| Additional public `ltl_to_tgba_fm_otf` | pinned `2.15.1.dev` | Initializes simplifiers/dictionaries and normally canonicalizes the initial formula by translating its symbolic successors. Does not drain the graph worklist. | Public `succ_as_bdd` / `succ_as_edges` compute formula-state successors, including destination canonicalization and promise allocation. | **Can grow during exploration; returned marks use negated-Inf semantics.** Complementation against the final number of sets is deferred until exploration ends. | `ltl_to_tgba_fm` is its eager worklist consumer. | **Reject as a ready fixed-acceptance `twa` provider.** Public row access exists, but this class is not a `twa` and its acceptance contract needs additional design. Not executed by this probe. | `spot/twaalgos/ltl2tgba_fm.hh:120-164,186-248`; `spot/twaalgos/ltl2tgba_fm.cc:2003-2110,2131-2158,2424-2450`; initial canonicalization at `spot/twaalgos/ltl2tgba_fm.cc:1889-1903` |

#### What “All allocated state sets” actually owns

`state_set_vec_` is a vector of pointers to destination sets in the **alternating
skeleton** and its initial set. `create_transition` calls `add_state` and
`add_state_set`; the latter allocates a set, populates it with skeleton-state
pointers and appends it to that vector. `set_init_state` also calls
`add_state_set`. This happens during the recursive factory visitor
(`spot/twa/taatgba.hh:64-72,174-199,269-291`;
`spot/twaalgos/ltl2taa.cc:44-48,313-318,395-409`).

The vector is **not an arena of all reachable TGBA combinations**. The source
makes that distinction unambiguous: `taa_succ_iterator` allocates its own
destination combinations and transitions into `succ_`/`seen_`, deletes them in
its destructor, and `dst()` returns an independently owned copy of a destination
set. It does not append to `state_set_vec_`
(`spot/twa/taatgba.cc:124-231,233-247,269-274`). The skeleton sets live until
`taa_tgba` destruction (`spot/twa/taatgba.cc:38-44`); skeleton states and
transitions are deleted by the labelled subclass
(`spot/twa/taatgba.hh:164-171`).

Nor is the factory merely linear formula bookkeeping. Binary release rules
combine child successor lists, and conjunction uses `all_n_tuples` to enumerate
their Cartesian product before returning
(`spot/twaalgos/ltl2taa.cc:197-214,243-294,340-389`). With refined rules enabled,
language-containment checks add work (`spot/twaalgos/ltl2taa.cc:162-163,194-195`).
This probe explicitly uses the default **false** setting.

Initial-state access allocates a `set_state` wrapping the existing `init_`, with
no successor construction. The entire next row is built by the iterator
constructor, including an accepting true self-loop for the empty conjunction,
Cartesian combinations, guard conjunction/pruning, and transition merging
(`spot/twa/taatgba.cc:52-64,124-231`). An abandoned TAA iterator has therefore
already paid for its whole row. There is no persistent complete-row cache in
this provider: `succ_iter` always constructs a new iterator; the base
`release_iter` cache can retain an old iterator, but TAA does not reuse it
(`spot/twa/taatgba.cc:59-64`; `spot/twa/twa.hh:708-718`).

#### Supported operators and the acceptance boundary

The factory first applies `negative_normal_form(unabbreviate(f, "^ieFG"))`.
Thus the visitor's unimplemented direct `F`/`G`, XOR, implication and equivalence
cases do **not** mean those LTL inputs are unsupported. They are rewritten
before visiting. The implemented core handles constants, APs, negated APs,
`X`/`strong_X`, `U`, `W`, `R`, `M`, conjunction and disjunction
(`spot/twaalgos/ltl2taa.cc:52-140,395-409`). PSL/SERE operators such as closure,
star and concatenation, quantifiers and the empty-word operator reach
unimplemented cases if they survive rewriting
(`spot/twaalgos/ltl2taa.cc:68-69,110-129`). Do not infer general PSL support from
a PSL spelling that the formula constructors simplify to LTL.

TAA acceptance sets are allocated eagerly by
`taa_tgba_labelled::add_acceptance_condition`; the factory finally sets the
generalized-Büchi formula. The iterator reads a const `acc_cond&` and complements
its stored promise marks on access; it never adds a set
(`spot/twa/taatgba.hh:211-218,149-152`;
`spot/twaalgos/ltl2taa.cc:408-409`; `spot/twa/taatgba.cc:124-126,283-287`).
The materialized view is an existential TGBA even though its private skeleton
is alternating. Zero acceptance sets mean `t`, not an ordinary one-set Büchi
encoding. The empty language can still have a single initial state and no
edges, as the `false` cases show.

In contrast, the FM explorer documents changing acceptance and negated marks
explicitly (`spot/twaalgos/ltl2tgba_fm.hh:134-163`). Its default initial
canonicalization already computes a symbolic row, and later translation can
allocate more colors (`spot/twaalgos/ltl2tgba_fm.cc:337-346,1889-1903,2106-2110`).
Its public API does not document a standalone operation that freezes a complete
promise-to-color inventory before exposing positive-Inf TGBA rows. Optional
fair-loop approximation or unobservable-event modes do pre-register promises
in the constructor (`spot/twaalgos/ltl2tgba_fm.cc:2048-2064`); those modes also
change translation behavior and were not validated here as a general freezing
contract. A future FM experiment would need to establish that contract, a
`twa` view and tests; it is not needed to finish P0 and was not implemented here.

### 3. Probe protocol and ownership

`src/research/spot_otf_probe.cc` follows `forward_game_replay.cc`'s hand-written
long-option parser, `fail`, `need_argument`, `usage`, TSV header and top-level
exception handling. Meson registers `acacia-spot-otf-probe` directly inside
`build_research_tools`, with only `[spot_dep, bddx_dep]`. It includes no Acacia
solver or `utils/verbose.hh`, so it requires neither Posets/stdsimd nor the
`utils::verbose`/`utils::vout` definitions.

Each formula/provider has three separate forked runs. They share no constructed
automata or dictionaries with one another. Parsing and dictionary creation
precede the factory measurement. No translation warm-up is performed.

1. **Partial:** measure the factory; record acceptance and any public explicit
   graph state count; request only the initial state; consume exactly its
   complete outgoing row; retain the first destination in provider iteration
   order and consume exactly its row. A self-loop destination is allowed.
   `false` has no destination, so its destination timing is `NA`.
2. **Full:** construct a fresh provider (separately report `full_factory_ms`),
   then call generic `make_twa_graph`. The wall-time and address-space caps
   cover the whole child, including factory and language check. Successful
   enumeration produces a complete graph; a killed/failed run has missing
   metrics and cannot pass language validation. The translator reference takes
   Spot's graph-copy fast path, explicitly labelled in `status`.
3. **Ownership:** construct a fresh provider and test cloned initial states,
   repeated `dst()`/clones using `compare` and `hash`, release after an early
   exit, release during an injected C++ exception, then complete a row after
   those release paths. Report success only after provider/dictionary teardown.

The reference is ordinary default `spot::translator{dict}.run(f)`:
TGBA/Small/High (`spot/twaalgos/postproc.hh:258-260`). It is a language and eager
construction reference, not Acacia's entire preprocessing pipeline.
Acacia's `create_automaton` normally requests BA/SBAcc, or Buchi for transition
acceptance (`src/solver/create_automaton.hh:18-54`). Nothing in the TAA factory
is routed through that function, `translator::run` or a postprocessor.

Product factors are two **independently translated** copies of the same formula,
sharing one dictionary. Consequently their intersection still recognizes that
formula, and all three providers use the same reference language. The default
factors have at most four states each. Factor translation is timed separately
as `factor_factory_ms` before timing `otf_product`; `factor_states` records both
sizes. The duplicated nontrivial acceptance sets exercise index shifting: for
`GF a & GF b` each factor has two sets and the product has four.

Every initial/destination/clone owned by the probe uses a `unique_ptr` deleter
calling `state::destroy()`. Every acquired iterator uses a deleter calling its
provider's `release_iter()` on completion, early exit and C++ exception. Both
copies returned by repeated `dst()` calls are destroyed exactly once; the first
retained destination remains owned after the first iterator is released. States
are never keyed by raw pointers or `format_state`. The full graph builder uses
Spot's semantic `state_map` and its own ownership implementation. The separate
ownership check expects three released consumer iterators and no live consumer
states. These counters verify **consumer cleanup**, not factory laziness or
the absence of all internal library leaks. Process termination by a signal
necessarily relies on OS cleanup rather than C++ unwinding.

For a later interner, `state_unicity_table` consumes the supplied state,
destroys duplicates and owns canonical states until table destruction;
`state_map` supplies semantic hash/equality but leaves state destruction to
the caller (`spot/twa/twa.hh:191-252`). Protect a not-yet-transferred state if
allocation can throw. Keep the provider alive until all its states, guards,
iterators and registrations are released. `register_ap` registers both AP
metadata and dictionary ownership; `twa::~twa` unregisters its variables
(`spot/twa/twa.hh:741-762`; `spot/twa/twa.cc:45-50`). Owning the dictionary alone
does not substitute for those registrations.

One source-level cap trap is deliberately avoided: the pinned generic graph
copy's `max_states` branch calls `seen.find(t->dst())` without releasing that
temporary destination, and can create more than the requested number before
marking incomplete states (`spot/twa/twagraph.cc:1703-1720`). This is a static
finding, not an executed leak test. The probe uses process limits around the
uncapped complete-copy operation, never the truncated-display overload.

#### Measurement definitions

- Times are milliseconds from `steady_clock`, one observation each. Row times
  include iterator acquisition, complete consumption, duplicate-destination
  equality checks and iterator release. Full materialization time excludes its
  fresh factory and the subsequent language check.
- `factory_peak_bytes` is Linux `getrusage(RUSAGE_SELF).ru_maxrss * 1024`
  sampled immediately at factory return. It is the **absolute process RSS
  high-water mark through return**, not a resettable factory allocation peak.
  `factory_baseline_peak_bytes` and `/proc/self/statm` RSS before/after are
  separate columns. They include runtime overhead, shared-library pages, BDD
  infrastructure and, for products, the prebuilt factors. The two Linux RSS
  interfaces need not report identical accounting. Their differences do not
  measure attributable heap allocations. Every row explicitly labels the
  high-water interpretation; no unavailable allocation peak is invented.
- `states_after_factory` is the explicit graph's `num_states()`, or `NA` for
  TAA/product. There is no public numeric factory-state-count accessor for
  these providers; their status says so. Source inspection, not a zero count
  or a consumer counter, establishes what their factories construct.
- Acceptance cells are `number_of_sets:acceptance_formula`. All partial
  checkpoints, both fresh factories, the fully explored provider and the
  materialized graph are compared. `acceptance_changed=unknown` is retained
  when a checkpoint is unavailable; it is never interpreted as stability.
- Language validation calls `spot::are_equivalent(materialized, reference)`
  after timing enumeration. This is an automata-language comparison, not a
  finite-word sample. For the translator row it is a consistency control using
  the same library translator, not an independent proof of that translator.
- Exit 0 means all requested checks completed and passed; exit 2 retains a
  provider error, signal, cap, language mismatch or unknown/changed acceptance;
  exit 1 is a driver/CLI error. Each child reports its current stage before
  risky work, so an exception/signal preserves completed metrics and later
  providers/formulas still run.

### 4. Measured evidence

#### Required eight formulas: actual complete TSV output

Command: `/tmp/otf-p0/src/acacia-spot-otf-probe`. Exit **0**, 24 data rows;
stderr empty. These are the measured values, not predictions or reconstructed
factory-work counts. The tiny, unreplicated timings are API cost observations;
they are not a benchmark speedup claim.

```tsv
formula	provider	factory_ms	factory_peak_bytes	states_after_factory	init_only_ms	first_row_ms	first_row_edges	one_destination_row_ms	full_enumeration_ms	full_enumeration_states	full_enumeration_edges	acceptance_before	acceptance_after	acceptance_changed	language_check	status	acceptance_after_init	acceptance_after_first_row	acceptance_after_destination	full_acceptance_before	materialized_acceptance	one_destination_row_edges	factory_baseline_peak_bytes	factory_rss_before_bytes	factory_rss_after_bytes	factor_factory_ms	factor_states	full_factory_ms	ownership_check	consumer_iterators_released	ownership_iterators_released
true	translator	0.180026	16166912	1	0.000553	0.002491	1	0.000703	0.001527	1	1	0:t	0:t	no	pass	ok;peak_is_process_hwm;materialization_is_graph_copy	0:t	0:t	0:t	0:t	0:t	1	13139968	13811712	16392192	NA	NA	0.086860	ok	2	3
true	taa	0.034870	14274560	NA	0.000445	0.008137	1	0.005506	0.027441	2	2	0:t	0:t	no	pass	ok;peak_is_process_hwm;factory_state_count_unobservable	0:t	0:t	0:t	0:t	0:t	1	13082624	13824000	14557184	NA	NA	0.041016	ok	2	3
true	product	0.006894	16543744	NA	0.000378	0.002307	1	0.001050	0.004275	1	1	0:t	0:t	no	pass	ok;peak_is_process_hwm;factory_state_count_unobservable	0:t	0:t	0:t	0:t	0:t	1	16281600	16764928	16769024	0.096113	1+1	0.011106	ok	2	3
false	translator	0.105308	16113664	1	0.000357	0.000958	0	NA	0.001091	1	0	0:t	0:t	no	pass	ok;peak_is_process_hwm;materialization_is_graph_copy;no_destination	0:t	0:t	0:t	0:t	0:t	NA	13086720	13828096	16334848	NA	NA	0.107892	ok	1	3
false	taa	0.033380	14278656	NA	0.000411	0.001800	0	NA	0.012011	1	0	0:t	0:t	no	pass	ok;peak_is_process_hwm;factory_state_count_unobservable;no_destination	0:t	0:t	0:t	0:t	0:t	NA	13086720	13828096	14561280	NA	NA	0.020446	ok	1	3
false	product	0.007485	16412672	NA	0.000431	0.001016	0	NA	0.002239	1	0	0:t	0:t	no	pass	ok;peak_is_process_hwm;factory_state_count_unobservable;no_destination	0:t	0:t	0:t	0:t	0:t	NA	16150528	16699392	16703488	0.136384	1+1	0.007696	ok	1	3
F a	translator	0.188481	16547840	2	0.000242	0.001983	2	0.000686	0.000838	2	3	1:Inf(0)	1:Inf(0)	no	pass	ok;peak_is_process_hwm;materialization_is_graph_copy	1:Inf(0)	1:Inf(0)	1:Inf(0)	1:Inf(0)	1:Inf(0)	1	13389824	14061568	16846848	NA	NA	0.139875	ok	2	3
F a	taa	0.021945	14581760	NA	0.000256	0.005986	2	0.001959	0.012790	2	3	1:Inf(0)	1:Inf(0)	no	pass	ok;peak_is_process_hwm;factory_state_count_unobservable	1:Inf(0)	1:Inf(0)	1:Inf(0)	1:Inf(0)	1:Inf(0)	2	13389824	14061568	14794752	NA	NA	0.023260	ok	2	3
F a	product	0.008997	16941056	NA	0.004206	0.004508	2	0.001584	0.007249	2	3	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	no	pass	ok;peak_is_process_hwm;factory_state_count_unobservable	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	1	16678912	17178624	17182720	0.272571	2+2	0.014374	ok	2	3
G a	translator	0.253091	16551936	1	0.000501	0.003053	1	0.001307	0.001563	1	1	0:t	0:t	no	pass	ok;peak_is_process_hwm;materialization_is_graph_copy	0:t	0:t	0:t	0:t	0:t	1	13393920	14065664	16842752	NA	NA	0.245269	ok	2	3
G a	taa	0.032879	14585856	NA	0.000568	0.007411	1	0.002748	0.011399	1	1	0:t	0:t	no	pass	ok;peak_is_process_hwm;factory_state_count_unobservable	0:t	0:t	0:t	0:t	0:t	1	13393920	14065664	14798848	NA	NA	0.021678	ok	2	3
G a	product	0.006461	16945152	NA	0.000325	0.002296	1	0.001125	0.003078	1	1	0:t	0:t	no	pass	ok;peak_is_process_hwm;factory_state_count_unobservable	0:t	0:t	0:t	0:t	0:t	1	16683008	17170432	17174528	0.161822	1+1	0.007659	ok	2	3
GF a	translator	0.205339	16945152	1	0.000256	0.002100	2	0.001056	0.000848	1	2	1:Inf(0)	1:Inf(0)	no	pass	ok;peak_is_process_hwm;materialization_is_graph_copy	1:Inf(0)	1:Inf(0)	1:Inf(0)	1:Inf(0)	1:Inf(0)	2	13393920	14069760	17182720	NA	NA	0.190478	ok	2	3
GF a	taa	0.023563	14585856	NA	0.000242	0.005363	2	0.003833	0.013940	2	5	1:Inf(0)	1:Inf(0)	no	pass	ok;peak_is_process_hwm;factory_state_count_unobservable	1:Inf(0)	1:Inf(0)	1:Inf(0)	1:Inf(0)	1:Inf(0)	3	13393920	14069760	14802944	NA	NA	0.023813	ok	2	3
GF a	product	0.006116	17207296	NA	0.000311	0.003023	2	0.001742	0.002778	1	2	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	no	pass	ok;peak_is_process_hwm;factory_state_count_unobservable	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2	16945152	17510400	17514496	0.256107	1+1	0.006078	ok	2	3
GF a & GF b	translator	0.425252	16945152	1	0.000252	0.002664	4	0.001631	0.000929	1	4	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	no	pass	ok;peak_is_process_hwm;materialization_is_graph_copy	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	4	13393920	14069760	17215488	NA	NA	0.398376	ok	2	3
GF a & GF b	taa	0.034797	14585856	NA	0.000272	0.007504	4	0.004545	0.032417	5	29	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	no	pass	ok;peak_is_process_hwm;factory_state_count_unobservable	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	4	13393920	14069760	14868480	NA	NA	0.035016	ok	2	3
GF a & GF b	product	0.006292	17207296	NA	0.000341	0.005623	4	0.003093	0.004243	1	4	4:Inf(0)&Inf(1)&Inf(2)&Inf(3)	4:Inf(0)&Inf(1)&Inf(2)&Inf(3)	no	pass	ok;peak_is_process_hwm;factory_state_count_unobservable	4:Inf(0)&Inf(1)&Inf(2)&Inf(3)	4:Inf(0)&Inf(1)&Inf(2)&Inf(3)	4:Inf(0)&Inf(1)&Inf(2)&Inf(3)	4:Inf(0)&Inf(1)&Inf(2)&Inf(3)	4:Inf(0)&Inf(1)&Inf(2)&Inf(3)	4	16945152	17547264	17551360	0.608184	1+1	0.005930	ok	2	3
G(a -> F b)	translator	0.258139	16949248	2	0.000264	0.002258	2	0.001107	0.001778	2	4	1:Inf(0)	1:Inf(0)	no	pass	ok;peak_is_process_hwm;materialization_is_graph_copy	1:Inf(0)	1:Inf(0)	1:Inf(0)	1:Inf(0)	1:Inf(0)	2	13398016	14073856	17211392	NA	NA	0.440260	ok	2	3
G(a -> F b)	taa	0.042907	14589952	NA	0.003715	0.008950	2	0.003419	0.027643	2	5	1:Inf(0)	1:Inf(0)	no	pass	ok;peak_is_process_hwm;factory_state_count_unobservable	1:Inf(0)	1:Inf(0)	1:Inf(0)	1:Inf(0)	1:Inf(0)	2	13398016	14073856	14807040	NA	NA	0.052336	ok	2	3
G(a -> F b)	product	0.006156	17211392	NA	0.000314	0.003418	2	0.002104	0.004276	2	4	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	no	pass	ok;peak_is_process_hwm;factory_state_count_unobservable	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2	16949248	17539072	17543168	0.331305	2+2	0.006155	ok	2	3
(a U b) | G c	translator	0.327741	16818176	4	0.000233	0.002478	4	0.000978	0.001149	4	8	1:Inf(0)	1:Inf(0)	no	pass	ok;peak_is_process_hwm;materialization_is_graph_copy	1:Inf(0)	1:Inf(0)	1:Inf(0)	1:Inf(0)	1:Inf(0)	2	13398016	14073856	16986112	NA	NA	0.311600	ok	2	3
(a U b) | G c	taa	0.030667	14589952	NA	0.000349	0.006336	3	0.002078	0.012537	4	7	1:Inf(0)	1:Inf(0)	no	pass	ok;peak_is_process_hwm;factory_state_count_unobservable	1:Inf(0)	1:Inf(0)	1:Inf(0)	1:Inf(0)	1:Inf(0)	2	13398016	14073856	14811136	NA	NA	0.029280	ok	2	3
(a U b) | G c	product	0.006377	17080320	NA	0.000309	0.004555	4	0.001642	0.005431	4	8	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	no	pass	ok;peak_is_process_hwm;factory_state_count_unobservable	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2	16818176	17313792	17317888	0.454857	4+4	0.005876	ok	2	3
```

All acceptance checkpoints agree. Partial runs release two iterators, except
`false`, which releases one; each separate ownership run releases three.
`true` under TAA has an initial singleton and an empty-set accepting sink,
explaining its two states versus the optimized reference's one. TAA may also
retain duplicate skeleton transitions from recursive visits; neither factory
nor fully enumerated size should be assumed minimal.

#### Operator coverage beyond the default list

The repeated `--formula` inputs below exercise next, weak until, release,
strong release, Boolean equivalence, negated until and a worker-shaped
implication/next/release nesting. The latter is an AP-renamed clause shape from
`tests/ltl/realizable/OneCounterGuiA9.ltl:1`, not a full worker run. That input
contains `G(disable -> X(enable R !pressed))`; other supplied formulas already
cover its Boolean operators. The simple PSL spelling `{a}<>-> b` normalizes to
a Boolean formula and is therefore not evidence for general PSL support.

One invocation passed the eight inputs using repeated `--formula` flags,
exit **0**. The following projection preserves measured state/edge counts and
check outcomes for every provider (times remain in the temporary run TSV):

| Formula | Provider | Full states | Full edges | Acceptance | Language | Acceptance changed | Ownership |
|---|---|---:|---:|---|---|---|---|
| X a | translator | 3 | 3 | 0:t | pass | no | ok |
| X a | taa | 3 | 3 | 0:t | pass | no | ok |
| X a | product | 3 | 3 | 0:t | pass | no | ok |
| a W b | translator | 2 | 3 | 0:t | pass | no | ok |
| a W b | taa | 2 | 3 | 0:t | pass | no | ok |
| a W b | product | 2 | 3 | 0:t | pass | no | ok |
| a R b | translator | 2 | 3 | 0:t | pass | no | ok |
| a R b | taa | 2 | 3 | 0:t | pass | no | ok |
| a R b | product | 2 | 3 | 0:t | pass | no | ok |
| a M b | translator | 2 | 3 | 1:Inf(0) | pass | no | ok |
| a M b | taa | 2 | 3 | 1:Inf(0) | pass | no | ok |
| a M b | product | 2 | 3 | 2:Inf(0)&Inf(1) | pass | no | ok |
| a <-> b | translator | 2 | 2 | 0:t | pass | no | ok |
| a <-> b | taa | 2 | 2 | 0:t | pass | no | ok |
| a <-> b | product | 2 | 2 | 0:t | pass | no | ok |
| !(a U b) | translator | 2 | 3 | 0:t | pass | no | ok |
| !(a U b) | taa | 2 | 3 | 0:t | pass | no | ok |
| !(a U b) | product | 2 | 3 | 0:t | pass | no | ok |
| G(a -> X(b R !c)) | translator | 2 | 4 | 0:t | pass | no | ok |
| G(a -> X(b R !c)) | taa | 2 | 4 | 0:t | pass | no | ok |
| G(a -> X(b R !c)) | product | 2 | 4 | 0:t | pass | no | ok |
| {a}<>-> b | translator | 2 | 2 | 0:t | pass | no | ok |
| {a}<>-> b | taa | 2 | 2 | 0:t | pass | no | ok |
| {a}<>-> b | product | 2 | 2 | 0:t | pass | no | ok |

#### Unsupported provider input is a finding

Command: `/tmp/otf-p0/src/acacia-spot-otf-probe --formula '{a[*];b}<>-> c'`.
Exit **2**. TAA throws `std::runtime_error("unimplemented")` during all three
factories, while translator and product complete and pass. No crash occurred
on the required eight cases. This unsupported PSL case was preserved rather
than filtered from the results.

```tsv
formula	provider	factory_ms	factory_peak_bytes	states_after_factory	init_only_ms	first_row_ms	first_row_edges	one_destination_row_ms	full_enumeration_ms	full_enumeration_states	full_enumeration_edges	acceptance_before	acceptance_after	acceptance_changed	language_check	status	acceptance_after_init	acceptance_after_first_row	acceptance_after_destination	full_acceptance_before	materialized_acceptance	one_destination_row_edges	factory_baseline_peak_bytes	factory_rss_before_bytes	factory_rss_after_bytes	factor_factory_ms	factor_states	full_factory_ms	ownership_check	consumer_iterators_released	ownership_iterators_released
{a[*];b}<>-> c	translator	0.208277	16703488	2	0.000213	0.002019	2	0.000645	0.001153	2	3	1:Inf(0)	1:Inf(0)	no	pass	ok;peak_is_process_hwm;materialization_is_graph_copy	1:Inf(0)	1:Inf(0)	1:Inf(0)	1:Inf(0)	1:Inf(0)	1	13414400	14061568	16949248	NA	NA	0.241896	ok	2	3
{a[*];b}<>-> c	taa	NA	NA	NA	NA	NA	NA	NA	NA	NA	NA	NA	NA	unknown	NA	partial:unimplemented@factory;full:unimplemented@factory;ownership:unimplemented@factory;acceptance_unknown;language_NA;peak_is_process_hwm;factory_state_count_unobservable	NA	NA	NA	NA	NA	NA	13422592	14073856	NA	NA	NA	NA	unimplemented@factory	NA	NA
{a[*];b}<>-> c	product	0.006272	16973824	NA	0.000289	0.003156	2	0.001085	0.004552	2	3	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	no	pass	ok;peak_is_process_hwm;factory_state_count_unobservable	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	1	16711680	17219584	17223680	0.268936	2+2	0.006255	ok	2	3
```

#### Factory cap and continuation check

Generated `F p0 & ... & F p19`, passed it with `--provider taa
--timeout-seconds 1`, followed by a second `--formula true`. Exit **2**. Each
large-formula phase ended with `SIGALRM` (14) at `factory`; the subsequent
`true` case passed. This directly demonstrates substantial work before any
consumer successor request, and verifies that an unusable case does not erase
later evidence. Missing factory-return values remain `NA`.

```tsv
formula	provider	factory_ms	factory_peak_bytes	states_after_factory	init_only_ms	first_row_ms	first_row_edges	one_destination_row_ms	full_enumeration_ms	full_enumeration_states	full_enumeration_edges	acceptance_before	acceptance_after	acceptance_changed	language_check	status	acceptance_after_init	acceptance_after_first_row	acceptance_after_destination	full_acceptance_before	materialized_acceptance	one_destination_row_edges	factory_baseline_peak_bytes	factory_rss_before_bytes	factory_rss_after_bytes	factor_factory_ms	factor_states	full_factory_ms	ownership_check	consumer_iterators_released	ownership_iterators_released
F p0 & F p1 & F p2 & F p3 & F p4 & F p5 & F p6 & F p7 & F p8 & F p9 & F p10 & F p11 & F p12 & F p13 & F p14 & F p15 & F p16 & F p17 & F p18 & F p19	taa	NA	NA	NA	NA	NA	NA	NA	NA	NA	NA	NA	NA	unknown	NA	partial:signal_14@factory;full:signal_14@factory;ownership:signal_14@factory;acceptance_unknown;language_NA;peak_is_process_hwm;factory_state_count_unobservable	NA	NA	NA	NA	NA	NA	13238272	13918208	NA	NA	NA	NA	signal_14@factory	NA	NA
true	taa	0.025066	14237696	NA	0.000668	0.008731	1	0.001332	0.020158	2	2	0:t	0:t	no	pass	ok;peak_is_process_hwm;factory_state_count_unobservable	0:t	0:t	0:t	0:t	0:t	1	13045760	13778944	14512128	NA	NA	0.018195	ok	2	3
```

### 5. Options and next-stage boundaries

#### a. Accept — public TAA/TGBA candidate (verdict A)

Accept direct `ltl_to_taa(..., false)` for the P5 LTL experiment. Source proves
deferred reachable set-state combination, and the required plus additional LTL
checks pass language/acceptance/ownership validation. Retain the eager
alternating skeleton and charge its cost to both C4 and C5. A future complete-row
cache can avoid repeated row construction, but no cache or adapter was built
in P0. The PSL failure and exponential-looking conjunction work are explicit
admission limits, not reasons to hide successful LTL evidence.

Before an ordinary bounded-rank consumer can use this TGBA, P5 must validate its
Büchi cursor normalization, including zero sets, multiple sets, marks that
discharge several obligations on one edge, initial cursor/rank, acceptance on
transitions versus destinations, and language equivalence with eager
degeneralization. P2's state/row ownership and successor oracle tests and P6's
same-construction eager/on-demand comparisons remain necessary. Test larger
and nested worker formulas before making any corpus coverage claim.

#### b. Reject as the formula-generation choice; accept product as a test provider

`twa_product` is a real public lazy provider and passes all small-factor tests.
Accept it for generic-interface tests and experiments whose proposed saving is
specifically deferred **product** construction. Reject using those timings to
claim deferred translation of an arbitrary formula: its factors were eagerly
translated and their cost is separately visible.

The additional public FM explorer is also rejected as a ready substitute for
the fixed-acceptance `twa` view. Its missing contract is supported acceptance
inventory freezing/positive-Inf row semantics before consumption, not merely
a public successor method. No verdict-B formula provider was established by
executing it.

#### c. Reject — an internal adapter as a prerequisite to this sprint

The tested public TAA route means P0 does not require opening Spot internals,
duplicating FM translation or proposing verdict C as the only feasible path.
If TAA later fails the generation experiment, the public FM explorer at
`2ae6210237` merits a separate acceptance-freezing design study. That study must
name the retained eager normalization/canonicalization work, preserve AP and
promise registrations, and test acceptance stability and language equivalence
under interrupted exploration. Its feasibility is not established here.

#### d. Reject — “no suitable public provider” for this P0 evidence

The executable LTL TAA results and source-backed deferral justify A rather
than D. D would still be a successful audit outcome if subsequent required
admission tests eliminate the candidate. P1–P4 do not depend on which
generation provider survives. Do not make their implementation conditional on
a translation speedup, add a new TLSF tableau, or migrate BDD libraries.

### 6. Validation and scope

Executed:

```sh
python3 scripts/acacia-config.py validate
PKG_CONFIG_PATH=/usr/local/lib/pkgconfig meson setup /tmp/otf-p0 -Dbuild_research_tools=true
meson compile -C /tmp/otf-p0
/tmp/otf-p0/src/acacia-spot-otf-probe
meson test -C build --suite=unit --suite=version
```

Setup and the final full research-tools compile succeeded. The first compile
exposed two probe C++ errors (const parser error-formatting and an initializer
list's pointer constness); both were corrected before any reported probe run.
The probe builds with TLSF frontend and diagnostics **disabled**, confirming
that its Meson target is not gated by either option. The required tests report:

```text
Ok:                33
Fail:              0
```

This includes `version-output`, not just unit suites. Additional probe
invocations are recorded above. CLI checks returned 0 for `--help` and 1 with
the program-name prefix for missing `--formula`, zero `--timeout-seconds`, and
an unknown provider. No benchmark suites were run and no commits were made.

The change consists only of `src/research/spot_otf_probe.cc`, this audit, and
the target registration in `src/meson.build`. No solver, translator, adapter,
new test file or sprint-report file was introduced. `otf.md` was an existing
untracked specification and was not changed.

### Bottom line

The alternating skeleton is eager; reachable TGBA set-state/row construction
is lazy at row acquisition. The measured factory timeout prevents treating
this as cost-free formula generation. Fixed acceptance and language equivalence
hold for the tested LTL cases; PSL repetition is outside the observed working
scope. Use the public route for P5 research with a validated ordinary Büchi
view, retain identical eager controls in P6, and proceed independently with
P1–P4. No production performance claim follows from this audit.

**Verdict A — the public TAA/TGBA provider works on the audited LTL cases and
defers relevant reachable construction.**

---

## Acacia: Spot-first on-the-fly automata and symbolic-letter OTFUR

<sub>Was `otf.md`. The handoff specification this sprint was given, dated 2026-09-05. Superseded by the completion record above; kept because it was never committed anywhere and it is what the work was measured against.</sub>

### Revised implementation handoff — 5 September 2026

**Primary repository:** `gaperez64/acacia-bonsai`  
**Scope:** one Spot/BuDDy research sprint, with independently gated production candidates.  
**Supersedes:** `acacia_demand_driven_symbolic_otfur_handoff.md` for the coming sprint.

> Keep the existing TLSF frontend, Spot formulas, Spot automaton interfaces, and Spot/BuDDy guards. First measure demand, then implement symbolic letter selection over the existing automata, then test a genuinely lazy **Spot-provided** construction where available. Do not write a general LTL translator in tlsf-tools or migrate to OxiDD in this sprint.

---

## 0. Decision and scope

### 0.1 What changes from the previous handoff

| Previous task | Revised task |
|---|---|
| Define a new backend-neutral automaton framework | Use `spot::const_twa_ptr`, `spot::state`, successor iterators, and `bdd`, behind one small Acacia row-cache adapter. |
| Build an owned TLSF formula DAG and general tableau translator | Remove from this sprint. Consume the existing effective worker formula as `spot::formula`. |
| Implement a tlsf-tools automaton transition stream | Remove. Audit and try Spot's public TAA/TGBA path first. |
| Add and compare an OxiDD backend | Defer entirely. No new OxiDD dependency, binding, build, or concurrency work. |
| Treat graph wrapping as a streaming speedup | Call it **streamed access to an already constructed graph**. It cannot save translation time. |
| Infer translation savings directly from rows visited | Treat row utilization as evidence about demand, not a measurement of avoidable translation work. |
| Change the existing OTFUR input type globally | Keep the existing solver and portfolio unchanged. Add an experimental row-consuming forward backend. |
| Assume any `twa` can be given to bounded-rank OTFUR | Admit only a validated ordinary Büchi view, with an explicit increment convention and initial rank. |
| Defer inspection of Spot internals until after a new provider | Audit Spot at the start. A public provider may already avoid the expensive reachable-state materialization. |

The symbolic-letter algorithms and correctness boundaries from the previous handoff remain the basis. This document restates the necessary parts so implementation does not depend on an earlier conversation.

### 0.2 The two hypotheses in scope

**H-Letters.** Keeping sets of inputs/outputs as BDDs avoids explicit action-table and controller-node multiplication. This is the better-supported hypothesis, given the measured benefit of lazy action evaluation.

**H-Generation.** A game proof may require only part of the automaton construction. Spot's on-the-fly interfaces may let Acacia avoid constructing the rest without introducing a new automata library.

These are independent. High automaton-row utilization does not refute H-Letters. A negative output-only experiment does not refute the benefit of eliminating explicit input classes. A weak TAA construction does not refute every possible lazy translator.

### 0.3 Explicitly deferred

Do not implement:

- a new general-LTL-to-Büchi translator in Acacia or tlsf-tools;
- an OxiDD migration or comparison;
- a Spot-free formula/automaton framework;
- changes to tlsf-tools' AST, semantics, decomposition, or expansion;
- changes to Posets data structures;
- deterministic parity translation or runtime delegation to another solver;
- a BDD representing the complete rank-state winning region;
- a new K schedule, more portfolio children, or multithreaded BDD operations;
- simultaneous experiments with conditional covering, new losing-state generalization, and the symbolic alphabet;
- arbitrary individual-edge streaming from unfinished rows;
- a learned exploration policy;
- automatic SCC/Boolean-rank inference from a partially explored automaton.

A small Spot-compatible adapter, including a local generalized-Büchi-to-Büchi cursor wrapper if necessary, is in scope. Reimplementing LTL translation is not.

### 0.4 Deliverable order

```text
P0  Freeze versions; audit Spot's actual installed on-the-fly machinery
P1  Measure row/support demand in current OTFUR without changing decisions
P2  Implement and test the Spot row cache and exact rank successor
P3  Implement exact BuDDy letter queries on frozen automata
P4  Implement guarded-input symbolic-letter OTFUR on frozen automata
    -------- first milestone: independent empirical decision --------
P5  Admit a public Spot lazy provider; add Büchi wrapper if needed
P6  Compare that same provider eagerly and on demand
P7  Integrate only successful candidates as default-off backend/arm options
```

P0 may find a public lazy provider immediately. Its small API smoke tests can run in parallel with P1–P4. Do not postpone that discovery, and do not require a new translator as the price of finishing the first milestone.

---

## 1. Evidence and version discipline

### 1.1 Known Acacia snapshot

The earlier handoff and the checked repository interfaces are located at:

```text
Acacia:     509c95ef706125060909bf9a63b2223b57267588  (PR #137 stack)
Posets:     139e14336b7a1f0bc064022e587ea4e1b9a81427
tlsf-tools: b42d5ef4a680252e04820ac7f073f5d786a43f7c
```

If implementation starts on a newer stack or merged tree, freeze that tree and rebuild all paired controls. Do not revert working changes merely to reproduce these SHAs.

The previously reported 1,057 versus 1,257 full-corpus scores belong to a particular 17-second campaign. They are context, not a fresh baseline measurement for this sprint. The task is to improve coverage of the official 2026 set, not to assume every historical mechanism label remains current. [A1]

### 1.2 Verified public Spot facts

1. `spot::twa` exposes the on-the-fly interface through `get_init_state()`, `succ_iter()`/`succ()`, and successor guards and acceptance marks. A `twa_graph` can be read through that interface even though its graph has already been built. [S1]
2. State equality is semantic (`compare`/`hash`), not raw pointer equality. Returned states must be released through `state::destroy()`. Iterators are returned with `release_iter()` or managed by the iterable wrapper. [S1, S2]
3. `spot::translator::run()` and `ltl_to_tgba_fm()` expose explicit `twa_graph_ptr` results. Acacia currently uses that eager boundary. [S3, A2]
4. `spot::twa_product` is explicitly documented as a lazy product. Lazy product construction is not by itself lazy translation of an arbitrary formula. [S4]
5. `ltl_to_taa()` returns `taa_tgba_formula_ptr`; `taa_tgba` is documented as a self-loop alternating representation **seen as a TGBA** through the `twa` interface. It is a more concrete reuse candidate than “perhaps an alternating container exists.” Its actual construction and successor costs must be checked in the linked source version. [S5, S6]
6. The documented `degeneralize()`/`degeneralize_tba()` interfaces take graph pointers and return graph pointers. Do not assume they preserve lazy generation merely because their input originated in a lazy object. [S7]

**Audit limitation of this handoff:** the public interfaces were checked, but the exact installed `ltl2taa.cc`/`taatgba.cc` implementation was not compiled or benchmarked here. P0 must establish what its factory constructs eagerly and what its iterator generates on request. Do not turn the TAA candidate into an unverified assertion of a zero-cost lazy LTL translator.

Public documentation currently spans different Spot versions. The website announces 2.16, while some API/source pages display 2.15.1 or older versions. Pin the source that actually built the Acacia binary. Do not upgrade Spot as part of the same A/B comparison. [S8]

### 1.3 Freeze before editing

Record in `benchmarking/SPOT-STREAMING-OTFUR-SPRINT.md`:

```text
Acacia and submodule SHAs
Spot version string and source revision/archive hash
BuDDy/Spot build identity and acceptance-set capacity
compiler, build type, LTO, enabled sanitizers
complete normalized Acacia preset
binary SHA-256
worker arms and all default-off feature flags
K schedule and maximum K
corpus/source-map hashes
CPU-affinity and cgroup limits
```

Useful initial commands:

```sh
git rev-parse HEAD
git submodule status
python3 scripts/acacia-config.py validate
pkg-config --modversion libspot
ltl2tgba --version
```

Use the actual project paths and module names from the checkout if renamed. Do not assume `master` or online `next` is the library linked by the benchmark binary.

### 1.4 Existing files to read, not duplicate

```text
src/solver/create_automaton.hh
src/solver/transition_payload.hh
src/solver/forward_reachable_safety.hh
src/solver/forward_game_nodes.hh
src/solver/forward_k_bounded_safety_aut.hh
src/solver/minimal_losing_antichain.hh
src/solver/certificate_verifier.hh
src/solver/solve_game_impl.hh
src/solver/solver_invoker.cc
src/actioners/standard.hh
src/ios_precomputers/mona.hh
src/ios_precomputers/semantic_mona.hh
src/boolean_states/forward_saturation.hh
src/solver/game_backend.hh
src/portfolio_arm.hh
benchmarking/POST-PR125-COVERAGE-SPRINT.md
benchmarking/FORWARD-DATA-STRUCTURES-REVIEW.md
config/acacia-options.json
config/acacia-presets.json
```

The current rank update counts destination acceptance in the state-based path and uses an explicit increment payload in the transition-acceptance path. The generic Spot iterator exposes transition marks. Those are not interchangeable conventions without normalization. [A2, A3]

---

## 2. Controls: separate five changes that could otherwise be confused

Use the same Spot build and BuDDy throughout.

| ID | Automaton | Rank domain | Letter/game operation | Interpretation |
|---|---|---|---|---|
| C0 | Current optimized Spot graph | Current dense mixed domain | Current semantic-MONA and OTFUR | Production reference |
| C1 | Exactly C0's graph | Exactly C0's domain | Shadow support-demand instrumentation only | No behavioral change; not streaming savings |
| C2 | Exactly C0's graph via row cache | Exactly C0's domain | Explicit-letter replay or output-only pilot | Adapter/oracle correctness control |
| C3 | Exactly C0's graph via row cache | Exactly C0's domain | Guarded-input symbolic OTFUR | Effect of symbolic letters |
| C3s | Exactly C0's graph | Sparse version of the same domain | Exactly C3 | Rank-storage ablation |
| C4 | Selected Spot public provider, same Büchi wrapper, fully enumerated | All-numeric sparse ranks | Same guarded OTFUR | Eager control for the lazy provider |
| C5 | Exactly C4's provider and wrapper, on demand | Exactly C4's rank domain | Same guarded OTFUR | Genuine reachable-automaton streaming |
| C6, optional | C4 materialized then globally postprocessed | Its recorded resulting domain | Same solver | Cost/benefit of global preprocessing, separately named |

Required comparisons:

```text
C0 vs C1       instrumentation overhead and demand traces
C0 vs C3       symbolic-letter effect on the SAME preprocessed graph
C3 vs C3s      dense/sparse representation only
C4 vs C5       laziness of the SAME automaton construction
C3 vs C5       end-to-end alternative; several factors changed
C4 vs C6       global preprocessing tradeoff, NOT a streaming comparison
```

Do not label C0 versus C5 “the benefit of streaming.” The automaton construction, acceptance layout, and preprocessing may all differ.

Keep exploration order deterministic where possible. Use a fixed AP order and a deterministic satisfying-valuation routine. BDD node IDs, allocator addresses, or hash-table iteration must not choose the search policy.

---

## 3. P0 — audit Spot first, with executable probes

### 3.1 Output

Create:

```text
benchmarking/SPOT-OTF-API-AUDIT.md
src/research/spot_otf_probe.cc
```

Do this before writing a formula-state generator.

### 3.2 Resolve these symbols in the linked source

| Entry point/type | Question |
|---|---|
| `translator::run`, `ltl_to_tgba_fm` | Which worklist materializes the graph? Is a supported row-level API exposed, or only internal implementation detail? |
| `ltl_to_taa` | What is constructed before the function returns? What input operators are supported? |
| `taa_tgba::get_init_state`, `succ_iter`, `taa_succ_iterator` | Does TGBA-state/successor combination happen on demand? What eager skeleton is retained? |
| `twa_product` / `otf_product` | Confirm a library lazy provider for adapter tests; inspect acceptance composition. |
| `copy` / `make_twa_graph` | Which exact operation deliberately enumerates a `twa` for the eager control? |
| `degeneralize`, `degeneralize_tba` | Does calling it force graph materialization? |
| `twa` state and iterator helpers | Ownership, canonical-state helper, BDD registration lifetime. |

Write source paths, source revision, function names, and local line ranges into the audit. Do not rely on stale online line numbers.

### 3.3 Probe sequence

For small formulas such as:

```text
true
false
F a
G a
GF a
GF a & GF b
G(a -> F b)
(a U b) | G c
```

and supported operators from actual worker formulas:

1. Construct the usual Spot graph reference.
2. Construct `ltl_to_taa` directly, where available, without going through `translator::run()` or postprocessing.
3. Record factory time and memory before any successor request.
4. Request the initial state only.
5. Enumerate exactly its complete outgoing row.
6. Request one destination row only.
7. In a separate capped run, fully enumerate the provider to a graph.
8. Record acceptance before and after each step; it must not acquire new acceptance sets after solving begins.
9. Check the language of the materialized provider against the formula/reference on small cases.
10. Run the same iterator/ownership tests on a genuine `twa_product` of small factors.

A wrapper that counts calls to `succ_iter()` proves what the consumer requested, not what the factory built internally. Report factory cost separately. Inspect source or add a temporary local profiling counter if the distinction cannot otherwise be measured.

### 3.4 Decision hierarchy

**A — public TAA/TGBA provider works and defers relevant reachable construction.** Use it in P5. Its alternating implementation is hidden behind the validated TGBA view; do not feed raw alternating clauses to Acacia.

**B — another public Spot provider already fits.** Use it with a documented language/acceptance contract. A lazy product of eager factors is an experiment in deferred product construction, not arbitrary lazy formula translation.

**C — only a small internal Spot adapter is feasible.** Write an explicit design proposal naming the supported API missing, the source revision, the eager work retained, and the required tests. Keep the experiment in Acacia research code or a small local Spot patch. Do not duplicate the entire translator or expose unstable internals throughout Acacia.

**D — no suitable provider at the pinned version.** Finish P1–P4 and the source audit. Report genuine lazy formula generation as unavailable in this sprint. Do not silently substitute a new tlsf-tools tableau construction or OxiDD migration.

P0 passes as an audit even under outcome D. The symbolic-letter experiment does not require a lazy translator.

---

## 4. P1 — measure actual demand without changing the current solver

### 4.1 Shadow-demand instrumentation

At every rank state for which current OTFUR actually computes a successor or checks a certificate, record its active automaton support:

\[
\operatorname{supp}(r)=\{q:r(q)\ne-1\}.
\]

Treat those sources as the rows a complete-row consumer would have requested.

Separate:

```text
search support demand
verification support demand
union over one K
union over all K values in this worker
```

The current actioner may scan dense arrays even for inactive coordinates. Do not count that dense loop as semantic demand for every automaton row.

Do not modify action order, rank order, the picker, the queue policy, or the result.

### 4.2 Metrics

```text
worker ID and transform
backend and exact options
formula digest
translation/preprocessing/action-construction/solve/verification times
total states and edges in frozen graph
unique active source rows requested by search
unique active source rows requested by verification
union rows requested over all K
edges in those complete rows
active support median/p95/max
semantic action profiles decoded
action profiles actually tried (with stable IDs)
controller nodes / environment nodes
forward limit reason
```

Compute, on completed eager graphs:

\[
\rho_Q=\frac{\text{distinct requested source rows}}{|Q|},
\qquad
\rho_E=\frac{\text{edges in requested source rows}}{|E|}.
\]

Use the union across K for an end-to-end worker estimate; a small ratio at one losing bound may become nearly one later.

### 4.3 Do not overinterpret utilization

Low utilization of an **optimized** Spot graph does not show that its unused states could have been omitted while preserving the same minimization/SCC results. It motivates C4/C5, not a claimed translation speedup.

High utilization does not eliminate possible benefits from lower peak memory, early refutations, or lazy product construction under a different provider.

When eager translation times out, `|Q|` and `|E|` are unknown. Do not report a zero numerator or a fabricated utilization ratio. Keep a separate translation-failure cohort.

### 4.4 Current coverage set

Reuse the current full-corpus and family-provenance scripts. Refresh, or join available matched runs to obtain:

```text
Acacia-only
external-solver-only (annotation)
both
neither
```

Select discovery targets from Acacia's own unresolved set and phase data, not exclusively the external-solver-only set.

A proposed discovery cohort is 24 jobs from at least six families, containing:

- eight jobs with large input/controller multiplication or action preparation;
- eight jobs with significant translation/pre-game work or apparently low support utilization;
- eight controls, including dense-support and fast-solving workers.

Use exact worker IDs and a solved neighboring parameter where available. Keep a separate held-out cohort from different families. Historical `robot_grid`, `lift_gr1`, `AllLights`, `collector`, and `Morning` jobs are seeds to remeasure, not permanent routing rules.

### P1 gate

Instrumentation must preserve completed results and search decisions, apart from timing.

Record which hypothesis each target can test. Proceed with symbolic letters where alphabet expansion is material. Admit the genuine lazy-provider experiment when the audit exposes an inexpensive route and either row underutilization or pre-game cost gives a concrete reason to test it. Do not require an arbitrary 5–30% ratio on every family.

---

## 5. P2 — a small Spot row adapter, not a new automata framework

### 5.1 Files

Suggested additions:

```text
src/solver/spot_rows.hh
src/solver/spot_state_ids.hh
src/research/spot_rows_replay.cc
tests/spot_rows_test.cc
```

Resolve existing helpers first. The names describe responsibilities; do not create duplicates of equivalent code already in the tree.

### 5.2 Provider ownership

The row adapter owns:

```text
spot::const_twa_ptr provider
provider's BDD dictionary/AP registrations for the full job lifetime
canonical Spot-state arena
stable Acacia StateIds
one complete-row cache per StateId
explicit acceptance/increment mode
```

Use Spot's `state_unicity_table`/`state_map` or equivalent helpers following their precise ownership contracts. Interning consumes an owned state object; a duplicate is destroyed exactly once. Raw pointer equality is allowed only after canonicalization, not as the initial equivalence test.

Never use `format_state()` as a uniqueness key. It is a display API.

Keep the provider alive until all state objects, rows, guards, and iterators that depend on it have been released. Arrange member destruction order explicitly.

### 5.3 Complete row types

Conceptually:

```cpp
using StateId = std::uint32_t;

struct SpotEdge {
    StateId destination;
    bdd condition;                       // AP guard, owned BuDDy handle
    spot::acc_cond::mark_t acceptance;   // preserved from source iterator
};

struct RankEdge {
    StateId destination;
    bdd condition;
    bool increment;                     // normalized rank payload
};

struct CompleteRankRow {
    std::vector<RankEdge> edges;
    RowDigest digest;
};
```

Cache states distinguish:

```text
not_requested
building
complete
failed_or_resource_limited
```

Only `complete` returns a usable row. A complete row may be empty. That is different from an unrequested or interrupted row.

Use stable storage (`deque`, owning pointers, or equivalent) so interning a new destination cannot invalidate a row reference currently being built.

### 5.4 Consume the generic iterator correctly

Conceptual procedure:

```text
row(q):
    if cached complete: return it
    start temporary row
    acquire provider.succ_iter(canonical_state(q)) under RAII
    call first()
    while not done:
        copy cond and acc
        obtain owned destination using dst()
        intern destination by semantic state equality
        append normalized edge
        advance iterator
    release iterator through provider.release_iter()
    atomically publish complete temporary row
    return published row
```

The `succ()` iterable can replace explicit iterator management when its exact pinned API is convenient. In either form, release destination objects according to Spot's contract.

Do not recursively request destination rows just because destination IDs have been discovered.

If a row exceeds a budget, return `RESOURCE_LIMIT`; no partial row participates in rank arithmetic or a certificate. Newly allocated destination IDs may remain cached, but the unfinished source row remains unusable.

### 5.5 Two increment modes; never infer one from the other

#### Frozen Acacia mode

The input is the exact graph after the existing worker transformations and preprocessing, at the current action-construction boundary.

Preserve:

```text
existing graph-coordinate mapping
existing initial rank
existing Boolean/numeric safe caps
actual transition_payload increment semantics
```

Normalize increment on row consumption using the same payload rule as `actioners::standard`. In the current state-based route this requires the destination's acceptance metadata from the already complete graph, not the source iterator's outgoing mark. [A3]

Keep graph-specific ID/acceptance lookup inside this frozen adapter. It is allowed to use the known graph for metadata; do not advertise this mode as independent of eager preprocessing.

#### Generic transition-Büchi mode

The provider exposes ordinary single-set transition Büchi acceptance with a fixed acceptance condition. The increment is the edge's membership in that set.

Start with initial rank 0 at the provider's initial state and -1 elsewhere. Use all-numeric ranks. The acceptance-cursor wrapper in P5 exposes exactly this contract.

Label both modes in every fixture and result. Identical language alone is not enough to equate their fixed-K games.

### 5.6 Adapter restrictions

The admitted automaton view must be nonalternating, even if its internal construction uses alternation. Every iterator result denotes one ordinary nondeterministic successor of the bad-language automaton.

Acacia subsequently combines **all enabled successors** using a maximum, under its universal co-Büchi interpretation. That does not authorize feeding raw alternating transitions into the same rule.

Do not use Spot's `prop_universal()` name as an informal “this is our universal co-Büchi automaton” flag. Check the actual representation contract and the appropriate pinned graph/provider API.

Do not accept arbitrary `Fin`, Rabin, Streett, or parity acceptance by treating every nonempty edge mark as one rejecting visit.

### 5.7 Direct rank evaluator

For every complete valuation `(u,c)`:

\[
\tau_{u,c}(r)(q)=\max\left(\{-1\}\cup
\left\{\min(K,r(p)+\mu):r(p)\ne-1,
(p,\gamma,\mu,q)\text{ enabled by }(u,c)\right\}\right).
\]

Before computing it, complete every active source row. A missing outgoing edge under a letter contributes nothing; an unexpanded row is not a missing edge.

Check safety against the existing frozen safe caps, or K-1 at every coordinate in generic all-numeric mode.

### 5.8 Tests and first control

On tiny frozen graphs, enumerate all safe ranks and all AP valuations and compare:

```text
existing actioner successor
row-based direct evaluator successor
```

Test nonzero initial IDs, empty AP blocks, empty support, incomplete automata, overlapping guards, multiple simultaneous branches, mixed ranks, parallel increments 0/1, and saturation at K.

Add lifetime tests with fresh state allocations for equal values, deliberate hash collisions, early iterator exit, a row exception, and cache destruction. Run ASan/UBSan where supported.

A genuine lazy-product fixture must produce the same row/transition language as its fully materialized copy. This tests the generic interface without requiring a lazy formula translator.

---

## 6. P3 — exact rank-local BuDDy queries

### 6.1 Files and scope

```text
src/solver/spot_letter_oracle.hh
src/research/spot_letter_oracle_replay.cc
tests/spot_letter_oracle_test.cc
```

Use `bdd`, Spot's dictionary, and the linked BuDDy operations directly behind one small helper. Do not build a general multi-library BDD abstraction now.

Required helper operations:

```text
Boolean AND/OR/NOT
restriction by a total input/output valuation
existential/universal abstraction over explicitly registered AP variables
support check
satisfiability and tautology check
node-count metrics
stable total model selection
```

For stable model selection, visit APs in a fixed recorded order, try false first, keep it when the restricted function remains satisfiable, otherwise choose true. Fill don't-cares explicitly. Never use a process-global BDD node ID as a priority.

The AP partition is the already transformed worker partition. Preserve the current Mealy adaptation, unreal transformations, and formula polarity exactly. Do not negate the bad formula a second time.

### 6.2 Threshold predicates

For fixed r, complete every active row and collect all possible destinations `D(r)`.

For 0 <= h <= K:

\[
T^r_{q,h}(u,c)=
\bigvee_{\substack{p\in\operatorname{supp}(r),\ (p,\gamma,\mu,q)\in E\\r(p)+\mu\ge h}}
\gamma(u,c).
\]

This equals `[tau(r,u,c)(q) >= h]`.

- h <= -1: true.
- h > K: false under saturated semantics.
- q absent from D(r): false for h >= 0.

Compute thresholds on demand. No rank variables are introduced into the BDD manager.

### 6.3 Unsafe, losing-cone, upper-box, and exact-equality preimages

Let `cap(q)` be K-1 for numeric coordinates and 0 for frozen Boolean coordinates.

\[
\operatorname{Unsafe}_r=\bigvee_{q\in D(r)}T^r_{q,\operatorname{cap}(q)+1}.
\]

For an earlier proved-losing generator ell:

\[
\operatorname{UpPre}(r,\ell)=\bigwedge_{q\in\operatorname{supp}(\ell)}T^r_{q,\ell(q)}.
\]

For an upper generator g, with absent coordinates equal to -1:

\[
\operatorname{DownPre}(r,g)=
\bigwedge_{q\in D(r)\cup\operatorname{supp}(g)}\neg T^r_{q,g(q)+1}.
\]

The coordinate union is mandatory. A successor may activate a destination absent from g.

For exact successor s, conjoin over `D(r) union support(s)`:

```text
s(q) = -1:       NOT T(q,0)
0 <= s(q) < K:   T(q,s(q)) AND NOT T(q,s(q)+1)
s(q) = K:        T(q,K)
```

Call the result `Eq(r,s)`.

Then:

```text
Bad(r,L)  = Unsafe(r) OR OR_{ell in L} UpPre(r,ell)
Good(r,G) = OR_{g in G} DownPre(r,g)
```

### 6.4 Exact queries

Fixed-rank losing test:

\[
\exists u\;\forall c\;\operatorname{Bad}(r,L)(u,c).
\]

Candidate winning-invariant test:

\[
r_{\mathrm{init}}\in\downarrow G,\quad G\text{ safe},\quad
\forall g\in G\;\forall u\;\exists c\;\operatorname{Good}(g,G)(u,c).
\]

Not known bad means **unresolved**, not winning.

Do not remove input valuations using an assumption formula outside the original monitor. Do not add a new legal-output filter in this ablation. All semantic restrictions must be those already represented by the worker game.

### 6.5 Cache lifetime

- Provider rows/guards: immutable within a job, reusable across K if the provider is unchanged.
- Thresholds and exact-rank queries: keyed by provider identity, rank identity, K, coordinate mode.
- `Bad`: additionally keyed by losing-set epoch.
- `Good`: additionally keyed by candidate-set epoch.

A provider replacement or coordinate-mode change clears the affected caches. Do not run BuDDy from concurrent worker threads or fork while relying on unverified BDD thread state.

Handle the linked BuDDy error mechanism explicitly. Allocation failure, stack/resource exhaustion, or aborted query cannot be returned as false or true. In a worker where errors are fatal, the parent must see an inconclusive/error exit, not a decisive verdict.

### 6.6 Correctness gate

For all tiny fixture ranks and letters, compare every query to direct evaluation. Include:

```text
inactive source with accepting edge
one overflowing branch among several enabled branches
no enabled transitions
different outputs needed for different inputs
absent upper-bound coordinate activated by a successor
empty input set / empty output set
parallel edges with different increments
mixed Boolean-tail overflow fixture
resource failure halfway through a query
```

At least 5,000 fixed-seed tiny games must agree on completed fixed-K answers with the existing exact forward/backward or finite-domain oracle. Testing a query returning UNKNOWN does not count as agreement on a verdict.

---

## 7. P4 — guarded-input OTFUR on existing Spot automata

### 7.1 Why not just change the type of the existing solver

Current OTFUR depends on finite action tables, a known dense dimension, and input-class nodes. Merely replacing `twa_graph_ptr` by `const_twa_ptr` does not remove those dependencies.

Keep it as the reference implementation. Add:

```text
src/solver/spot_guarded_forward_safety.hh
```

Reuse existing status, order, queue, losing-proof, and certificate infrastructure where it actually matches the new responsibilities.

### 7.2 Optional output-only pilot

A short pilot may retain explicit input classes while replacing output action enumeration with the new oracle.

Use only a certified partition. The current action-list merging does not by itself prove that its attached BDD guards form a complete partition with a uniform output witness.

Prefer pre-merge MONA input regions grouped by equality of their complete residual function over output variables and increment-labelled endpoints; OR their input guards. Require disjointness and union true. When such metadata is unavailable, run the pilot only on offline tiny total-input enumerations and proceed directly to guarded inputs.

Do not block the main experiment on rebuilding a globally explicit input partition: avoiding it is one of the intended benefits.

### 7.3 Data model

```cpp
struct GuardedChoice {
    bdd input_region;             // input variables only
    TotalOutputValuation output; // constant output over this region
    RankNodeId successor;        // exact rank successor throughout region
    bool active;
};

struct GuardedRankNode {
    Rank rank;
    bool active_rows_complete;
    bool losing;
    std::vector<GuardedChoice> choices;
    bdd covered_inputs;          // OR of active input regions
    std::vector<ChoiceRef> incoming;
    bool queued;
};
```

Initially retain dense ranks and the exact frozen coordinate domains. Sparse dynamic ranks are P5/P6 work, with a separate C3s comparison.

A constant output per region is intentional. General output functions/Skolemization are deferred. This representation is exact but can still require exponentially many guarded choices.

### 7.4 Expand one reached rank

```text
expand(r):
    if r already losing: return
    if unsafe(r): enqueue_loss(r, UNSAFE); return
    if an earlier losing ell <= r:
        enqueue_loss(r, SUBSUMPTION); return

    complete all active source rows
    if row/query budget fails: return RESOURCE_LIMIT

    deactivate any choices whose target has become losing
    covered = OR of remaining active input guards
    if covered == true: return

    missing = NOT covered
    u0 = stable total input satisfying missing
    bad_c = restrict(Bad(r,L), u0)

    if bad_c == true:
        enqueue_loss(r, input u0, earlier proof dependencies)
        return

    c0 = stable total output satisfying NOT bad_c
    s = direct_successor_from_complete_rows(r,u0,c0)
    assert safe(s)
    assert not already known losing(s)

    C = missing AND restrict_outputs(Eq(r,s), c0)
    assert support(C) subset of input APs
    assert C(u0)

    sid = intern_exact_rank(s)
    append active choice (C,c0,sid)
    append incoming dependency at sid
    covered = covered OR C

    fairly enqueue sid and, if necessary, r
```

An optional global `exists u forall c Bad` query can precede the witness selection. Time it separately; it is an optimization, not required for correctness.

### 7.5 When a selected target loses

1. Store an immutable losing proof and update the minimal losing antichain.
2. Deactivate every incoming choice that selected that target.
3. Recompute each affected source's covered-input BDD.
4. Reopen the uncovered region by re-enqueuing the source.
5. The next Bad query includes the newly proved losing cone, so the failed semantic successor is blocked, not merely its last sampled output valuation.

Do not conclude that a source loses because one selected target lost. A refutation requires a complete all-output proof for an input.

Keep the existing correct losing-subsumption behavior. Do not add a new dominance index or covering heuristic in this patch.

### 7.6 WIN condition and verification

An empty queue alone is not a certificate. Traverse the selected graph reachable from init and require:

```text
every reached rank is safe
all of its active source rows are complete
all active selected targets exist and are not losing
OR of input regions is true at each reached rank
no pending loss propagation or unresolved operation failure
```

Recompute, without trusting search caches:

\[
C(u)\Rightarrow[\tau_{u,c_0}(r)=s]
\]

for every guarded choice, and check full input coverage.

Also reduce the reachable ranks to maximal generators G and independently check the full `forall u exists c Good` invariant. The verifier may use different action witnesses, but it must recompute from rows rather than trust the search's flags.

In frozen small tests, check with exhaustive letters and the existing action-table verifier as an additional oracle. A verifier timeout/resource failure means UNKNOWN, not an unchecked WIN.

### 7.7 LOSE proof

A proof node records:

```text
rank r
witness total input u0
complete-row identities/digests
IDs of earlier losing generators used
```

Rebuild and check:

\[
\forall c:\operatorname{Unsafe}_r(u_0,c)
\lor\bigvee_{\ell\text{ previously proved}}\operatorname{UpPre}(r,\ell)(u_0,c).
\]

Subsumption nodes additionally certify ell <= r. Proof references must be acyclic. Budget exhaustion is never a losing reason.

### 7.8 Exactness statement

For the fixed finite automaton and K, with complete rows, fair scheduling, and no resource limits, this is an exact safety-game procedure. Every new choice covers at least one previously uncovered input valuation. Failed targets are permanently excluded by sound losing information at this K. Finite safe cycles can close a winning graph.

This gives finite termination, not a polynomial bound. The experiment must report BDD/guarded-choice blow-up if it happens.

### 7.9 Gate: first milestone

Test the discovery set, then held-out families, then the full official set.

A candidate merits a live default-off arm if either:

- it gains at least three previously unresolved cases across two families; or
- at least five hard completed cases across two families improve by 25% or more with repeatable timings and no correctness regression.

Record all lost answers, including resource and near-cap losses. An option may remain research-only even when some targets improve.

Report prep, solving, and verification times separately. In C3, bypass full semantic-action-table construction rather than building it just to throw it away. The frozen reference table is allowed only in the separately timed differential-verification mode.

---

## 8. P5 — use a public Spot lazy provider, not a new LTL translator

### 8.1 Admission from P0

Choose exactly one provider route first:

```text
TAA/TGBA public path, if source and smoke tests validate it
or
another compatible public Spot on-the-fly provider
or
one explicitly reviewed small Spot adapter
```

`ltl_to_taa()` is the first candidate to inspect because its public return type already offers a TGBA-facing interface. Do not confuse the internal alternating skeleton with the ordinary successor stream consumed by the rank solver.

Keep `refined_rules` and any translation options fixed between its eager and lazy controls. Trying another option is a separate construction arm.

If only lazy products are suitable, limit the experiment to explicitly justified products of small bad-language monitors. Do not claim a general arbitrary-LTL streaming translator or replace conjunction/disjunction semantics opportunistically.

### 8.2 Reuse the existing formula and frontend boundary

Capture exactly the formula supplied by the existing worker to `create_automaton()`. This is already the bad-language formula for that worker's rank-game reduction.

```text
native TLSF frontend / existing effective semantics
    -> existing simplification and worker transformation
    -> captured spot::formula
    -> admitted Spot public provider
```

No new negation, new TLSF adaptation, AST reconstruction, parameter expansion, or section-level assumption pruning is permitted in this comparison.

Support real workers first. Enable formula-unreal only after its transformed-job fixtures agree. Keep the existing automaton-unreal route eager until its automaton transformation is separately compatible with the provider. Do not silently relabel another route as automaton-unreal.

### 8.3 Acceptance adapter

Generic `twa` permits more than Büchi acceptance. Acacia's rank solver does not.

Admit directly:

```text
ordinary transition Büchi
```

For generalized Büchi, first look for a pinned public Spot adapter that is genuinely lazy. The ordinary graph-based `degeneralize_tba()` is not evidence of such an adapter. If none exists, implement a small Acacia `spot::twa` wrapper; this is acceptance conversion, not formula translation.

Suggested file:

```text
src/solver/spot_lazy_buchi_view.hh
```

### 8.4 Simple cursor construction

Let the provider have generalized Büchi acceptance sets indexed 0,...,m-1, fixed before any game exploration. Require the acceptance formula to be exactly their conjunction of Inf conditions. Reject arbitrary Emerson–Lei acceptance rather than guessing.

For m > 0, a wrapper state is:

```text
(underlying Spot state, next required acceptance index j)
```

On an underlying transition with mark set A:

```text
if j not in A:
    next_j = j
    wrapper transition nonaccepting
else if j < m - 1:
    next_j = j + 1
    wrapper transition nonaccepting
else:
    next_j = 0
    wrapper transition accepting
```

Advance at most one index on one transition in the first version. A more aggressive skip-level convention changes fixed-K counts and is a separate experiment.

Initial cursor is 0. The wrapper exposes a single Büchi set and uses transition acceptance.

For m = 0 and acceptance true, mark every existing transition accepting: every infinite run is accepted, but a finite dead run is not. Handle a constant-false acceptance case explicitly or decline it; do not turn it into true.

Do not merge differently marked underlying edges merely by OR-ing their acceptance sets. Normalize and merge only when destination **and resulting acceptance payload** agree.

#### Why it is correct

Infinitely many completed cursor rounds imply infinitely many visits to every underlying set. Conversely, if every underlying set occurs infinitely often, a cursor waiting for the next set eventually advances and completes infinitely many rounds. The wrapper therefore preserves the infinite-word language.

This does not equate its fixed-K ranks with Spot's optimized degeneralizer or the shipping state-based BA. C4 and C5 use this same wrapper.

### 8.5 Ownership and cursor caching

Wrapper-state equality and hashing include:

```text
provider identity
underlying semantic state
cursor index
```

Use Spot's state ownership rules for wrapped states. The wrapper holds the underlying provider alive. Cache underlying complete rows independently from cursor rows where useful; the same TGBA row may be reused at multiple cursor positions.

Acceptance-set count and meaning must remain constant. Detect a provider that changes them after first successor expansion and decline the run.

### 8.6 Dynamic sparse ranks

Add a small Acacia rank type, not a new Posets container:

```text
sorted immutable pairs (stable StateId, rank value)
missing coordinate = -1
values 0,...,K
```

- Normalize duplicate destinations by maximum.
- Omit -1 entries.
- Hash/equality depend only on stored pairs, not the current state-arena size.
- Compare componentwise with missing coordinates equal to -1.
- All discovered coordinates are numeric for the generic provider.
- Use a sufficiently wide integer during addition; do not overflow a signed-byte rank before saturation.

Late discovery of a state adds no entries to old rank keys. It was inactive in those old states because their predecessor transitions were computed from complete active rows.

Run C3s on the frozen graph to distinguish sparse-storage cost from provider-generation cost. Do not replace dense ranks in existing Acacia globally.

### 8.7 Global passes excluded from the lazy path

Do not call, before or during C5 solving:

```text
full reachable-state copy/materialization
whole-automaton SCC inspection
simulation minimization
Boolean-state forward saturation
whole-graph postprocessing/degeneralization
DOT/HOA printing of the complete provider
statistics functions that traverse every reachable row
```

Some methods on generic automata perform hidden materialization; audit each helper used. In particular, Fin-acceptance operations can trigger graph copying in Spot. [S1]

All-numeric ranks are the honest initial price of not having global SCC information. A larger rank game or weaker simplification can outweigh streaming savings; measure it.

### 8.8 Provider tests

Mandatory:

- small formula language checks after full materialization;
- constants true/false, dead ends, GF/FG, U/R, supported W/M, nested next;
- m = 0, m = 1, m >= 2 acceptance cases;
- edge with several marks; two edges with different marks;
- one missing fairness set forever;
- same provider eagerly enumerated versus lazily consumed at fixed K;
- late state discovery does not change old rank hashes/equality;
- factory or row expansion exceeding an acceptance/state/BDD budget returns UNKNOWN;
- injected alternating/unsupported acceptance is declined;
- a false candidate caused by an omitted row is rejected by the verifier.

Offline language testing may use Spot's established comparison/complementation tools. These are test oracles, not a new deterministic synthesis backend. Bounded lasso tests are useful regressions, not a general language-equivalence proof by themselves.

---

## 9. P6 — the genuine eager/lazy comparison

### 9.1 Eager control C4

In a fresh process:

1. Construct the chosen public provider from the same captured worker formula.
2. Wrap its acceptance with exactly the P5 cursor convention.
3. Enumerate all reachable wrapper states/rows under a research resource cap.
4. Freeze that exact graph without changing acceptance or applying global optimizations.
5. Run the same sparse-rank guarded OTFUR and verifier.

### 9.2 Lazy candidate C5

In a different fresh process:

1. Construct the same provider and wrapper with identical options.
2. Request only the initial state.
3. OTFUR requests complete rows only for active sources.
4. Verification may request additional active rows required by its finite certificate.
5. No full-graph statistic or printer runs until after the measured solve has ended.

Do not warm one provider by materializing it and then call that the lazy run. Do not count an untimed reference translation as free candidate preparation.

### 9.3 Accounting

Report:

```text
provider factory time/memory
underlying rows requested
cursor-wrapper rows requested
underlying states discovered
wrapper states discovered
rows requested by search only
additional rows requested by certificate checking
row-generation time
rank-support distribution
BDD query and peak-node metrics
game states/guarded choices
verification time
end-to-end time and peak RSS
```

Materialize a separate copy after the experiment to obtain a denominator when feasible. If eager enumeration times out or exhausts memory, record its size as unknown/censored and report absolute lazy counts.

Row utilization is no longer merely hypothetical in this comparison: C5 actually did not request the rest of the wrapper's reachable rows. Still include the cost of the eagerly constructed TAA/formula skeleton, factor automata, and acceptance setup.

### 9.4 Equivalence comparison

Compare exact fixed-K outcomes only for C4 versus C5, with the same initial convention, all-numeric domain, and acceptance wrapper. If IDs differ, compare through the construction/state correspondence, not integer IDs or `format_state()` text.

Compare the admitted provider with shipping Spot by language checks and certified top-level coverage. Equal formula language does not imply equal minimal sufficient K, equal intermediate rank regions, or equal strategy size.

### 9.5 K lifecycle

For one job, immutable provider rows may remain cached across K attempts. Reinitialize:

```text
rank states and rank interner
losing ranks/proofs
threshold and preimage caches
selected guarded strategy
```

unless an existing, separately proved reuse rule applies. Keep the current K schedule. At Kmax, return the current worker's existing inconclusive/failure result; a fixed-K loss is never directly an LTL unrealizability verdict.

### P6 gate

Admit C5 as an explicit experimental provider option only when:

- all same-construction eager/lazy fixed-K tests agree;
- all returned certificates verify;
- at least three new full-pipeline answers across two families, or repeatable >=25% gains on five hard jobs across two families;
- preprocessing losses, memory regressions, and unsupported inputs are fully reported;
- a capped candidate does not hide the cost by discarding required row or query work.

If C5 requests nearly all rows and loses to C4 or the shipping pipeline, stop the generation branch. Keep useful symbolic-letter work. Do not respond by automatically starting the deferred tlsf-tools/OxiDD rewrite.

---

## 10. P7 — integration without changing the existing portfolio

### 10.1 Research options first

Use the config registry and existing backend machinery. Suggested concepts, not final spelling:

```text
forward_explicit             existing implementation
forward_spot_letters         frozen/known Spot graph + symbolic letters
forward_spot_lazy            admitted public Spot provider + symbolic letters
```

The automaton provider and game backend are separate recorded fields. A request for an unavailable provider/backend must produce a clear unsupported configuration, not silently run backward and retain the wrong label.

Do not add a new top-level process, change existing arms, or enable a new default during this sprint.

### 10.2 Decision only first

Successful guarded invariants prove worker wins. Initially report only through the existing decision path.

The guards and constant outputs are strategy information, but synthesis requires a separate output-encoding/Mealy-Moore/polarity integration gate. Until then, synthesis requests use the existing supported backend.

### 10.3 Honest fallback

Resource-limited candidates return UNKNOWN or invoke a clearly logged existing fallback. If a lazy provider must be fully materialized to fall back, account for that cost and cap it. Do not claim that fallback can always resume the old mixed-rank fixed point without rebuilding preprocessing.

Test candidate-only mode for attribution and candidate-with-fallback mode for user-facing coverage separately.

### 10.4 Spot patches

A small adapter implementing the cursor wrapper belongs in Acacia first. A true defect or missing reusable upstream operation may justify a Spot issue/patch with a minimal reproducer.

No tlsf-tools or OxiDD PR is expected from this sprint. Record follow-up requirements only after measurements identify a concrete missing capability.

---

## 11. Benchmarks and result gates

### 11.1 Corpus and baselines

Reuse:

```text
tests/suites/benchmarks/syntcomp26/all.list
tests/suites/benchmarks/syntcomp26/tlsf-sources.tsv
current exact family-origin metadata
existing coverage and status-adjudication scripts
```

Verify 1,524 official logical entries. Do not use a small panel to claim the full coverage gain. Known status exceptions must remain adjudicated rather than treated as unconditional annotation truth.

Keep the current pure forward reference and four-arm portfolio separately identified. Compare isolated worker jobs for mechanisms and actual end-to-end runs for coverage.

### 11.2 Run protocol

```text
same binaries' build flags except the named treatment
same Spot/BuDDy build
17-second primary cap
60-second targeted diagnosis cap
8 GiB memory, zero swap
one sequential invocation per benchmark scope
repeat near-cap and claimed major-speedup cases
no overlapping campaigns that contend for the same machine
```

Tests with sanitizers are correctness tests, not timing measurements.

### 11.3 Required tables

#### Spot API audit

```text
| provider | linked version | factory does | row request does |
| acceptance | materializing calls | usable? | source evidence |
```

#### Demand

```text
| job | construction ms | game ms | total Q/E | requested Q/E |
| verification-only rows | union across K | action profiles used | result |
```

#### Symbolic letters

```text
| job | C0 result/time | C3 result/time | input/controller nodes avoided |
| guarded choices | query peak nodes | preparation ms | verification ms |
```

#### Streaming

```text
| job | provider/options | C4 result/time | C5 result/time |
| factory ms | underlying/wrapper rows generated | rank bytes | peak RSS |
```

#### Coverage

```text
| candidate | corpus | answered | new answers | lost answers |
| REAL/UNREAL split | conflicts | PAR-2 | CPU | peak memory |
```

Also report answer-conditioned timing. A fast UNKNOWN is not a performance win over a slower decisive answer.

### 11.4 Production gates

Run the existing unit/config, frozen correctness, labelled correctness, and 2025/2026 panel gates, then the full 2026 run for an eligible arm. If an inherited gate already fails, run matched baseline and candidate and show the exact failure sets rather than declaring success or blaming the new code indiscriminately.

No candidate is promoted on static BDD size, a small row-utilization ratio, or a handful of selected examples alone.

---

## 12. Correctness checklist for code review

```text
[ ] Effective worker formula and AP polarity captured at the current boundary.
[ ] No second negation or unproved TLSF/assumption simplification.
[ ] Provider state equality uses compare/hash, not raw pointer values.
[ ] Every returned Spot state has one clear owner; destroy() is used correctly.
[ ] Every iterator is released on success, error, and early exit.
[ ] Provider/dictionary/AP ownership outlives every cached guard and state.
[ ] Complete empty row and unknown/incomplete row are distinct.
[ ] No certificate uses a partially generated row.
[ ] All enabled automaton branches contribute to max rank updates.
[ ] Frozen increment convention matches the existing actioner exactly.
[ ] Generic acceptance is admitted/converted explicitly, never guessed from marks.
[ ] Acceptance universe is fixed before exploration.
[ ] No SCC/Boolean-state conclusion is drawn from an unfinished graph.
[ ] Missing sparse coordinates are -1 and old keys never change with arena size.
[ ] BDD quantifiers use the transformed input/output partition.
[ ] Unknown or untried outputs are not classified as losing.
[ ] Uncovered inputs prevent a WIN certificate.
[ ] Target loss reopens input coverage rather than prematurely losing the source.
[ ] All-output refutations have acyclic proof dependencies.
[ ] Independent verifier reconstructs from rows, not search caches.
[ ] BDD/row/verifier failure yields UNKNOWN, never Boolean false/true.
[ ] Language equivalence is not used as a fixed-K equality assertion.
[ ] No hidden copy/postprocessor/statistics call materializes the lazy graph.
[ ] Timings include provider construction and verification.
[ ] C4/C5 use fresh instances and identical construction/options.
[ ] Existing portfolio and TLSF/OxiDD dependencies remain unchanged.
```

---

## 13. Commit boundaries and execution checklist

Keep the patches small enough to review independently.

```text
Commit 1  P0 version report, source audit, public-provider smoke tests
Commit 2  P1 no-behavior-change support-demand instrumentation and campaign
Commit 3  P2 Spot ownership/row adapter and exhaustive transition tests
Commit 4  P3 exact BuDDy letter oracle and independent replay tests
Commit 5  P4 guarded-input forward engine and proof verification
Commit 6  P4 frozen-automaton results: LAND / RESEARCH / STOP decision
Commit 7  P5 sparse ranks and admitted public Spot provider acceptance adapter
Commit 8  P6 same-provider eager/lazy tests and measurements
Commit 9  P7 default-off runtime option only for admitted candidates
```

Suggested Acacia files:

```text
src/research/spot_otf_probe.cc
src/solver/spot_state_ids.hh
src/solver/spot_rows.hh
src/solver/spot_letter_oracle.hh
src/solver/spot_guarded_forward_safety.hh
src/solver/spot_lazy_buchi_view.hh       only if no public lazy adapter fits
src/solver/sparse_forward_rank.hh
src/research/spot_rows_replay.cc
src/research/spot_letter_oracle_replay.cc
benchmarking/spot-demand-campaign.py
benchmarking/spot-letter-campaign.py
benchmarking/spot-provider-campaign.py
benchmarking/SPOT-OTF-API-AUDIT.md
benchmarking/SPOT-STREAMING-OTFUR-SPRINT.md
```

Use existing certificate/diagnostic/export helpers rather than creating duplicates. No changes to tlsf-tools or Posets are required. No generic BDD-backend interface is required.

Execution:

```text
[ ] P0 pin linked Spot; inspect TAA and product before writing any translator
[ ] P0 record exact capabilities and unsupported cases
[ ] P1 collect shadow demand and preparation bottlenecks
[ ] P2 verify state/iterator ownership and rank-successor equivalence
[ ] P3 truth-table test every symbolic query
[ ] P4 verify guarded coverage, reopening, and proof replay
[ ] First milestone report: symbolic letters on existing graphs
[ ] P5 choose a public Spot provider or explicitly decline generation work
[ ] P5 validate generalized-Büchi wrapper and all-numeric sparse domain
[ ] P6 compare eager versus lazy versions of that same construction
[ ] P7 run actual full-corpus/portfolio attribution gates
[ ] Publish conclusions and only demonstrated follow-up requirements
```

---

## 14. Final deliverables and decisions

Leave:

```text
benchmarking/SPOT-OTF-API-AUDIT.md
benchmarking/SPOT-STREAMING-OTFUR-SPRINT.md
benchmarking/spot-demand-results.tsv
benchmarking/spot-letter-results.tsv
benchmarking/spot-provider-results.tsv
```

Each experiment must end with:

```text
LAND DEFAULT-OFF ARM
KEEP RESEARCH TOOLING
STOP — WITH RECORDED REASON
NOT ADMITTED — CAPABILITY/GATE NOT MET
```

Success can be any one of:

1. Symbolic-letter OTFUR gains coverage while retaining the current Spot translator.
2. A public Spot provider plus a small adapter avoids meaningful reachable-automaton construction and gains coverage.
3. The source audit finds that the available provider is too eager or too costly, but gives a precise, reproducible missing-operation specification for a later Spot extension.
4. The measurements decisively show that changing translation is not the next useful investment, while preserving reusable row/query/proof infrastructure.

A later tlsf-tools automaton generator or OxiDD experiment is justified only by an identified deficiency: for example, no usable Spot provider, an acceptance/construction limitation that a local wrapper cannot address, or a measured BuDDy query bottleneck. They are not automatic remaining tasks after this sprint.

---

## 15. Sources and provenance

**User-provided basis:** `acacia_demand_driven_symbolic_otfur_handoff.md`. The correctness contracts and guarded-input algorithm above revise that document; the scope change to Spot-only implementation follows the current request.

The implementation design, thresholds, and proposed gates are recommendations. They are not claims that the experiments have already been executed.

### Acacia sources

**[A1]** PR #137, runtime per-arm backend and measured portfolio:
https://github.com/gaperez64/acacia-bonsai/pull/137

Pinned full-run report:
https://github.com/gaperez64/acacia-bonsai/blob/509c95ef706125060909bf9a63b2223b57267588/benchmarking/plots/three-way-full-20260905/README.md

**[A2]** Current explicit translator boundary:
https://github.com/gaperez64/acacia-bonsai/blob/509c95ef706125060909bf9a63b2223b57267588/src/solver/create_automaton.hh

**[A3]** Current rank/action semantics:
https://github.com/gaperez64/acacia-bonsai/blob/509c95ef706125060909bf9a63b2223b57267588/src/actioners/standard.hh

**[A4]** Current forward solver and prior empirical decisions:
https://github.com/gaperez64/acacia-bonsai/blob/509c95ef706125060909bf9a63b2223b57267588/src/solver/forward_reachable_safety.hh
https://github.com/gaperez64/acacia-bonsai/pull/129

### Spot official documentation

**[S1]** `spot::twa`: on-the-fly interface, state/iterator ownership, arbitrary acceptance, materialization cautions:
https://spot.lre.epita.fr/doxygen/classspot_1_1twa.html

**[S2]** `twa.hh`: state comparison/hash/clone/destroy, state-map/unicity helpers, iterator API:
https://spot.lre.epita.fr/doxygen/twa_8hh_source.html

**[S3]** Translation entry points and explicit FM result:
https://spot.lre.epita.fr/doxygen/group__twa__ltl.html
https://spot.lre.epita.fr/doxygen/classspot_1_1translator.html

**[S4]** Lazy product:
https://spot.lre.epita.fr/doxygen/classspot_1_1twa__product.html

**[S5]** TAA/TGBA public view:
https://spot.lre.epita.fr/doxygen/classspot_1_1taa__tgba.html

**[S6]** Formula-labelled provider:
https://spot.lre.epita.fr/doxygen/classspot_1_1taa__tgba__formula.html

**[S7]** Documented graph-based acceptance conversion signatures:
https://spot.lre.epita.fr/doxygen/degen_8hh_source.html

**[S8]** Official versions/source acquisition:
https://spot.lre.epita.fr/install.html
https://gitlab.lre.epita.fr/spot/spot

Online pages may display different documentation versions. P0 must replace any version-sensitive assumption with evidence from the actually linked source.
