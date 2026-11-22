# Current results and open gaps

This is the single results-and-gaps report. [The evidence index](evidence-index.tsv)
names published archives; [protocol](README.md) defines caps, gates and noise floors.
[Decisions](decisions.md) records policy and [architecture](DATA-STRUCTURES.md) owns
runtime contracts. Exact pre-closeout reports, all measured rows and retired
narratives are in the published and indexed archive `refactor-closeout20261017`.

Historical rows below keep their original values and measured/derived meanings.
Administrative statements in the retained October 13/16 reports describe their
write-time state: their original campaign archives are now indexed. New manifests
preserve executable modes; only pre-#245 restores need chmod repair. Historical
baseline identities remain provenance, not current build instructions.

## 2026-11-22: Bound parity strategy reduction and reselect small games (#467)

Initial campaign `strat467-20261122` uses implementation `e018f539`, based on master
`b28b0454`. The verified archive is retained locally under
`~/scratch/strat467/` pending publication; no evidence-index row is added.
Release GCC 15.3.1, private Spot 2.16.3, one compile job/LTO thread/test process,
5G/no-swap scopes and serial measurements use `g3-440/env.sh`, with TMPDIR set
to `strat467/tmp`. The frozen binary embeds its pre-commit base/dirty version;
the archive retains the exact implementation sources, build options and hashes.

**Diagnosis.** Tier four solves the expanded ACD parity arena globally with
Spot's Zielonka solver. Its strategy is already positional, but the arena's
translation/ACD state remains controller memory. `apply_strategy` follows that
single chosen controller edge and every environment edge from the initial state,
so extraction already excludes unreachable states. It also performs structural
merging. `solved_game_to_separated_mealy` does **not** call a minimizer or consult
`minimize_lvl`. The legacy backward extractor explicitly calls
`simplify_mealy_here(mealy, 2, false)` on its antichain-based controller.

The runtime option `--parity-mealy-reduction none|bisim|bisim-output|reselect`
adds explicit post-selection work inside winning synthesis runs only. `none`
retains the previous pipeline. `bisim` and `bisim-output` use Spot levels 1 and 2:
bisimulation, and bisimulation with output assignment. Both are exact controller
specializations. They skip machines above 512 states, 8,192 edges, or 65,536
label BDD nodes summed over edges. The default, `reselect`, additionally copies
small tier-4 arenas, stably sorts outgoing edges by destination, re-solves, extracts
and reduces a second strategy, and keeps it only if encoded A+L is strictly smaller.
That second solve skips above 2,048 arena states, 8,192 edges or four acceptance
sets. Re-solving is necessary: arbitrary substitutions of winning-region edges
can lose liveness. Original game certificates and decision solving are untouched.
Fix pass 1 encodes the incumbent first and forks both optional attempts through
the restriction prepass's shared supervisor. They share at most 200 ms and 5% of
the remaining invocation time, with private 512 MiB AS/RSS limits. Failures,
timeouts, invalid AIGER and non-improvements keep the best encoded incumbent.
Bounded structural parsing avoids rebuilding BDDs in the parent; each attempt
records its option, outcome, cause and wall time.

**Initial 43-input smoke panel, not full-corpus admission.** The fixed panel contains the
three outliers, the previous encoder sweep's 30 varied realizable cases, and ten
additional historical first-member size wins. Labelled arguments in
`benchmarking/synthesis-size.py` compare identical frozen
`otf_sparse_formula_parity` executable bytes at 17 s, with fresh independent
`tests/controller_check.cc` runs capped at 60 s / 200,000 states.

Every option verifies all 43 inputs. There are **zero checker rejections**, synthesis
or verification timeouts, resource limits, or other failures. Totals and medians
use the same 43 commonly verified inputs; times sum wall seconds over all attempts.

| Option | Verified / 43 | Rejected | Total A+L | Median A+L | Synthesis s | Check s |
|---|---:|---:|---:|---:|---:|---:|
| `none` | 43 | 0 | 36,347 | 45 | 8.04 | 14.68 |
| `bisim` | 43 | 0 | 35,302 | 41 | 8.13 | 14.67 |
| `bisim-output` | 43 | 0 | 34,055 | 41 | 8.17 | 14.71 |
| `reselect` | 43 | 0 | 26,152 | 41 | 8.13 | 14.52 |

Choose `reselect`: 28.0% lower total with unchanged coverage. No input's size
increases in this panel. Median synthesis times are 0.168–0.169 s and the largest
individual synthesis time is 0.626 s. This is no general speedup or full-corpus
coverage claim; the totals include ordinary portfolio selection differences.

Forced `both:parity:spot` reruns isolate the outliers, with all 12 circuits verified:

| Input | `none` states / edges; A+L | `bisim` | `bisim-output` | `reselect` |
|---|---:|---:|---:|---:|
| prioritized_arbiter n=6 | 386 / 6,088; 13,070 | unchanged | unchanged | unchanged |
| Lights_2c5b09da | 153 / 1,387; 10,765 | 117 / 1,225; 9,941 | 111 / 1,204; 9,265 | 25 / 300; 1,475 |
| prioritized_arbiter n=5 | 162 / 1,748; 4,715 | unchanged | unchanged | unchanged |

Lights shrinks by 86.3%; both arbiter gaps remain. The larger arbiter's 68,392
label BDD nodes exceed the production cap; uncapped diagnostic levels 1 and 2
also leave it unchanged. The smaller arbiter's alternative strategy has the same
size and is discarded. Existing parity wins remain: forced parity with either
`none` or `reselect` yields 2,752 versus forced legacy's 23,698 on ltl2dba_U1 n=7,
and 203 versus 1,802 on ltl2dba_E n=7; all checks pass. The collector n=7 comparison
also confirms that a first-member win is not necessarily a parity-route win:
forced parity is 6,875, while its own legacy arm produces 936.

**Rejected candidates.** Spot's public `minimize_mealy` API uses SAT, with premin
-1/0/1 mapping to simplification levels 3/4/5. It has no cancellation or step
budget; `max_letter_mult` bounds alphabet processing, not SAT solving. Every raw
SAT level exceeds a 3 s diagnostic cap on each outlier. Canonical low output
choices barely help the arbiters and can hinder output-assignment merging.
Condition-BDD-ID ordering sometimes improves Lights further but is sensitive to
BDD history; destination order is the structural candidate retained. All 21
completed fixed-machine diagnostic circuits and all 21 strategy-order trials
verify. Reloaded-dictionary or post-encoding reductions can have different AIG
sizes; the table above is the actual original-dictionary production pipeline.

Initial validation: ruff passes, 127 Python regressions pass, and four focused release
Meson tests pass. Fixtures cover exact output specialization, all three graph/BDD
size guards, Mealy/Moore target timing and independent controller checks. CLI tests
cover accepted, invalid and missing values, default bytes, and identical decision
stage traces for every option without `-s`. Full corpus and G0–G5 are not run.


**Review fix pass 1 (2026-11-23).** The final paired rerun uses exactly the same
43 inputs, comparing only `none` and `reselect`, with one frozen binary
(`1c4ea71eb777fdcf`) and the same independent checker. The measurement
runner now supplies its absolute invocation deadline to synthesis.

| Option | Verified / 43 | Rejected | Total A+L | Median A+L | Synthesis s | Check s |
|---|---:|---:|---:|---:|---:|---:|
| `none` | 43 | 0 | 36,146 | 41 | 7.99 | 14.71 |
| `reselect` | 43 | 0 | 26,152 | 41 | 8.62 | 14.60 |

Every input verified by `none` also verifies with `reselect`; there are zero
synthesis or verification failures. Total A+L falls 27.6%, with no per-input size
increase in this pair. These remain smoke measurements, including ordinary
portfolio selection differences; no speedup or full-corpus admission is claimed.
Raw rows, controllers and verification output are in `~/scratch/strat467/fix1-panel-run/`.

All ten affected release Meson tests pass, along with 30 measurement-runner Python
tests, Ruff and clang-format 22. A reachable, input-complete 41-state / 81-edge
nested-output machine below every size cap exhausts its 50 ms child budget and
returns the incumbent in 51.6 ms; `controller_check` verifies its liveness objective.
A two-state arena seeded with a valid alternating strategy requires reselection
to replace it with a zero-A+L constant strategy; both controllers independently
verify `GF o`. Tests also cover child allocation/exception/signal failures, strict
improvement, unchanged parent limits/cause state, expired-budget fallback,
reordered signal names and malformed AIGER. The native arm's earlier construction
deadline also bounds optional work. No evidence-index row is added.

## 2026-11-22: Select AIGER modes; +dc rejects controllers and large strategies remain the main gap (#280)

**Correctness finding: eight of 495 smoke controllers are rejected.** All eight
contain `+dc`; baseline `isop` verifies all 33 inputs. The rejected pairs are:

- Cockpitboard_efe2feaf: `isop+dc`, `both+dc`, `both+dc+ud`.
- Gamelogic: `isop+dc`, `ite+dc`, `ite+dc+ud`, `both+dc`, `both+dc+ud`.

Spot's own `aig::as_automaton(false)` and fresh negated-objective product confirm
all eight, with accepting counterexample runs retained. Re-encoding the same
captured legacy component machines reproduces every rejection with both checkers;
the baseline circuits are byte-identical to the primary baseline. The components
pass Spot's separated-Mealy and input-determinism checks. Fixed legacy replay also
rejects Cockpitboard's `ite+dc` and `ite+dc+ud`; the primary portfolio chose its
single-machine parity route for those two modes and verified them. This is a
correctness finding about these encoder/pipeline combinations, not a size win.
These repeats retain the original BDD dictionary. Reloading the HOA into a fresh
dictionary did not reproduce the fault; a portable upstream reproducer and exact
root cause remain follow-up work. The smaller invalid circuits never count.
Keep `isop` as the default. `--aiger-mode` refuses every `+dc` mode, at the CLI and
at each synthesis entry point, until #466 finds the cause.

Archive `enc280-20261122` (release `evidence-2026-11-22-enc280`) contains the raw rows, circuits,
checks, counterexamples, phase records, captured Mealy graphs, explicit partitions,
reproduction code, selected source files and hashes, and the release binary/checker.
The implementation binary is from `b59f76cb` on `aiger-mode-280`, based on master
`f3e25c1c`; `14932c52` adds only a direct helper test. GCC 15.3.1 and private
Spot 2.16.3 are used. Every build/test/run is scoped at 5G/no swap, with one compile
job, one LTO thread, one test process and serial measurement. The sourced environment
is `g3-440/env.sh`, with TMPDIR overridden to `enc280/tmp`.

`--aiger-mode MODE` is a runtime option, passed through legacy, obligation and
parity synthesis. Its default remains `isop`, and no extraction/minimization step
changes. Validation follows Spot 2.16.3 `aiger.hh` and `aiger.cc`: `ite`, `isop`,
`both`, source-supported `optim`, arbitrary-order/repeated `+dc`, `+ud`, `+sub0..2`,
and comma-separated alternatives. The last `subN` wins. Acacia accepts only this
canonical subset: empty components and unknown tokens fail during argument parsing,
and `+dc` is refused (#466); the accepted spelling is `+ud`, not the
header prose's `+dual`. No build option or registry entry is added.
All 33 primary `isop` circuits are byte-identical to `syn280-20261122`'s first member;
CLI tests additionally compare omitted mode with explicit `isop`.

**The outliers are primarily strategy-size problems.** An external interposer
captures the exact machines at Spot's encoder boundary. Both parity-bearing archived
members have the parity rows below; both remaining archived members have the legacy
rows. Every row uses `isop`. The current branch's default and forced parity runs
reproduce the parity rows. Forced legacy runs have the same state/edge counts, with
sizes 57/63/65 respectively, reflecting the different configuration/strategy choices.

| Input | Parity states / edges, level 0 | Legacy states / edges, level 2 | Parity / archived legacy A+L | Parity after level 2, states / edges; A+L | Same parity machine, both+ud+sub1 A+L |
|---|---:|---:|---:|---:|---:|
| prioritized_arbiter n=6 | 386 / 6,088 | 12 / 1,536 | 13,070 / 66 | 386 / 6,088; 13,070 | 12,397 |
| Lights_2c5b09da | 153 / 1,387 | 2 / 42 | 10,765 / 63 | 111 / 1,204; 8,905 | 9,612 |
| prioritized_arbiter n=5 | 162 / 1,748 | 10 / 640 | 4,715 / 55 | 162 / 1,748; 4,715 | 4,389 |

All three use the genuine tier-4 parity game, with `synthesis.minimize_lvl=0`.
The backward extractor calls `simplify_mealy_here(mealy, 2, false)` before encoding;
its pre/post state counts are 1,441→12, 4→2 and 241→10. Applying that same level-2
step to copies of the parity machines leaves both arbiters unchanged and helps
Lights only modestly. Plain ITE makes the arbiter circuits 62,556 and 14,622 A+L;
Lights becomes 9,612. Even the best smoke mode on fixed parity machines remains
orders of magnitude above the smaller strategies. #467 follows up on strategy
extraction and bounded minimization (level 2 is worth measuring for Lights); adding the missing
level-2 call alone cannot repair the arbiter gap. No pipeline choice changes here.
The obligation arm retains a racing legacy fallback; route records distinguish that
fallback from actual helper encoding, and a direct C++ test exercises the helper.

**Smoke panel, not full coverage.** `panel.list` fixes the three outliers plus 30
historically commonly verified inputs from distinct families spread across baseline
sizes 0–2,752, with historical solve ≤2 s and check ≤3 s. The panel includes TSL,
automaton examples, buses, schedulers, arbiters and load balancing. Each labelled
member reuses one release binary through `--extra-args MEMBER='--aiger-mode MODE'`.
Every controller gets a fresh independent `tests/controller_check.cc` invocation,
with a 17 s synthesis cap, 60 s check cap and 200,000-state check budget.

Totals and medians below use the **same 31 inputs verified by every one of the
15 modes**. Cockpitboard and Gamelogic are excluded from every size column, including
baseline. Verified counts remain over all 33 attempts. Times sum recorded synthesis
and checker wall times separately over all attempts. There are no synthesis or
verification timeouts, resource limits, unsupported checks or other errors.

| Mode | Verified / 33 | Rejected | Total A+L | Median A+L | Synthesis s | Check s |
|---|---:|---:|---:|---:|---:|---:|
| `isop` | 33 | 0 | 34,401 | 32 | 6.49 | 10.36 |
| `isop+dc` | 31 | 2 | 34,387 | 32 | 6.54 | 10.38 |
| `isop+ud` | 33 | 0 | 34,415 | 22 | 6.96 | 10.40 |
| `isop+dc+ud` | 33 | 0 | 34,582 | 24 | 7.00 | 10.37 |
| `ite` | 33 | 0 | 96,463 | 38 | 6.30 | 10.43 |
| `ite+dc` | 32 | 1 | 96,108 | 31 | 6.30 | 10.39 |
| `ite+ud` | 33 | 0 | 96,243 | 33 | 6.37 | 10.46 |
| `ite+dc+ud` | 32 | 1 | 96,075 | 27 | 6.46 | 10.43 |
| `both` | 33 | 0 | 33,230 | 39 | 6.53 | 10.39 |
| `both+dc` | 31 | 2 | 33,116 | 39 | 6.59 | 10.34 |
| `both+ud` | 33 | 0 | 33,090 | 21 | 7.05 | 10.49 |
| `both+dc+ud` | 31 | 2 | 33,020 | 21 | 7.14 | 10.29 |
| `both+ud+sub0` | 33 | 0 | 33,028 | 21 | 7.11 | 10.30 |
| `both+ud+sub1` | 33 | 0 | 31,302 | 22 | 7.07 | 10.42 |
| `both+ud+sub2` | 33 | 0 | 33,000 | 27 | 7.01 | 10.26 |

Select the core winner by most verified, then lowest common total, median and
recorded solve time; this chooses `both+ud`. Its three `subN` variants follow in a
second serial stage. The lowest observed valid total is `both+ud+sub1`, 31,302,
9.0% below baseline 34,401. These are portfolio observations: 10 inputs change
winning route across mode runs. In particular, `both+ud` and its equivalent
`+sub0` variant differ by 62 solely because lilydemo06 switches from parity to
backward (100→38 A+L). All three outliers retain the same parity route. The
fixed-machine comparisons above separate encoding effects from these races.
There is no wall-time speedup or full-corpus selection claim. Campaign elapsed
wall time is 259.9 s for the core stage and 65.6 s for the subexpression stage.

Validation: ruff passes; focused Python tests plus the benchmark-hardcoding guard
pass 127 tests. Release Meson checks cover help, mode grammar/propagation and default
bytes, standalone controller verification, and direct obligation-helper modes.
All four final selected Meson tests pass. Full corpus runs and G0–G5 are not run.

## 2026-11-22: One-sided restrictions add ten solves at 17 s (#465, #451)

Archive `restrict451-20261122` (release `evidence-2026-11-22-restrict451`). #465 adds
a bounded prepass inside the existing formula workers, so there is no new arm. It
has two kinds of attempt:

- **Merged inputs.** Every member of a declared input index family is replaced by its
  first member, in up to three attempts of 500 ms each and 1 s in total. Only an
  UNREALIZABLE answer is accepted.
- **Fixed outputs.** All scalar outputs are set to false, in one attempt of 200 ms.
  Only a REALIZABLE answer is accepted.

The substitution covers the complete positive objective, under the original
ownership map, before any unreal-side negation or player exchange. tlsf-tools
`Restriction.lean` (#149) proves that each kind transfers only its own polarity.
Each attempt runs in a subprocess capped at 512 MiB, and the prepass is skipped
under `-s`.

**Local G3** ran under the regime of `g3-local-20261120`: `--scope-mode instance`,
a 6G cap and 17 s. The base is the #449 head binary `26fccc41`, which is
solver-identical to the branch's merge base, and there is a base-vs-base control.

| Campaign | SYNTCOMP25 solved / PAR-2 | SYNTCOMP26 solved / PAR-2 | Gate |
|---|---|---|---|
| #465 `0071d532` | 164 → 164 / 772.8 → 755.0 s | 167 → 167 / 593.2 → 573.0 s | PASS |
| control (base vs base) | 164 → 164 / 772.1 → 774.9 s | 167 → 167 / 590.1 → 592.4 s | PASS |

No verdict changed. Both panels are about 18 to 20 s faster, while the control
moves by 2 to 3 s.

**Full-corpus screen.** All 1,524 SYNTCOMP26 inputs ran at 17 s, serially, under
the same 6G scope, against the base's screen in `lift437b-20261121`. #465 solves
1,384 against 1,374, and PAR-2 falls from 6,253.6 to 5,878.1 s. The ten gains are
exactly the ones #370 predicted:

- collector_v3 n=10–13 and 15, REALIZABLE in 0.14 to 0.17 s;
- ltl2dba_theta n=25, 50, 100 and 300, UNREALIZABLE in 0.14 to 0.28 s;
- ltl2dba_C2_unreal n=100, UNREALIZABLE in 0.47 s.

theta n=500 stays unsolved. There are no losses and no verdict conflicts. Three
inputs both binaries solve are more than 1 s slower, all simple_arbiter_enc, whose
merged-input attempt runs into its 500 ms cap: n=16 goes from 12.1 to 14.8 s, n=10
from 6.9 to 8.4 s and n=11 from 6.5 to 7.6 s.

**Attempts.** The phase records hold 1,441 finished attempts on 1,155 inputs.
The median took 11 ms; 235 reached their deadline, the slowest after 529 ms. 201
transferred a verdict: 190 UNREALIZABLE from merged inputs and 11 REALIZABLE from
fixed outputs. None conflicts with the expected status or with the base's verdict.

**Renamed corpus.** The same binary on the tlsfobf rename corpus (seed 145) solves
the same 1,384 inputs with the same verdicts, with no coverage flips or exclusions.
PAR-2 is 5,860.1 s against 5,878.1 s on the plain corpus.

## 2026-11-22: The first synthesis member is the right one, but best-of-four controllers are half the size (#280)

Archive `syn280-20261122` (release `evidence-2026-11-22-syn280`). This is the first
measurement of synthesis output. `benchmarking/synthesis-size.py` (#453) ran `-s` at
17 s on all 433 SYNTCOMP26 inputs whose expected status is REALIZABLE, for each
`docker_default` member, serially under a 5G scope. Every controller was checked
independently by `tests/controller_check.cc`. That check explores the circuit's
reachable latch states, takes the product with the negated full TLSF objective,
and has a 60 s cap.

| Member | Controllers | Verified | Checker rejected | Synthesis timeout | Verify timeout | Ands+latches on 330 common | Median |
|---|---:|---:|---:|---:|---:|---:|---:|
| otf_sparse_formula_parity | 401 | 362 | 0 | 32 | 37 | 93,498 | 25.5 |
| otf_sparse_formula_parity_forward | 401 | 362 | 0 | 32 | 37 | 93,481 | 27.0 |
| best_decomp_rank_bucketed_semantic_mona | 346 | 331 | 0 | 87 | 14 | 211,000 | 25.0 |
| otf_sparse_formula_gr1_real_prepass | 365 | 338 | 0 | 68 | 26 | 210,722 | 26.5 |

**No controller was rejected.** Unverified controllers are verification timeouts,
one checker error (amba_decomposed_lock n=30) and the resource limit on
ltl2dba_C2 n=14.

**The member choice stands.** `scripts/acacia-bonsai.sh --synthesis` uses the first
member, `otf_sparse_formula_parity`. It produces the most controllers, ties for the
most verified, and has the smallest total size. Only one input is verified by
another member and not by it.

**Size is where the room is.** On the 330 inputs every member verifies, the
first member and the per-input smallest of the four differ by 2.2x: 93,498
against 42,807 ands and latches. The gap is concentrated on a few inputs.
prioritized_arbiter n=6 is 13,070 against 66, Lights_2c5b09da 10,765 against 63,
and prioritized_arbiter n=5 4,715 against 55. On those three the parity route's
controller is about 200 times larger than the other routes'. Against the
competition's `REF_SIZE` (237 inputs), the first member's median ratio is 2.7x. It
is at or below the reference on 40 inputs and more than 10x above it on 27.
Median synthesis time is 0.16 s. The next steps of #280 (Spot encoder options,
then an ABC post-pass, both re-verified) target this gap.

## 2026-11-22: Formula symmetry cannot drive the K-bounded reduction; one-sided restrictions can decide 11 inputs (#370, #451)

Archive `sym370-20261122` (release `evidence-2026-11-22-sym370`). This is a
read-only investigation; no solver code changed.

**Census join.** The exact objective check of `tlsfsym` (`native-20261107`) verifies
a group on 154 of 1,524 inputs, and the automaton detector of #205 on 45. The two
sets share one input. Of the 154, 24 are unsolved by master at 17 s
(`lift437b-20261121`) or in #407's 120 s set: collector_v1, v2 and v3 (11,
REALIZABLE), ltl2dba_theta and ltl2dba_C2_unreal (11, UNREALIZABLE), and one each
of prioritized_arbiter_enc and simple_arbiter_enc (REALIZABLE). The automaton
detector finds none of the 24. The 11 cyclic-only inputs are all `shift`, which
master already solves.

**Why the current reduction cannot use them.** A formula-level symmetry preserves
the language, not the structure of the UcB automaton Spot builds, and the
K-bounded game counts rejecting visits on that structure. The certificate the
equivariance proofs assume (`proofs/SymmetryProofs/Detector.lean`) needs a state
bijection that fixes the initial state and preserves acceptance and edges.
Language invariance does not give it. On small members, the collector_v1 and v3
automata are structurally symmetric, but their state orbits (subsets of clients)
do not fit the natural block layout `symmetry_blocks.hh` accepts. For
collector_v2, ltl2dba_C2_unreal and ltl2dba_theta, Spot's automaton is already
structurally asymmetric: the state signatures differ under a transposition. An
exact quotient would need an equivariant monitor and a quotient built during
construction, an estimated 3 to 7k lines with no measured solve. It is parked.

**One-sided restrictions.** Merging a declared input family into one input restricts
the environment, so an UNREALIZABLE answer transfers to the original. Fixing a
scalar output to a constant restricts the system, so a REALIZABLE answer transfers.
On hand-restricted copies (single runs), ltl2dba_theta n=25 to 500 and
ltl2dba_C2_unreal n=25 to 100 are UNREALIZABLE in 0.03 to 0.31 s, and
collector_v3 n=10 to 15 REALIZABLE in 0.02 to 0.03 s. Eleven of these are unsolved
by master at 17 s. #451 builds this as a prepass, with a Lean proof of the
transfer first.

## 2026-11-21: The parametric-support fold solves load_balancer n=9 to 11 (#449, #437)

Archive `lift437b-20261121` (release `evidence-2026-11-21-lift437b`). #449 pins
tlsf-tools 9246712, whose GR(1) lift gains a parametric-support fold for
bus-wide monitors (#437 route B, tlsf-tools #147). When seed discovery declines
on a large template and the global monitors keep their state counts, the lift
binds monitor states across sizes and learns the seeds' regions as folds. It
answers REALIZABLE only when the region checker verifies the certificate on the
target, so a wrong candidate can only cost coverage. The pin also adds `tlsfobf`
(tlsf-tools #145).

**Local G3** ran under the regime of `g3-local-20261120`: `--scope-mode instance`,
a 6G cap, 17 s, a shared master `28b8b4b9` baseline and a base-vs-base control.

| Campaign | SYNTCOMP25 solved / PAR-2 | SYNTCOMP26 solved / PAR-2 | Gate |
|---|---|---|---|
| #449 `26fccc41` | 164 → 164 / 782.8 → 778.2 s | 167 → 167 / 591.0 → 590.7 s | PASS |
| control (base vs base) | 164 → 164 / 771.2 → 770.0 s | 167 → 167 / 588.8 → 588.8 s | PASS |

No verdict changed, and both PAR-2 differences are within the control's spread.

**Full-corpus screen.** All 1,524 SYNTCOMP26 inputs ran at 17 s, serially, under
the same 6G scope. #449 solves 1,374 against master's 1,371, and PAR-2 falls from
6,329.8 to 6,253.6 s. The three gains are load_balancer n=9, 10 and 11, all
REALIZABLE in 5.0 to 6.6 s, and load_balancer n=8 drops from 9.7 to 5.0 s. There
are no losses and no verdict conflicts. The fold also runs on load_balancer n=7
and prioritized_arbiter n=8, 9, 10 and 12, which both binaries solve; there it
costs 0.5 to 1.4 s each. No other input changes by more than 3 s.

**Renamed corpus.** `benchmarking/obfuscate-corpus.py` renamed every identifier of
all 1,524 inputs with `tlsfobf` (rename mode, seed 145) and gave the files neutral
names. #449 on the renamed corpus solves the same 1,374 inputs with the same
verdicts (PAR-2 6,217.7 s against 6,253.6 s on the plain corpus), with no
coverage flips and no exclusions. This includes the three load_balancer gains,
so the fold does not depend on benchmark names or identifiers.

## 2026-11-21: The fold lift solves simple_arbiter_enc n=11 to 16 (#448, #437)

Archive `lift437-20261121` (release `evidence-2026-11-21-lift437`). #448 pins
tlsf-tools 219270e, whose GR(1) lift gains a fold fallback for binary-encoded index
families (#437 route A). When seed discovery rejects every axis on encoding width,
the lift builds width-stratified seeds whose monitor states fold into Live or
LiveExcept classes. It answers REALIZABLE only when the region checker verifies
the certificate on the target AIGER (tlsf-tools #142 proves the checker sound), so
a wrong candidate can only cost coverage.

**Local G3** ran under the regime of `g3-local-20261120`: `--scope-mode instance`,
a 6G cap, 17 s, a shared master `9e379f95` baseline and a base-vs-base control.

| Campaign | SYNTCOMP25 solved / PAR-2 | SYNTCOMP26 solved / PAR-2 | Gate |
|---|---|---|---|
| #448 `c9839ef4` | 163 → 164 / 799.6 → 774.7 s | 167 → 167 / 589.3 → 592.9 s | PASS |
| control (base vs base) | 163 → 163 / 792.2 → 796.4 s | 167 → 167 / 593.8 → 588.1 s | PASS |

The one new solve is simple_arbiter_enc n=14 (REALIZABLE, 10.2 s). No verdict
changed. The SYNTCOMP26 difference is within the control's spread.

**Full-corpus screen.** All 1,524 SYNTCOMP26 inputs ran at 17 s, serially, under
the same 6G scope. #448 solves 1,371 against master's 1,365, and PAR-2 falls from
6,471.7 to 6,340.1 s. The six gains are simple_arbiter_enc n=11 to 16, all
REALIZABLE in 6.8 to 14.9 s. There are no losses and no verdict conflicts. Among
inputs both binaries solve, the largest slowdown is 2.1 s (prioritized_arbiter_enc
n=10, 13.3 → 15.5 s). No input that master leaves unsolved runs more than 1 s
longer.

**Census rerun.** The 30 inputs of `census437-20261120` were rerun with the lift arm
alone (60 s). simple_arbiter_enc n=14, 15 and 16 now lift (REALIZABLE in 6 to 8 s).
The other 27 keep their groups. prioritized_arbiter_enc n=13 to 15 reach the fold
but decline on `fold_seed_mismatch`. simple_arbiter_enc n=32 and
full_arbiter_enc n=12 used to decline (in 25 s and 57 s) and now time out at 60 s.
Neither is solved by either binary at 17 s, so this costs CPU but no coverage.

## 2026-11-20: v2.10.0 release candidate against master, SemML, ltlsynt and Acacia v1

Archive `final-20261120` (release `evidence-2026-11-20-final`, with plots and tables attached).
All 1,524 SYNTCOMP26 inputs, 8G/no swap per call, serial, staged caps (each stage reruns what the
previous left undecided). "new" is master 6a2586a (#435, #438, #439) on Spot 2.16.3, run fresh,
and SemML (20ad6ec7, JDK 21) was rerun fresh. master, ltlsynt and Acacia v1 are earlier measured
runs: master is the six-arm portfolio from `preset384-20261115` (80827c26, Spot 2.16.0); ltlsynt
2.16 and Acacia v1 come from the adjudicated tables of `threeway-syntcomp26-20261019`, with no
120 s run and conversion untimed.

| Series | 17 s solved | 17 s PAR-2 (s) | 60 s solved | 60 s PAR-2 (s) | 120 s solved | 120 s PAR-2 (s) |
|---|---:|---:|---:|---:|---:|---:|
| **new (v2.10.0 RC)** | **1379** | **5749.9** | **1399** | **16514.7** | **1404** | **30752.6** |
| master (six arms) | 1370 | 5871.9 | 1392 | 17193.1 | 1398 | 32117.5 |
| SemML | 1274 | 11614.5 | 1333 | 27836.5 | 1352 | 47818.0 |
| ltlsynt 2.16 | 1264 | 9420.7 | 1290 | 29523.6 | — | — |
| Acacia v1 | 812 | 24778.5 | 854 | 82407.0 | — | — |

No series disagrees with another on any verdict. Against master, new solves 9, 7 and 6 more inputs
at 17, 60 and 120 s, all of them unrealizable, and PAR-2 is lower by 122, 678 and 1,365 s. New at
60 s already decides more than master at 120 s.

Who wins the race in new (phase records, `parent_winner`) at 17 s, out of 1,379 decided:
- native Acacia antichain 755 (54.7%):
  - backward 442, on a Spot-translated automaton;
  - the formula-unreal arm 313: 180 in its direct checks, 110 on the closure-recurrence automaton,
    23 after falling back to Spot;
- Spot 525 (38.1%): parity 499, obligation 26;
- SemML-style parity in tlsf-tools/OxiDD 64 (4.6%);
- the GR(1) lift 35 (2.5%).

The 60 and 120 s stages add 20 and 5 more, mostly Spot parity and the GR(1) lift.

Family ablations (shipped arms minus one family, 17 s), with the inputs each lost rerun at 120 s:

| Family removed | Solved at 17 s | Lost | PAR-2 change (s) | Recovered by others at 120 s | Unique at 120 s |
|---|---:|---:|---:|---:|---:|
| Native Acacia antichain | 1254 | 129 | +4210.2 | 14 | 115 |
| Spot (parity + obligation) | 1333 | 46 | +1656.7 | 9 | 37 |
| SemML-style parity (OxiDD) | 1353 | 26 | +864.8 | 8 | 18 |
| GR(1) lift | 1363 | 16 | +520.9 | 5 | 11 |

Every family decides inputs no other family decides within 120 s. Removing the native antichain
arms frees a worker and gains 4 inputs, but loses 115 that no other arm decides. Dropping each of
the other families gains nothing.

The `preset384-20261115` index row now records its full source revision, so `fetch` accepts it;
the row had an abbreviated revision that did not match the archive.

## 2026-11-20: The lift's large-template bound grows with the instance (#437)

Archive `census437-20261120` (release `evidence-2026-11-20-census437`). The #407 lift
census was rerun on its 30 inputs with the committed per-axis decline reasons
(#440), using the `both:gr1-real-lift:oxidd` arm alone. The setup was 60 s, a 6G
scope and the release binary from `g3-local-20261120`. Every input lands in the
same group as in the #407 traces:

| Group | Inputs | Realizable variants by family name |
|---|---:|---:|
| Encoding width of binary indices (`axis`) | 12 | 10 |
| Large template (`bus_schema`) | 13 | 7 |
| Other: unindexed parameter, shape mismatch, unstable roles | 5 | 0 |

No run decided anything. The large-template arity equals the instance parameter
(`arity 10>4` on load_balancer n=10), so raising `--lift-max-predicate-arity` only
clears instances with n at or below the bound. The 7 realizable large-template
inputs were rerun at 120 s with bounds 4, 6 and 8:
- **rw_arbiter n=5 and lift_unary_enc n=6** (at 6), and **lift_unary_enc n=7** (at 8):
  the bound clears, then every seed window declines on a structural shape mismatch.
- **load_balancer n=10, 11 and lift n=9, 10:** still decline on the large template
  at 8.

The bound is therefore not the lever. A lift for these families would need
templates whose predicate arity is parametric. Independent ground truth is thin:
TLSF status is unknown for most of these instances, and SemML's 120 s run decides
only lift_unary_enc n=6 (REALIZABLE, 96 s).

## 2026-11-20: Recording the lift's decline reasons is timing-neutral (#440, #437)

Archive `g3-local-20261120` (release `evidence-2026-11-20-g3local440`). #440 makes
the native GR(1) lift arms record the per-axis reasons why seed discovery rejects
each axis (tlsf-tools #141). It is not meant to change verdicts or speed. G3 ran on
the maintainer's 4-core, 7 GB workstation under the regime of `g3-local-20261117`:
`--scope-mode instance` with a 6G per-solver cap instead of 8G, applied by a local
uncommitted patch to `landing-campaign.sh`, the 17 s cap and both 180-input
panels. Both binaries are release `otf_sparse_formula_parity` builds against a
locally built Spot 2.16.3 (`--enable-max-accsets=64`). They share one master
`6a2586a0` baseline with a baseline-vs-baseline control. No solver scope hit the
cap.

| Campaign | SYNTCOMP25 solved / PAR-2 | SYNTCOMP26 solved / PAR-2 | Gate |
|---|---|---|---|
| #440 `786b15c3` | 163 → 163 / 800.5 → 798.6 s | 167 → 167 / 592.7 → 588.1 s | PASS |
| control (base vs base) | 163 → 163 / 794.7 → 794.2 s | 167 → 167 / 591.0 → 587.7 s | PASS |

Neither campaign changed a verdict or lost coverage. Across the three baseline-binary
runs, the PAR-2 spread is 6.4 s on SYNTCOMP25 and 5.0 s on SYNTCOMP26. The candidate
falls inside it, so no speed claim is made. These timings are not comparable to the
benchmarking computer's floors.

## 2026-11-18: Spot 2.16.3 fixes the obligation-synthesis bug and adds three solves (#375)

Archive `spot2163-20261118` (release `evidence-2026-11-18-spot2163`). Spot 2.16.3 fixes
the simplification that made `spot::obligation_synthesis` answer REALIZABLE for
`G i -> G(i <-> XXX false)` (Spot issue #666). The #375 reproducer with
`--arms both:obligation:spot` answers UNREALIZABLE on 2.16.3 (REALIZABLE on 2.16.0), and
the `fuzz-portfolio` replay passes.

Shipped preset, all 1,524 SYNTCOMP26 inputs at 17 s, 8G/no swap, serial; the same
source (master b951fbcc, the 2.16.0 leg from `cpre407-20261118`) on each Spot:

| Spot | Solved | PAR-2 (s) |
|---|---:|---:|
| 2.16.0 | 1376 | 5811.0 |
| **2.16.3** | **1379** | **5743.4** |

Gains: abcg_arbiter_pb_4 (9.4 s), load_balancer_unreal1_pb_6_4 (14.4 s) and
full_arbiter_enc_pb_10 (17.0 s, at the cap). No losses and no verdict conflicts. G3, G4 and
G5 pass against master. The installed Spot on the benchmarking machine stays at 2.16.0 so
that frozen binaries keep their runtime. Binaries for this release are built against
2.16.3 with an rpath.

## 2026-11-18: The cheaper lazy KD intersection ships; theta persistence blocks do not

Archive `gates-cpre407-20261118` (release `evidence-2026-11-18-gates-cpre407`).

**Admitted: the posets intersection that skips contained maxima.** The entry below recorded it
as not admitted because it decides no new input. The owner's rule (2026-11-18) is that a change
that cuts time or space with no regression is merged anyway. Its runs from `cpre407-20261118`:
- all 1,524 inputs at 17 s: 1,376 against 1,376 solved, PAR-2 unchanged;
- the 41 search-bound inputs at 120 s: memory-outs drop from 3 to 1;
- posets microbenchmark: CPre 3.7x faster at dimension 10, 72x at dimension 128.

Gates against master b951fbcc, all passing:
- **G3:** 164/180 and 168/180, unchanged (first attempt refused by a leftover scope, logged; rerun passed);
- **G4:** pass;
- **G5:** pass, 1,524 pairs.

Posets is pinned at main a4387cb7 (posets #71), the same tree that was measured.

**Not merged: persistence blocks for theta.** Branch `theta-persistence` builds sibling
`FG c_1 | ... | FG c_m` disjuncts in closure-recurrence as one block, with a shared round-robin
counter and a single commit, the dual of a recurrence block. The language is unchanged (Spot
equivalence tests). It removes the subset blowup on `ltl2dba_theta`: in the formula-unreal frame
it decides n=13 in 5.5 s and n=17 in 44 s, where master gave up. The rank vectors now have
supports of 11-16 states instead of binomially many. But none of the corpus theta inputs
(n >= 25) is decided in 120 s, even with the arm alone.

At 17 s on all 1,524 inputs it solves 1,369 against 1,376, PAR-2 6047.3 against 5811.0 s. It
loses the six `ltl2dba_C2_unreal` inputs (and Morning2_1cf58fc1, which master solves at 15.0 s).
C2's negated objective also contains `FG ¬r_i` disjunctions; with the block, the construction
still succeeds (233 states), but the K-bounded search no longer finishes at K=2 and is still
at K=14 after 30 s. The encoding keeps the language but costs the bounded game far more.
A lead for later, not a merge candidate.

## 2026-11-18: A faster lazy KD intersection does not move the backward search (#407)

Archive `cpre407-20261118` (release `evidence-2026-11-18-cpre407`). Frame-pointer
profiles of `real:small:backward` on four inputs where it searches at the 120 s cap
(simple_arbiter_enc_pb_14, prioritized_arbiter_enc_pb_13, arbiter_on_inpchange_pb_6,
robot_grid_pb_7_7) put 82-99% of the time in CPre's `lazy_kdtree_backed::intersect_with`
and the domination checks of its inserts. The critical input picker and union copies do
not appear, so #407 items 3 and 4 are dropped.

Candidate for items 8/9: posets branch `intersect-contained-rhs` (5f1f3cc), which inserts
each right-hand maximum already contained in the left side once and skips all its meets.
It keeps the exact maximal set and runs CPre 3.7x faster at dimension 10 and 72x at
dimension 128 on posets' microbenchmark. Shipped preset, 8G/no swap, serial, master
b951fbcc against the candidate (51b89da1):

| Run | Before | After |
|---|---:|---:|
| All 1,524 inputs, 17 s: solved / PAR-2 (s) | 1376 / 5811.0 | 1376 / 5810.8 |
| 41 search-bound inputs, 120 s: solved / MEMOUT | 0 / 3 | 0 / 1 |

No gains, losses or verdict conflicts, and the backward arm reaches the same highest K
on all 41 inputs: within one K the search still needs far more than 120 s. Not admitted.
About 20 of the 41 are unrealizable per SemML (round_robin_arbiter_unreal1 16,
amba_case_study_unreal 2, load_balancer_unreal2 2), which the realizability arm cannot
decide in any case.

## 2026-11-18: The formula-unreal arm builds its automaton directly (#407)

Archive `ucw407-20261118` (release `evidence-2026-11-18-ucw407`). The shipped
parity presets now run `unreal:formula:spot-guarded-sparse` on the
`closure-recurrence` provider in fallback mode: the arm builds its Büchi
automaton from the formula, and falls back to Spot's translation (low effort,
direct checks first) when the construction hits a limit. All runs serial,
8 GiB and no swap per call, SYNTCOMP26.

| 17 s, all 1,524 inputs, one binary (57b76fc7) | Solved | REAL / UNREAL | PAR-2 (s) |
|---|---:|---:|---:|
| Shipped six arms | 1370 | 632 / 738 | 5871.6 |
| **Same, formula-unreal arm on closure-recurrence** | **1376** | 632 / 744 | **5809.0** |

The six gains are `ltl2dba_C2_unreal_pb_25, 50, 60, 70, 80, 100`, decided
UNREALIZABLE in 0.2-11.4 s, matching SemML's verdicts; Spot's translation of
their worker formula does not finish. No input is lost and no verdict
disagrees. PAR-2 is 62.6 s lower, beyond the 12.1 s floor. The arm decides
them because rows are now built as maps from (next set, postponement bit) to
guard, merged after every union and product (C2 has 300 implications under G
at n=25, which previously exceeded the branch limit), with each obligation
expanded once per row and the step limit per row.

Two defects found on the way were fixed before this run and stay recorded in
the archive: without a memory budget the construction grew until the
portfolio's scope was killed (7 round_robin_arbiter_unreal1 MEMOUTs on the
126-input screen; it now fails with a typed memory limit inside the worker's
share and falls back), and the closure route skipped the arm's direct checks,
losing 23 full_arbiter_unreal inputs in a first full-corpus run (1,353
solved).

Gates for 4584dafb against frozen master a3fdd342 (`MASTERA3F`, includes #427
and #428): G3 unchanged on both panels (164/180 and 168/180), G4 pass, G5 pass
(1,524 pairs, no opposite verdicts or frontend errors). The eight-arm
forward-inclusive preset (#433) takes the same arm setting so that it stays the
six-arm preset plus the forward arms; it was not measured separately.

Still open on the unrealizable side: `load_balancer_unreal1/2` and
`amba_case_study_unreal` exceed the per-row guard limit (one row's product has
over 500,000 distinct next sets from `GF(!req | XX grant)`), and
`ltl2dba_theta` builds 166 states but its K=2 search is exponential in the
number of recurrent inputs.

## 2026-11-17: Eight-arm second Docker slot (#431)

Local partial campaign `acacia-431-20261117`, pinned to master `0b369526`.
Matched normal release builds ran all 1,524 SYNTCOMP26 inputs through `-T`,
sequentially, at 17 s with per-instance 5G scopes and no swap on the 4-core,
7 GB workstation. These are local coverage/verdict observations and descriptive
PAR-2 totals; no speedup or comparison to the 12.1 s noise floor is claimed.

| Preset position | Solved | REAL | UNREAL | TIMEOUT | PAR-2 total (s) |
|---|---:|---:|---:|---:|---:|
| Previous second slot | 1,217 | 573 | 644 | 307 | 11,530.625 |
| New eight-arm second slot | 1,352 | 619 | 733 | 172 | 6,784.008 |

The eight-arm preset has 138 exclusive gains and three losses, with zero
verdict conflicts. All nonsolves are timeouts; neither leg has resource
limits, UNKNOWNs or errors. The losses, with the previous preset's solve
times, are `06.ltl` (12.689 s), `collector_v2_pb_15_pe_.ltl` (14.479 s), and
`load_balancer_unreal1_pb_5_5_pe_.ltl` (16.369 s). Each times out at 17 s in
the eight-arm leg. The raw gain/loss lists remain with the campaign evidence.

The new second slot shares all six arms with the first slot and appends
`real:small:forward` and `unreal:automaton:forward`. The first slot retains
its identity, arm order, options and hash (`e1f7cf334444`); all 13 pre-existing
preset option vectors and hashes are unchanged. The revised group also
retains the historical backward solver in slot three and the previous second
preset in slot four. Historical labels remain selectable outside the group.

G0 passed (release: 90 OK, six expected failures, zero failures; Posets:
110 OK, zero failures). G1 passed all 40 frozen rows with zero verdict
changes or coverage losses, using the same eight-arm configuration built
from the captured master source. G4 passed all 625 records with 32 permitted
timeouts. G5 passed all 1,524 inputs and ten independent spot checks, with
zero missing conversions, fallback pairs, frontend errors or opposite
verdicts; 39 answers are native-only and two converted-only.

Evidence: archive `acacia-431-20261117` (status partial: it predates G3), release
`evidence-2026-11-17-issue431`, SHA-256 `92f9b3ce…`, 77,488,200 bytes.

G3 passed on both panels, run locally under the maintainer-approved regime:
`--scope-mode instance`, a 6G per-solver cap instead of 8G (an uncommitted
local patch to `landing-campaign.sh`), 17 s, both 180-input panels. The baseline
is the previous second preset's release build (`33956fc9…`), the candidate the
eight-arm release build (`8b263f58…`). No solver scope hit the cap. Archive
`g3-431-20261117`, release `evidence-2026-11-17-g3-431`.

| Campaign | SYNTCOMP25 solved / PAR-2 | SYNTCOMP26 solved / PAR-2 | Gate |
|---|---|---|---|
| eight-arm vs previous second slot | 136 → 162 / 1711.4 → 835.1 s | 147 → 167 / 1219.8 → 549.1 s | PASS |
| control (previous vs previous) | 136 → 136 / 1712.1 → 1709.6 s | 147 → 147 / 1219.9 → 1218.4 s | PASS |

Neither campaign has a verdict change or coverage loss. The three baseline runs
give a local noise floor of 2.5 s (SYNTCOMP25) and 1.6 s (SYNTCOMP26) PAR-2;
the eight-arm gains are far outside it. These local timings are not comparable
to the 12.1 s floor.

## 2026-11-17: Local G3 admits A11 and A12 under a 6G regime (#403)

Archive `g3-local-20261117` (release `evidence-2026-11-17-g3local`). The
benchmarking computer was busy, so G3 for #427 (A12) and #428 (A11) ran on the
maintainer's 4-core, 7 GB workstation. The regime deviates from the README in
one way: `--scope-mode instance` with a 6G per-solver cap instead of 8G. This
was a local, uncommitted patch to `landing-campaign.sh`, also applied to
`landing-bar.py` remeasures. Each campaign used the 17 s cap and both 180-input
panels. All three campaigns shared one origin/master `1c61eda5` baseline
binary. No solver scope hit the cap.

| Campaign | SYNTCOMP25 solved / PAR-2 | SYNTCOMP26 solved / PAR-2 | Gate |
|---|---|---|---|
| #427 A12 | 162 → 162 / 804.3 → 805.8 s | 166 → 166 / 578.4 → 571.9 s | PASS |
| #428 A11 | 162 → 162 / 804.0 → 799.7 s | 166 → 166 / 569.7 → 569.3 s | PASS |
| control (base vs base) | 162 → 162 / 796.5 → 796.4 s | 166 → 166 / 571.7 → 574.4 s | PASS |

No campaign had a verdict change or coverage loss. The four baseline-binary
runs set a local noise floor: PAR-2 spread is 7.9 s on SYNTCOMP25 and 8.7 s on
SYNTCOMP26. Both candidates fall inside it, so no speed claim is made. These
local timings are not comparable to the benchmarking computer's 12.1 s floor.
## 2026-11-17: Translation and recurrence experiments for #407 decide nothing

Archive `issue407-20261117` (release `evidence-2026-11-17-issue407`). Offline
censuses and three screens on SYNTCOMP26 inputs left unsolved at 120 s, all on
master `1c61eda5`, serial, 120 s, 8G/no swap. None of the candidates is merged.

**Census.** On the 50 inputs where every antichain arm was still translating,
Spot translates the whole negated specification on none within 60 s, but
translates every per-guarantee piece on 11 and all but one piece on 23. The lift
declines on 30 inputs for two proof limits in tlsf-tools: the encoding width of
binary indices (12) and the large-template bound in `bus_schema` (13). Every
GR(1) fragment rejection comes from a guarantee at the reactivity level, so a
cheap fragment extension does not exist.

| Candidate | Binary | Inputs | Solved |
|---|---|---:|---:|
| `--translation-split guarantees` (Spot per piece, joined with `spot::sum`) | `0afe219c` | 50 | 0 |
| Recurrence-aware Büchi construction on `real:small:backward` | `4663c8f4` | 126 | 0 |
| Same construction on both guarded-sparse arms, fallback to Spot | `40dc762b` | 36 | 0 |

No screen has a verdict conflict.

- **Splitting with Spot** fails because Spot's simplification of the negated
  formula hides the guarantee structure (Demo1 and Felix split into 2 pieces,
  not 8 and 11), and where the split works, summing 36-61 pieces and lowering
  back to state-based Büchi does not finish.
- **The direct construction** gives small automata fast (theta(500): 2,012
  states in under a second, where Spot runs past 120 s). On the backward arm,
  36 inputs then stop in I/O preparation and 38 in the antichain search. I/O
  preparation builds one BDD relation over inputs, outputs and states, which
  grows exponentially with the number of outputs: `abcg_arbiter_pb_5` has 89
  states and 25 propositions and did not finish in 180 s or 3.3 GB.
- **On the guarded-sparse arms**, which work on symbolic rows, the
  realizability arm gets past I/O on 32 of the 36 and is still searching at the
  cap (K=2 on 28, K=5 on 4). The unrealizability arm hits the construction's
  branch or guard limit on 31 and falls back to Spot, which is still
  translating at the cap.

So on the realizability side a better automaton alone does not decide these
inputs: the antichain search at small K is the next limit. The unrealizability
side was not tested fairly. SemML's 120 s run (`c120-syntcomp26-20261024`)
decides 46 of the 126, 34 of them unrealizable, and on 25 of those the shipped
unrealizability arm never leaves Spot's translation while the new construction
stops at its branch limit within a second. The construction stays on branch
`ucw-recurrence-407` (`dbe18139`) while that is pursued; the split is not kept.

## 2026-11-17: Optimizing the forward arms would not recover coverage (#378)

Archive `fwd-20261117` (release `evidence-2026-11-17-fwd`). The campaign has
three parts:

- a read-only source pass;
- a phase-record reading of `cleanup-20261113`;
- a solo-arm census on the maintainer's 4-core, 7 GB workstation, with one arm
  per run, `-T`, a 3G scope with no swap and a 120 s cap.

The census binary is a release `otf_sparse_formula_parity` build at
02d4082-dirty. Verdicts and coverage are the evidence here; the timings are
not comparable to the 12.1 s floor.

Where `unreal:automaton:forward` decides at 17 s, search is 0.3% of CPU. At
120 s it is unsolved on 127 inputs that have records:

| State at 120 s | Inputs |
|---|---:|
| still translating | 95 |
| early quit on caps (13 input-push edge, 1 state, 8 forward node) | 22 |
| still searching, all Morning2/Morning2s at K=2 | 10 |

The 10 searching inputs are bound by the explicit action table. For example,
`Morning2s_06cf2373` holds 590k actions in a 2.4 GB table at 5 GB RSS while
18 of 348 states are live.

| Input | `unreal:automaton:forward` | `real:small:forward` | `both:parity:spot` | Current six default arms |
|---|---|---|---|---|
| `lift_wrong_physics_unreal_pb_10` | UNREAL 1.8 s | timeout | UNREAL 25.4 s | — |
| `load_balancer_unreal1_pb_6_4` | UNREAL 19.8 s | inconclusive | UNREAL 19.9 s | — |
| `load_balancer_unreal1_pb_4_8` | UNREAL 56.8 s | inconclusive | UNREAL 59.1 s | — |
| `generalized_buffer_pb_5` | inconclusive | **REAL 1.2 s** | timeout | timeout (120 s) |

On the 15 Morning2/Morning2s inputs:

- `unreal:automaton:forward` solves none. On three of them it is oom-killed
  at 3G during action construction.
- The forward and backward small arms tie on the two trivial inputs.
- `unreal:formula:spot-guarded-sparse` solves one.
- No arm solves the other 12.

`generalized_buffer_pb_5` is the only exclusive forward solve found. In the
same family, pb_2 to pb_4 are solved by both and pb_6 by neither.

**Rejected for now.** The forward-engine optimizations are rejected, because
the only coverage they could recover is the Morning family, which no arm
reaches:

- successors from live states only;
- memory-based caps;
- indexed action access;
- selective invalidation;
- demand-driven obligations;
- conditional proof payloads;
- `push_aps` partition memoization.

Both forward arms stay in the code and are selectable with `--arms`, outside
the default portfolio.


## 2026-11-16: The shipped portfolio drops `unreal:automaton:forward` (#384)

Archive `preset384-20261115` (release `evidence-2026-11-16-preset384`). Same frozen
binary as the cleanup report (`80827c26`, source 480b7676), all 1,524 SYNTCOMP26
inputs, 8G/no swap, serial; arm sets chosen with `--arms`. The seven-arm reference
is `final17/60/120` from `cleanup-20261113`. Later caps rerun only the previous
cap's timeouts.

| Arm set | 17 s | PAR-2 (s) | 60 s | PAR-2 (s) | 120 s | PAR-2 (s) |
|---|---:|---:|---:|---:|---:|---:|
| Shipped seven | 1367 | 5959.0 | 1388 | 17660.6 | 1394 | 33105.1 |
| **C1: six, without `unreal:automaton:forward`** | **1370** | **5871.9** | **1392** | **17193.1** | **1398** | **32117.5** |
| C2: seven, `real:small:forward` in its place | 1366 | 5987.3 | — | — | — | — |
| C3: six, the fallback set without parity arms | 1256 | 9967.3 | — | — | — | — |

No run has a verdict conflict or a memory failure. C1 gains `amba_decomposed_arbiter_pb_9`,
`amba_gr_pb_2` (Strict, from the TLSF source), `robot_grid_pb_6_6` and, at 17 s, `08`
(16.7 s) or, from 60 s, `lift_gr1+_pb_7`. Its only loss is `lift_wrong_physics_unreal_pb_10`
at 17 s (1.0 s with seven arms), solved again at 60 s. PAR-2 is 87 s, 468 s and 988 s
lower, well outside the 12.1 s floor. C2 gains `generalized_buffer_pb_5` but loses
`robot_repair5` and the same lift input. Without the parity arms C3 loses 115 inputs,
most solved in under 2 s by the shipped set, so it is not a useful fallback either.
The first `docker_default` preset now runs C1; `unreal:automaton:forward` remains
selectable with `--arms`.

Gates for the six-arm release build against the 2.9.0 gate binary: G4 and G5 pass
(no opposite verdicts or frontend errors on 1,524 inputs). G3 fails on both panels
(SYNTCOMP25 165 → 164, SYNTCOMP26 169 → 168, no verdict change) with the same single
loss, `lift_wrong_physics_unreal_pb_10` at 17 s. The owner admitted that known loss
given the full-corpus gains above; the input is solved again at 60 s.

The archive also holds the #373 rerun of the 22 Strict specs at 120 s: ltlsynt times
out on all of them with both the old and the strict formula, and native and strict
converted Acacia agree (all time out). Only the old non-strict conversion answers,
REALIZABLE on `amba_gr_pb_2` and `amba_gr+_pb_2`, which is the weaker specification.

## 2026-11-15: Cleanup sprint report

Archive `cleanup-20261113` (release `evidence-2026-11-15-cleanup`), which also
holds the sprint's gate runs and reviews. Master 216fdf36 with posets 82cd7bf
and tlsf-tools dce8e00, release build of the first `docker_default` preset.
Only this binary ran fresh, on all 1,524 SYNTCOMP26 inputs at 17 s, then its
17 s timeouts at 60 s and its 60 s timeouts at 120 s; references are recycled
from earlier archives.

| Series | 17 s | 60 s | 120 s |
|---|---:|---:|---:|
| This build (7 arms) | 1367 | 1388 | 1394 |
| Seven-arm default before the sprint (`native-20261107`) | 1368 | 1387 | — |
| Previous report's 8-arm final (`sprint-performance-20261102`) | 1351 | 1379 | 1387 |
| SemML | 1271 | 1334 | 1355 |
| ltlsynt 2.16 | 1264 | 1290 | — |

PAR-2 at 17/60/120 s is 5959.0, 17660.6 and 33105.1 s. No verdict disagrees
with any reference. Against the 8-arm final at 120 s the build gains seven
inputs, loses none, and its commonly solved inputs take a geometric mean 0.664
of the time; the one slower input, `generalized_buffer_pb_5_pe_` (100.5 s
against 1.4 s), is the arm removed in #359. Native arms alone at 17 s on the
same tree decide 1214 with lazy KD and 1211 with rank buckets; three inputs only
lazy KD decides, none only rank buckets. Plots, score and per-arm tables are
release assets.

## 2026-11-12: Shipped lazy KD build, native arms and gates

Archive `sprint-20261111` (release `evidence-2026-11-12-sprint`) holds this sprint's
campaigns, including the local IDs cited in the sections below. Final release build of
500a86e9, 1,524 SYNTCOMP26 inputs, 17 s, 8 GiB per call:

| Native arms only | Solved | PAR-2 (s) |
|---|---:|---:|
| Rank buckets, before the sprint's search work | 1,207 | 11,560 |
| Rank buckets, same final tree | 1,212 | 11,384 |
| **Lazy KD with bounds, same final tree** | **1,214** | **11,348** |

Lazy KD adds `abcg_arbiter_pb_3_pe_` and `arbiter_with_cancel_pb_6_pe_` and loses nothing;
no verdicts conflict. Gates on the shipped binary: G3 PASS (SYNTCOMP25 165/180 and
SYNTCOMP26 169/180, equal to master, PAR-2 650.8 vs 651.3 s and 455.2 vs 455.9 s), G4 PASS,
G5 PASS (0 opposite verdicts, 0 resource limits).

## 2026-11-12: Lazy KD with subtree and pending-block bounds becomes the shipped downset

Local campaigns `kd-bounds-20261112` (component replay and panels) and `kdb20261112`
(full corpus) are now included in the indexed `sprint-20261111` archive.
Posets 016826e adds coordinatewise lower/upper bounds to
lazy KD frozen nodes and to sealed 16-entry pending blocks; a failing one-direction
bound comparison skips the subtree or block, and live points are always checked exactly.
Both backends below are release builds of the same tree with identical options except the
downset.

| Measurement | Lazy KD | Rank buckets |
|---|---:|---:|
| Captured CPre updates, 8 inputs: cycles / instructions | 21.9B / 71.0B | 30.5B / 101.2B |
| Same, lazy KD before bounds | 34.1B / 93.9B | |
| 20-input search-bound panel, solved at 17 s / 30 s | 11 / 11 | 9 / 9 |
| 10 held-out inputs, solved at 17 s / 30 s | 4 / 5 | 4 / 4 |
| Shipped portfolio, all 1,524 SYNTCOMP26 inputs at 17 s | 1,368 | 1,368 |
| Shipped portfolio PAR-2 | 5,943 s | 5,947 s |

Every replayed update produced the same maximal antichain. The portfolio runs disagree on
no input and record no memory failure. The three panel gains (two arbiter inputs and AMBA 7,
all realizable) repeated with backend order reversed; in the portfolio another arm already
decides them. Peak RSS on replayed updates is 4.6% higher (881 against 842 MiB); robot-grid
6x6 reaches 4.5 GiB against 0.7 GiB at a 30 s timeout, which the review attributes to deeper
search but did not prove. Block sizes 8, 16, 32 and 64 were within 2%. Review found that
bound comparisons through the cached-sum shortcut could overflow for wide coordinate types;
bounds now compare coordinates only (posets regression tests with 64-bit coordinates). The
measurements above predate that fix, which only drops the sum shortcut from bound checks.

## 2026-11-12: Lazy KD performance pilot

Local subcampaign `lemire-20261112-pilot` is now included in the indexed
`sprint-20261111` archive. This is a subset measurement, not a full-corpus admission gate. Both release
builds use the first `docker_default` preset, with only the downset choice differing.

| Final backend | Fixed cases solved | Instructions (billion) | Cycles (billion) | Wall (s) |
|---|---:|---:|---:|---:|
| Lazy KD | 11/11 | 479.689 | 238.908 | 61.844 |
| Rank buckets | 11/11 | 520.809 | 250.360 | 64.582 |

Both share lazy directional vector comparisons and portable SIMD tail loading. KD also
replaces a repeated private ID bounds check with a debug assertion; pending and frozen
IDs must belong to the current append-only epoch. Nodes remain 32 bytes and the original
policy defaults remain. Intersection short-circuiting, 64-entry leaves, a smaller pending
buffer, and the rank-only block sum were rejected. All conclusive verdicts agree.

On the original 20 search-bound inputs, both solve 0/20 at 17 s. At 30 s KD solves 2/20
and rank buckets 1/20, with no losses: KD alone solves `abcg_arbiter_pb_3_pe_`. Full raw
instructions, cycles, timings, rejected trials, source snapshots, and validation logs are
retained in the local campaign. Both debug unit suites pass 100/100; the selected posets
tests pass 14/14; pytest reports 2056 passed, 15 skipped and 58 passed subtests; ruff passes.
Wall and cycle timings are noisy on the shared host. The owner's full-corpus run remains
the shipping gate.

The exclusive solve repeats with backend order reversed: KD finishes in 20.09 s while
rank buckets time out at 30 s again. Two exploratory scaling cases, AMBA 7 and 8, time out
for both backends at both limits and are recorded separately from the original panel.

## 2026-11-12: Local solver pilot

This is a local pilot on `d7873d6`, not a full-corpus admission result. The first
`docker_default` preset was resolved through the registry. Release measurements
use optimization 3, LTO, assertions disabled and the release compiler profile.
All solver calls are serial in 8 GiB systemd scopes with swap disabled; builds
use at most four jobs. Raw rows, stdout/stderr, counters, manifests, frozen
executables and build options from `/tmp/biere-20261112` are now included in
the indexed `sprint-20261111` archive.

The inherited forward change was completed and checked. It hashes a reusable
contiguous copy, caches exact 64-bit coordinate sums, and uses the sums only as
necessary conditions before the unchanged coordinate order. Hash lookup still
checks full state equality. An injected constant hash tests every collision;
additional tests cover coordinate lengths around 8/32-byte and SIMD boundaries.
The cached sums and scratch payload are included in logical byte accounting.
The generator consistency scan now lives inside `assert`, so release builds
cannot perform that second antichain traversal.

Two formula changes remove BDD work: helper operands borrow references for the
duration of each call, and the lazy Oracle counts result nodes only when a
reporter consumes the metric. The latter retains both step-budget checkpoints
and the independent manager live-node limit; reporting keeps its node counts.

Initial ten-input fixed-work screens, with identical conclusive verdicts:

| Change | Instructions (G), before -> after | Cycles (G), before -> after | Wall (s) |
|---|---:|---:|---:|
| Forward hash and cached sums | 947.53 -> 686.04 | 315.42 -> 220.57 | 72.22 -> 51.81 |
| Formula borrowed BDD operands | 484.84 -> 455.45 | 258.91 -> 225.02 | 62.24 -> 51.51 |
| Formula node counts, after borrowing | 455.45 -> 434.24 | 225.02 -> 207.30 | 51.51 -> 50.50 |

A separate paired repeat alternates baseline/candidate order per input. Forward
instructions fall 27.55% and cycles 22.25% (74.33 -> 60.42 s); formula instructions
fall 10.44% and cycles 14.35% (59.21 -> 51.15 s). Every verdict agrees.
`GF-G-contradiction4` crosses 17 s in both passes: 18.82 -> 11.25 s and
19.65 -> 11.34 s. Shared-host timing outliers remain in the raw rows. Forward L1
misses fall 9.76% in the repeat; last-level cache misses change direction between passes, so no
last-level cache improvement is established.
A short matched formula profile reduces markcount/unmark from 6.10% of samples
to below the 0.3% display threshold each; addref/delref fall from 10.55% to 6.52%.

Rejected: direct iteration in the critical picker removes temporary containers
but saves only 0.14% aggregate instructions (520.10 -> 519.35 G; cycles
212.24 -> 208.30 G). A reusable contiguous actioner input copy increases forward
instructions 12.35% (686.04 -> 770.77 G; cycles 220.57 -> 272.98 G), with only
0.02% fewer backward instructions and 8.33% more cycles. Both changes were
restored. The unchanged backward baseline itself varies on `load_balancer`
(171.00 versus 143.75 G instructions); its repeated panel is not evidence of a
fixed amount of work or a backward speedup.

On a separately selected 20-input search near-miss panel (7 backward, 6 forward,
7 formula), 17 s coverage is 1 -> 4 and staged 30 s coverage is 4 -> 4. There are
no lost solves or conflicting verdicts. Only 17 s timeouts advance to 30 s.
The three apparent new 17 s solves all time out in the first baseline pass:

| Instance | Baseline at 30 s | Candidate at 17 s |
|---|---:|---:|
| g-unreal-116 | 24.87 s | 15.26 s |
| infinite-race-unequal-26 | 18.22 s | 15.33 s |
| robot-to-target-charging12 | 16.68 s | 14.60 s |

The last baseline's sub-17 s result at the longer cap shows timing noise; the
initial coverage difference alone does not establish three stable crossings.
Two additional ordered 17 s repeats, combined with the original pass, give:

| Instance | Baseline solves / 3 | Candidate solves / 3 |
|---|---:|---:|
| g-unreal-116 | 0 | 3 |
| infinite-race-unequal-26 | 0 | 2 |
| robot-to-target-charging12 | 1 | 3 |

Thus the first crossing repeats consistently here; the two formula crossings
remain sensitive to timing. The candidate's successful repeat times are
14.62/14.76 s, 14.82 s, and 14.93/14.78 s respectively. These repeated rows are
separate observations, not corrections to the initial coverage counts.
A 14-input full-portfolio check solves every case in both binaries, with
identical verdicts (33.23 -> 32.13 s total, not an established speedup).
Full-corpus portfolio admission remains unmeasured in this pilot.

Correctness gates: the first registry preset in a debug build, with assertions
and fresh Python bindings, passes all 100 serial unit tests; pytest reports
2056 passed, 15 skipped and 58 subtests passed; Ruff 0.16.9 passes. Forward tests
compare 5012 independent explicit games and replay 7170 losing proofs and 5685
winning certificates. Lazy Oracle tests include 184647 predicate comparisons,
1122 fixed-K matches, cache eviction, corrupt certificates and exact step-limit
boundaries with telemetry enabled and disabled.

## 2026-11-04: Shared deterministic routes; rank/128 retained

The final eight-arm release binary decides **1362/1385/1390 of 1524 SYNTCOMP26
inputs at 17/60/120 s**. Against the previous sprint it gains **11/6/3** solved
inputs and loses none at each cap. It also loses no solves against the archived
November 1 eight-arm default. No conclusive verdict conflicts with a normalized/adjudicated recycled
reference or the separate diagnostic pass. Archive `pipeline-posets-20261103`
contains the [verified campaign evidence](https://github.com/gaperez64/acacia-bonsai/releases/download/evidence-2026-11-04-pipeline-posets/pipeline-posets-20261103.tar.gz);
its [guide](https://github.com/gaperez64/acacia-bonsai/releases/download/evidence-2026-11-04-pipeline-posets/README.md),
[plots and tables](https://github.com/gaperez64/acacia-bonsai/releases/tag/evidence-2026-11-04-pipeline-posets)
and [per-arm counts and percentages](https://github.com/gaperez64/acacia-bonsai/releases/download/evidence-2026-11-04-pipeline-posets/per-arm-counts-percentages.md)
are also published separately.

| Series | 17 s solved | 17 s PAR-2 (s) | 60 s solved | 60 s PAR-2 (s) | 120 s solved | 120 s PAR-2 (s) |
|---|---:|---:|---:|---:|---:|---:|
| New final | 1362 | 6199.319 | 1385 | 18148.228 | 1390 | 34068.512 |
| Acacia 2026-11-02 | 1351 | 6492.201 | 1379 | 18983.506 | 1387 | 35106.502 |
| Acacia 2026-11-01 | 1351 | 6489.177 | 1379 | 18979.462 | 1388 | 34986.434 |
| Prior master headroom | 1341 | 6799.767 | 1364 | 20533.729 | — | — |
| Acacia 2.7 lineage | 1342 | 6792.732 | 1358 | 21057.255 | 1366 | 39637.722 |
| SemML | 1271 | 11689.974 | 1334 | 27788.038 | 1355 | 47408.945 |
| ltlsynt 2.16 | 1264 | 9420.729 | 1290 | 29523.584 | — | — |
| Acacia 1.x | 812 | 24778.516 | 854 | 82407.024 | — | — |

Only the new binary was run freshly. Each pass starts with all 1524 inputs at
17 s and advances only its own preceding TIMEOUTs. The clean pass retried
162 inputs at 60 s and 139 at 120 s. REAL/UNREAL counts are 626/736, 641/744 and
645/745; remaining outcomes are 162/139/132 TIMEOUTs and two memory-limit results
at 120 s. There were no primary ERROR, UNKNOWN or conversion-failure results.
All calls were serial, each in its own 8 GiB scope with swap disabled. Both
120 s passes recorded five scopes with OOM kills; outcomes that also reach the
wall cap remain TIMEOUT under the recorded classifier. Resource sidecars retain
those events rather than treating them as absent.

The frozen executable is source `110b77994f247502b6b57a61fc68cf0dc6f46833`, SHA-256
`fb4928a5f8993765cf5ccc8cccff811ca015a1528db049336276834265be1242`, built with
release `-Ofast -march=native`, LTO and explicit compatible 128-bit Posets SIMD.
The clean pass supplies the timing curves. Archived comparators come from
different sessions; their clock conventions and the Acacia 2.7 prerelease/release
lineage remain explicit in the guide. Missing historical 120 s series are
omitted. These observations do not establish a matched full-corpus speedup.

The separate recording pass decides 1360/1384/1390 inputs. Parent-confirmed
winner counts cover 1307/1331/1337, with 53 unknown identities at each cap.
The unchanged strict auditor attributes 1088/1112/1118, with 272 unknowns.
These are alternative metrics over the same diagnostic solves; percentages
include unknowns in the solved denominator. They measure portfolio race wins,
not solo-arm coverage or winners in the clean pass. The
[instance ledger](https://github.com/gaperez64/acacia-bonsai/releases/download/evidence-2026-11-04-pipeline-posets/per-instance-winner-ledger.csv)
retains each reused observation's source and identity evidence.

#366 adds opt-in OxiDD-to-Spot classification/fast paths, direct negative-objective
Büchi construction and guarded Δ₂ normalization, using a copied formula-DAG API.
The original positive graph and objective remain available for fallback and
controller checks. Profiling led to batching internal deadline reads while
retaining cancellation callbacks and exact public/publication checks. The bound
is 255 skipped poll sites, not elapsed cancellation latency. Cheap syntax
admission features have not established a runtime winner predictor, so both
translators and the eight-arm default remain.

The Posets study covers fourteen backends, scalar controls and actual 128/512-bit
representations. No 256-bit performance mode was measured. BBox/128 alone reached
the frozen shortlist but failed held-out qualification and regression criteria;
rank/128 stays selected. Strict checks exposed noncanonical sharingtree storage,
now fixed together with shared-forest allocation recovery and BBox empty-set
algebra. All six strict sharingtree/sharingtrie replays across scalar/128/512
produce exactly 326 maxima. Correctness reruns are separate from the preserved
original performance observations.

Independent source and evidence reviews applying the `legendary-nbjorner` skill pass. G0 passes 84 Acacia and
42 assertion-enabled Posets tests; G1 passes 40 sentinels. The targeted ten-case
cycle gate has a 1.098 baseline/candidate geometric ratio (8.9% fewer cycles),
mainly from AMBA, with a worst regression of 1.3%. Both 180-input coverage panels
retain their solved sets and verdicts. G4 has 595 OK and 30 permitted TIMEOUTs
among all 625 registered occurrences. G5 checks all 1524 inputs without opposite
verdicts or frontend errors; seven native-TLSF fallback pairs are explicitly
not independent cross-checks.

[tlsf-tools #102](https://github.com/gaperez64/tlsf-tools/pull/102) and
[Posets #35](https://github.com/michaelcadilhac/posets/pull/35) are merged with
post-merge CI passing. Posets #36/#37 are closed.
[tlsf-tools #103](https://github.com/gaperez64/tlsf-tools/issues/103) remains open
for DAG/normalization/import proof obligations, and
[Posets #38](https://github.com/michaelcadilhac/posets/issues/38) for sharingtrie
allocation propagation and rebuild ownership. The current rank default is
unchanged; the sprint does not resolve those broader obligations.

An [archive retention table](https://github.com/gaperez64/acacia-bonsai/releases/download/evidence-2026-11-04-pipeline-posets/archive-retention-limits.md) records eleven unrecovered intermediate hashes: six early Posets executables, one superseded metadata receipt and four pre-build source drafts. Two executables supported an early untimed semantic screen, a third supported its alias/width checks, and the other three supported early unit and release checks. Their original hashes and surviving receipts remain; the final campaign, gates and frozen pilot/training/held-out measurements retain their verified artifacts.

The [archive metadata qualification](https://github.com/gaperez64/acacia-bonsai/releases/download/evidence-2026-11-04-pipeline-posets/archive-metadata-qualification.md) identifies three JSON metadata digests included in the declared binary-hash list. The archive and index retain the same declaration for verification; the qualification lists the 20 executable hashes separately. The measured binary and all result rows are unchanged.

## 2026-11-03: Lazy parity stays opt-in; accounting fixes retain the eight-arm default

The completed SYNTCOMP26 campaign decides 1351/1379/1387 of 1524 inputs at
17/60/120 s. The archived eight-arm default decides 1351/1379/1388: no gains,
and the only lost solve is `finding_nemo_pb_6_pe_`, previously REAL at 119.243 s
and now TIMEOUT at 120 s. No verdict disagrees with the normalized/adjudicated
reference series. The [plots and aggregate tables](https://github.com/gaperez64/acacia-bonsai/releases/tag/evidence-2026-11-03-sprint-performance)
are published separately, including the [per-arm solve-count table](https://github.com/gaperez64/acacia-bonsai/releases/download/evidence-2026-11-03-sprint-performance/per-arm-solve-counts.md)
and [CSV](https://github.com/gaperez64/acacia-bonsai/releases/download/evidence-2026-11-03-sprint-performance/per-arm-solve-counts.csv).
The complete [verified evidence archive](https://github.com/gaperez64/acacia-bonsai/releases/download/evidence-2026-11-03-sprint-performance/sprint-performance-20261102.tar.gz)
is `sprint-performance-20261102`, registered in [the evidence index](evidence-index.tsv).
It includes the raw observations, frozen binaries, input corpus and reviewed provenance.

Source `5b7b6fcb` has the same tracked tree as the reviewed/tested `c53f06df`.
The final frozen `otf_sparse_formula_parity` executable is SHA-256
`9d42189b7931fd3dc300987a82ff7e42c9ee82a068f927ec46baa6ba0179f3c0`,
built with release `-Ofast -march=native` and LTO, using the explicit compatible
128-bit Posets SIMD ABI, not the widest-vector build. The eight default arms
are unchanged. Every primary timing call used native TLSF in its own 8 GiB/no-swap scope,
serially, with phase/worker records and formula capture disabled.

Only this new binary was measured freshly: all 1524 inputs at 17 s, only its
173 TIMEOUTs at 60 s, then only those 145 TIMEOUTs at 120 s. Earlier source
observations are retained in the derived 60/120 s series. The interrupted
60 s stage preserved seven completed rows and resumed with the same pinned
identity. REAL/UNREAL counts are 622/729, 637/742 and 642/745. The remaining
outcomes are 173/145/136 TIMEOUTs and one resource limit at 120 s; no UNKNOWN,
ERROR or conversion failure occurred in the primary campaign.

| Series | 17 s solved | 17 s PAR-2 (s) | 60 s solved | 60 s PAR-2 (s) | 120 s solved | 120 s PAR-2 (s) |
|---|---:|---:|---:|---:|---:|---:|
| New final, eight arms | 1351 | 6492.2 | 1379 | 18983.5 | 1387 | 35106.5 |
| Prior default, eight arms | 1351 | 6489.2 | 1379 | 18979.5 | 1388 | 34986.4 |
| Prior master headroom | 1341 | 6799.8 | 1364 | 20533.7 | — | — |
| Acacia 2.7 lineage | 1342 | 6792.7 | 1358 | 21057.3 | 1366 | 39637.7 |
| SemML | 1271 | 11690.0 | 1334 | 27788.0 | 1355 | 47408.9 |
| ltlsynt 2.16 | 1264 | 9420.7 | 1290 | 29523.6 | — | — |
| Acacia 1.x | 812 | 24778.5 | 854 | 82407.0 | — | — |

Comparators are recycled historical observations, not fresh matched runs.
Acacia 2.7's 17 s rows are its prerelease lineage; 60/120 s retries use its
release, while earlier prerelease solves are reused.
SemML includes Java startup; ltlsynt and Acacia 1.x exclude SyFCo conversion.
Acacia 1.x retains archived resource-limit adjudications of false REAL reports.
The prior-default archive's contents are verified, but its archive/index
provenance lists differ; the relevant binary is declared by both and the
discrepancy is retained. Missing 120 s series are omitted. The 17 s PAR-2
difference from the prior default is 3.024 s, inside the recorded 12.074 s
noise floor, with identical coverage; this is no speedup claim. Standalone
figures, score tables, plot-data hashes and full clock/provenance notes are
retained in the archive's `closeout/` directory.

#361 retains opt-in `real:parity:oxidd-otf` and `unreal:parity:oxidd-otf`.
They expand complete lazy DPA rows and use the existing exact solver with
doubling exploration budgets. A [pre-code reviewed proof](coverage-first/on-the-fly-parity-proof.md)
covers the positive effective-Mealy objective, fixed frontier losses, stable
priorities and closed winning strategies. Each arm emits only its named
verdict, declines synthesis and reports resource/cancellation stops as UNKNOWN;
the existing DPA/Spot trust assumptions remain. BFS solves none of the required
nine load_balancer inputs at 17 s: seven TIMEOUTs and two initial-row BDD limits.
An opt-in strategy-order experiment observes three UNREAL solves at
7.108/2.718/12.152 s, but needs 2048/1024/2048 expanded rows. It fails the
predeclared requirement for a solve within 128 rows and is reverted; its patch,
proof, executable and all observations are preserved. No heuristic or new worker
is added to the default.

Two seven-arm candidates also fail their veto screens. Removing
`unreal:automaton:forward` loses `lift_wrong_physics_unreal_pb_10_pe_`
(incumbent UNREAL in 0.946 s, candidate TIMEOUT17). Removing
`real:small:forward` loses `generalized_buffer_pb_5_pe_` (incumbent REAL in
1.276 s, candidate TIMEOUT17). Both are rejected; #359 remains unresolved.

#362 makes completeness auditing validate the actual obligation/parity diagnostic
schemas while retaining identity, sequence, writer-total and drop checks.
#363 emits missing child record summaries on completed obligation/parity exits,
including UNKNOWN. The separate instrumented attribution pass has the same
solved totals as the primary campaign; its strict, parent-confirmed and UNKNOWN
winner evidence stays distinct and cannot identify the unrecorded primary race's
winner. It does not supply the primary timing curves.

G0 passes 82 unit and 19 Posets tests; G1 passes all 40 frozen rows. G2s passes
the preselected `membership-default` rule (baseline/candidate cycle ratio
1.00298; worst reported improvement -1.26%). Both G3 panels pass with no loss
or verdict change. G4 reports 595 OK, 30 permitted timeouts and no failures.
G5 passes over all 1524 inputs with no frontend error or opposite verdict:
10 spot checks pass, 28 answers are native-only and one converted-only. Its
seven tlsf-tools fallback pairs are explicitly not independently cross-checked.

## 2026-11-01: The oxidd parity arm joins the default portfolio; the swap for the Spot arm loses

Archive `otfdpa-20261101` (release `evidence-2026-11-01-otfdpa`), PR #358. The shipping eight-arm default is e8df0bbc (frozen binary SHA-256 `127eb09d72135a9fa58cd5a9817e02def8d3c44dbe8739d2edabac128dc09730`). Full-corpus runs cover all 1524 SYNTCOMP26 inputs. Every call ran in its own 8 GiB scope, serially. At 60 s and 120 s, each series reran only the inputs undecided at the previous cap and recycled earlier verdicts.

References are archived from earlier sessions, so the comparison is indicative: master headroom from `sharegate-20261028`, ltlsynt 2.16 and Acacia 1.x from `threeway-syntcomp26-20261019`, SemML from `semml-syntcomp26-20261024`, and v2.7.0 and SemML at 120 s from `c120-syntcomp26-20261024`.

| | 17 s decided | 17 s PAR-2 (s) | 60 s decided | 60 s PAR-2 (s) | 120 s decided | 120 s PAR-2 (s) |
|---|---:|---:|---:|---:|---:|---:|
| Eight-arm default | 1351 | 6489.2 | 1379 | 18979.5 | 1388 | 34986.4 |
| Master headroom | 1341 | 6799.8 | 1364 | 20533.7 | | |
| SemML | 1271 | 11690.0 | 1334 | 27788.0 | 1355 | 47408.9 |
| ltlsynt 2.16 | 1264 | 9420.7 | 1290 | 29523.6 | | |
| Acacia 1.x | 812 | 24778.5 | 854 | 82407.0 | | |
| v2.7.0 | | | | | 1366 | 39637.7 |

The default hit the scope memory limit on three calls at 120 s. No verdict disagrees with any reference.

Replacing `both:parity:spot` with `both:parity:oxidd` leaves seven arms (same build as b889e84c, frozen binary SHA-256 `3fed2ca91a49b4d0a028b51409f0d58fe6c17394d642123f03bd694ce73d2617`): 1317 decided at 17 s, PAR-2 8036.2 s. It loses 37 inputs master decides, most in under 3 s, and gains 13 hard ones, including `SPIPureNext`, `chain-5-real`, AMBA arbiters/encoders, `reversible-lane-r-real` and `robot-to-target-charging-unreal0`/`robot-to-target-charging-unreal3`. With the same binary, an explicit eight-arm list decides 1352 at 17 s, PAR-2 6465.9 s, +12/-1 against master; master alone decides `chomp_pb_4_4_pe_` at 10.4 s. Reducing the arm count is #359.

On the 37 losses, master's portfolio and the Spot parity arm alone decide all 37 in 0 to 8 s: 7 through the Büchi fast path and 30 as full parity games. The oxidd arm alone decides none: 12 unsupported outside Boolean Π2/Σ2, 22 still constructing at 17 s, 2 at the BDD limit and 1 slow solve. The swap is rejected. Moving Spot's Büchi fast path into the oxidd arm is also rejected: its DPA does not arrive in time on those inputs.

Earlier in the sprint, on the 69 inputs only SemML decides at 120 s, Owl's eager DPA fully expands on 43 (median 1883 states) and SemML's non-learned Strix heuristic decides 57/69. The lazy DPA arm alone at 120 s initially decided 0/69 (2 with the research flag), stopped by the BDD limit at the first state, the priority limit at import, host share or construction. Priority compression, memory-sized budgets and factoring (grouping, G/F distribution, decided-product sinks and AP-constrained unfolding) bring it to 12/69 by default and 18/69 with the general construction; the Spot parity arm decides 10. No verdict mismatches SemML anywhere in the sprint.

## 2026-10-28: Headroom becomes the default; translation sharing closes without a build

Archive `sharegate-20261028` (release `evidence-2026-10-28-sharegate`), #341. The equal and headroom policies used the same release binary of `otf_sparse_formula_parity` on all 1524 SYNTCOMP26 inputs. Every call ran in its own 8 GiB scope, serially. At 17 s, equal decided 1342 inputs and headroom 1341, with no verdict change. The one difference, `amba_case_study_unreal_pb_2_pe_`, finished under equal at 16.36 s, near the cap; both policies decide it at 60 s.

At 60 s, the 183 inputs either policy left undecided at 17 s ran with the order reversed. Equal reached 1357 decided in total and headroom 1364. Headroom alone decides `F-G-contradiction-114`, `chain-5-real`, `ltl2dba_R_pb_10_pe_`, `robot-to-target-charging-unreal0` and `robot-to-target-charging-unreal1`, and `simple_arbiter_enc_pb_11_pe_` and `simple_arbiter_enc_pb_12_pe_`. No input is decided only by equal. Headroom had no scope out-of-memory event; equal had one, on `round_robin_arbiter_unreal1_pb_3_13_pe_` at 59.6 s. Headroom became the default.

Archive `sharecheck-20261028` (release `evidence-2026-10-28-sharecheck`), #342, closed without a build. Phase records with full formula capture on `robot-to-target-charging-unreal5` show that only `real:small:backward` and `real:small:forward` translate the same formula with the same options. Each used about 1560 MiB of RSS and was still translating at 60 s. The two unreal workers translate different formulas. Parity uses deterministic options and was killed by its memory share at 17.7 s.

The upper bound for sharing used `robot-to-target-charging-unreal4` through `robot-to-target-charging-unreal7`. At a 120 s cap, parity alone decides all four in 46 to 64 s. Headroom with all arms decides none at 60 s or 120 s. Without `real:small:forward`, it decides none at 60 s and 1 of 4 at 120 s (`robot-to-target-charging-unreal4` at 86 s). A backward worker forking its forward sibling after translation was estimated at about 350 production lines and 405 test lines. It was not built. Scoping it found process lifecycle leaks fixed in #352.

The cap census was removed as dead diagnostics (#343, PR #350).
The soundness audit (#344) argued that 72 shipped reductions are sound and found one deadline bug (#347, PR #351).
The nondeterministic GFG fast path was removed (#346, PR #353, archive `gfgcensus-20261028`).

## 2026-10-28 — The GFG Büchi fast path fires on one input, so it is removed rather than proved

Archive `gfgcensus-20261028` (release `evidence-2026-10-28-gfgcensus`), master 1571a87b, a checked
build of the first `docker_default` member against Spot 2.16. The nondeterministic GFG decision
branch (`--spot-fast det-and-gfg`, never shipped) had an open proof obligation (#346). Before
proving it, this counted how often it would fire.

The 17 s phase records of `spotfast-ablation-20261019` give, for each REAL-side antichain worker,
the translated automaton's size and whether the deterministic shortcut solved it. On 245 inputs
some REAL-side automaton is a nondeterministic single-set Büchi automaton within the two-token
recognizer's 31-state budget; the default decides 240 of them at 17 s. Running the recognizer on
each (`--arms real:small:backward --spot-fast det-and-gfg`, 20 s, 4 GiB scope, serially):

| Classification per input | Inputs |
|---|---:|
| Not GFG | 228 |
| Not GFG, or over the budget | 5 |
| Not GFG, or deterministic | 1 |
| Deterministic (translation differs from the archive) | 6 |
| Not classified within 20 s | 4 |
| GFG | 1 |

The one GFG input, `ordered-visits-choice-real`, is REALIZABLE, which the GFG verdict matches and
the default reaches in 0.11 s. None of the five inputs the default leaves undecided at 17 s
(`generalized_buffer_pb_6`, `simple_arbiter_enc_pb_11`–`_14`) is GFG, and the four unclassified
inputs are decided by the default in under 3 s. The branch is removed; `--spot-fast` keeps
`off` and `det`.

## 2026-10-27 — The parity worker loses inputs to its memory share; on-the-fly parity would first need a lazy automaton

Archive `otfpar-20261027` (release `evidence-2026-10-27-otfpar`), branch `research/otf-parity` at d5cb75c5, not merged. Every call ran in its own 8 GiB scope, serially.

Where the eager parity route stops. Alone at 120 s on the 69 inputs only SemML decides (c120-syntcomp26-20261024), `both:parity:spot` decides 9 with 0.8–3.6 GB; the other 60 never finish translating to a deterministic automaton. On 10 of those, `ltl2tgba` does not even build the nondeterministic automaton within 60 s on 7. On the 231 tier-4 wins of `tier4-spot282-20261018`, the part of the solved game reachable under the winner's strategy covers a median 36% of the automaton (20% on the 40 automata with at least 1000 states). An on-the-fly parity solver would therefore need a lazily built deterministic automaton first; the game is not the bottleneck.

The memory share. In the default portfolio the parity worker is killed above 512 MiB (8 GiB over eight workers, halved), which it exceeds on 58 of the 69, so the portfolio decides none of the 9. A fixed 4 GiB share decides 5 but lets the scope run out of memory 26 times. The research option `--parity-memory-share pressure` keeps the equal share but enforces it only once the solver's own cgroup comes within an eighth of its `memory.max`; without a readable limit it is the equal share. Same binary, paired, order alternating:

| | equal | pressure |
|---|---:|---:|
| 69 SemML-only inputs, 120 s | 0 | 5 |
| SYNTCOMP26, 17 s | 1341 | 1342 |
| SYNTCOMP26, 60 s on the 17 s undecided | 1359 | 1364 |

At 60 s pressure alone decides chain-5-real, ltl2dba_R_pb_10, robot-to-target-charging-unreal0 and -unreal1, and simple_arbiter_enc_pb_11 and _12; equal alone decides Morning_9cac58d3 at 59.9 s, which both settle at 60.5–62.5 s in three reruns each. No verdict changes and no out-of-memory kill in either.

## 2026-10-26 — Backward reductions add little to the forward unreal engine, even computed from the whole automaton

Archive `fwdp-20261026` (release `evidence-2026-10-26-fwdp`), branch `research/fwd-prune` at 510b1ed6, not merged. The guarded sparse forward game streams the automaton and so skips the reductions the backward engine applies first. It already has the surely-losing purge and Boolean states; cap census, direct simulation and symmetry were missing. Before writing on-the-fly versions, this measured a ceiling: the three computed from the whole frozen automaton and applied inside the sparse search (opt-in diagnostic build option, lifted winning certificates verified against the original automaton). An on-the-fly version knows at most as much.

On the 105 targets of `otf2-20261026` at 120 s, single-arm `unreal:formula:spot-guarded-sparse:frozen-graph`, every call in its own 8 GiB scope, serially, the oracles decide 25 inputs against 26 without them. Nothing is decided only with them and no verdict disagrees; the one difference is a run that finishes at 119.99 s without them. At equal K, game states fall by at most 5x (by 2–5x on 5 inputs), and by at most 1.3x on the 51 inputs with cheap on-the-fly states, against a 10x gate. Cap census bounds no state on any input and symmetry applies on one. On-the-fly versions of these reductions are not pursued.

## 2026-10-26 — Building Spot's FM automaton on demand does not help; the guarded game is the bottleneck

Archive `otf2-20261026` (release `evidence-2026-10-26-otf2`), branch `research/fm-otf` at cc154734, not merged. On 3 of 4 inputs only SemML decides at 120 s, a phase profile showed every legacy worker still translating at the cap, so this sprint tried Spot 2.16's on-the-fly FM translator (`ltl_to_tgba_fm_otf`), which neither earlier lazy provider (f0006568, e5fa8d08) used. Every call ran in its own 8 GiB scope, serially.

A probe at 30 s per call on 105 inputs (the 69 inputs only SemML decides at 120 s plus the 46-input translation frontier of f0006568) compared, per worker formula, the worker's translation, bare `ltl_to_tgba_fm`, and a full breadth-first `ltl_to_tgba_fm_otf` exploration. All three agreed (67 finish, 137 time out) except two formulas that finish only without postprocessing, so postprocessing is not the cost. Of the timed-out explorations, 95 on 38 inputs never reached 1000 states (each state is expensive), and 87 on 51 inputs reached 1k–1.3M states (cheap states, too many), which is where a demand-driven provider could pay.

The provider `spot-fm-otf` (opt-in compile option, demand route of the guarded sparse game, fixed promise bound with a decline on overflow) passed language-equivalence and overflow tests and agreed with frozen-graph on `ab/tiny` and `ab/small`. On the 51 cheap-state inputs at 120 s, single-arm, against the same arm with frozen-graph:

| Worker | frozen-graph decided | spot-fm-otf decided | Only spot-fm-otf |
|---|---:|---:|---:|
| unreal:formula, spot-guarded-sparse | 10 | 6 | 0 |
| real:small, spot-guarded-sparse | 6 | 4 | 0 |

No verdict disagrees. The guarded game exhausts its 200k-rank budget after 100–200 automaton rows on most inputs (34 acceptance sets on 06 and 07 multiply it further), so automaton construction was not what bound these runs. Rerunning every UNKNOWN of either provider with game and provider limits raised 25x decided nothing new. The provider is rejected; a lazy automaton only helps a solver whose game grows with the automaton.

## 2026-10-25 — SemML narrows the gap to 2.7.0 at 120 s but does not close it

Archive `c120-syntcomp26-20261024` (release `evidence-2026-10-25-c120`). SemML's deficit to 2.7.0 shrank from 71 inputs at 17 s to 24 at 60 s, so the owner asked whether it overtakes with more time. The v2.7.0 release binary (ed481505) and SemML 20ad6ec7 reran at 120 s the inputs each left undecided at 60 s (166 and 190), four chunks per tool with the tools alternating, each call in its own 8 GiB scope with no swap. Earlier verdicts are recycled, as at 60 s; the SemML wrapper and heap cap are those of the entry below.

| | Solved | REAL | UNREAL | Timeouts | Memory cap | No verdict | PAR-2 (s) |
|---|---:|---:|---:|---:|---:|---:|---:|
| 2.7.0 | 1366 | 630 | 736 | 154 | 4 | 0 | 39637.7 |
| SemML | 1355 | 603 | 752 | 156 | 12 | 1 | 47408.9 |

The extra minute gives 2.7.0 eight more solves and SemML 21, so the gap is now 11 (71, 24, 11 at 17, 60, 120 s). 2.7.0 alone decides 80 inputs and SemML alone 69; no verdict disagrees. Memory starts to bind: SemML hits the 8 GiB cap on 12 inputs (none at 17 or 60 s, where its largest peak was 7.89 GiB), 2.7.0 on four round_robin_arbiter inputs, and one SemML run ends without a verdict at 7.47 GiB, which looks like Java heap exhaustion but was not confirmed because solver output is not kept. A higher cap or more memory could change the order; this run does not say.

## 2026-10-24 — SemML decides fewer inputs than 2.7.0 at both caps

Archive `semml-syntcomp26-20261024` (release `evidence-2026-10-24-semml`). SemML (gitlab.com/live-lab/software/semml at 20ad6ec7, default preset, `--target real`) was built with Temurin JDK 21 and run on the 1524 SYNTCOMP26 TLSF inputs at 17 s, then at 60 s on the 253 it left undecided, each call in its own 8 GiB scope with no swap. Its launcher asks for a 60 GiB heap; a wrapper caps it at 7 GiB and maps the printed verdict to exit codes, because SemML exits 0 on both. The 2.7.0, ltlsynt 2.16 and 1.x rows are those of the entry below.

| | 17 s solved | 17 s PAR-2 (s) | 60 s solved | 60 s REAL | 60 s UNREAL | 60 s PAR-2 (s) |
|---|---:|---:|---:|---:|---:|---:|
| 2.7.0 | 1342 | 6792.7 | 1358 | 624 | 734 | 21057.3 |
| SemML | 1271 | 11690.0 | 1334 | 591 | 743 | 27788.0 |
| ltlsynt 2.16 | 1264 | 9420.7 | 1290 | 594 | 696 | 29523.6 |
| 1.x (TACAS'23) | 812 | 24778.5 | 854 | 480 | 374 | 82407.0 |

At 60 s, 2.7.0 decides 90 inputs SemML does not and SemML decides 66 that 2.7.0 does not (111 to 40 at 17 s). SemML decides more unrealizable inputs than 2.7.0 (743 to 734). Its fastest solve takes 0.47 s, mostly JVM start-up, which accounts for much of its PAR-2 gap on the easy inputs. No resource failure; the largest scope peak is 7.89 GiB. No verdict disagrees with any of the other three tools.

## 2026-10-24 — 2.7.0 against ltlsynt and 1.x at 60 s

Archive `rel27-60-20261023` (release `evidence-2026-10-24-rel27`). For the v2.7.0 release notes, the release binary (ed481505, `otf_sparse_formula_parity`) reran the 182 inputs that timed out at 17 s in `bddgrowth-20261023`, at 60 s, each call in its own 8 GiB scope with no swap. The 17 s rows of 327c23db keep their verdicts and times; 327c23db and ed481505 differ only in the dependency pins and the changelog. The ltlsynt 2.16 and 1.x (TACAS'23) rows are the archived 60 s rows from `threeway-syntcomp26-20261019`, derived the same way.

| | Solved | REAL | UNREAL | Timeouts | PAR-2 (s) |
|---|---:|---:|---:|---:|---:|
| 2.7.0 | 1358 | 624 | 734 | 166 | 21057.3 |
| ltlsynt 2.16 | 1290 | 594 | 696 | 230 | 29523.6 |
| 1.x (TACAS'23) | 854 | 480 | 374 | 574 | 82407.0 |

The rerun solves 16 more inputs (9 REAL, 7 UNREAL) with no resource failure. 2.7.0 decides 80 inputs ltlsynt does not and ltlsynt decides 12 that 2.7.0 does not; against 1.x the split is 513 to 9. No verdict disagrees with either tool or with the 2.6.0 rows.

## 2026-10-23 — A 4M-node BuDDy growth step solves four more inputs

Archive `bddgrowth-20261023` (release `evidence-2026-10-23-bddgrowth`). The sprint above left BuDDy's node-table growth step (#301) off by default. To choose one, the owner asked for a full-corpus run that ships 4M nodes only if no input loses a solve, flips its verdict or gains a resource failure. Paired full corpus (1524 SYNTCOMP26 inputs, 17 s, eight chunks alternating which binary runs first, each call in its own 8 GiB scope with no swap), master 50a12cc7 (step off) against 327c23db (default 4M nodes), configuration `otf_sparse_formula_parity`:

| | Solved | Timeouts | PAR-2 (s) | CPU (s) | Largest peak |
|---|---:|---:|---:|---:|---:|
| step off (50a12cc7) | 1338 | 186 | 6895.6 | 18887 | 5.11 GiB |
| 4M nodes (327c23db) | 1342 | 182 | 6792.7 | 18468 | 5.38 GiB |

The 4M build gains 08, amba_case_study_unreal_pb_2, robot-grid-reach-repeated-with-obstacles-6d-real and round_robin_arbiter_unreal1_pb_4_7, all solved between 15.3 and 16.7 s, and loses none. No verdict is opposite and no input hits a resource limit. The 103 s PAR-2 drop is outside the 12.1 s noise floor, but it rests on near-cap solves: amba_case_study_unreal_pb_2 was also solved by 50a12cc7 in the sprint run above (1339 there, 1338 here). Wall time improves by 4–5 s on finding_nemo_pb_5, collector_v2_pb_15, ltl2dba_theta_pb_16 and load_balancer_unreal1_pb_5_5. One input is more than 1 s and 10% slower (amba_decomposed_arbiter_pb_6, 3.1 → 4.4 s). Memory is the cost: scope peaks rise more than 10% and 64 MiB on 205 inputs and fall on one. The median ratio is 1.31 on inputs that peaked above 256 MiB, and the largest ratio is 2.45, and the largest rise is finding_nemo_pb_7 at 1.36 → 3.32 GiB. All peaks stay below 5.4 GiB.

The default is now 4M nodes; `-Dacacia_bdd_max_increase=0` keeps Spot's own step.

## 2026-10-23 — Cheaper MONA decoding solves five more inputs

Archive `perf-20261022` (release `evidence-2026-10-23-perf`). This sprint resolved the SIMD and memory review issues (#301–#311, #314; review evidence `perfreview-20261022` and `perfreview-followup-20261022`). Control B0 is master 1ee199bf, frozen; configuration `otf_sparse_formula_parity`. Every solver call ran in its own 8 GiB scope with no swap, serially, with CPU time collected. Each candidate was screened on a 42-input panel picked from the review's measured behaviour (precompute-bound, translation-bound, forward and backward search, sparse UNREAL, large-AP and fast controls) with the full portfolio at 17 s and three alternating reps per binary. The keep rule asked for no verdict change, no new resource failure, and a CPU or peak gain that separates across reps on the inputs the issue targets.

| Change | Screen against B0 | Outcome |
|---|---|---|
| Decode MONA endpoints straight from minterm assignments (#302, PR #320) | 33 vs 31 solved (06 and Alarm_a5f99bc6 newly solved), PAR-2 572.0 → 514.6 s; CPU −21% to −46% on seven precompute-bound inputs. Peaks rise on four inputs without a wall-time change, the largest 566 → ~990 MiB. Byte-identical action output on 1163 of 1524 corpus inputs at 17 s, the rest not comparable (timeout or translation OOM on both) | kept |
| Explicit guard letters for at most 12 active APs (#314, PR #321) | Against #302: 06 9.1 → 6.5 s, collector_v3_pb_9 7.3 → 5.5 s, CPU −12% to −26% and peak −12% to −35% on four inputs. Byte-identical to #302 on 1167 corpus inputs, 0 mismatches; 10 not comparable, 3 of them one-sided near-cap at 17 s | kept |
| Forward hash reuse, multimap state index, local row sums (#303, #306, #307, PR #317) | Same 31 solved; CPU −28% robot_grid_pb_4_4, −7% collector_v1_pb_9, −6% 05 | kept |
| Integer class IDs in the symmetry search (#310, PR #318) | Same 31 solved; CPU −15% on collector_v1/v3_pb_9 | kept |
| BuDDy node-table growth step as a build option (#301, PR #319) | At 4M nodes: 32 solved, PAR-2 530.8 s, CPU −6%; peaks +11% to +95% on 26 inputs (largest 2.23 → 2.93 GiB). 1M and 16M bracket it | kept off by default |
| One SCC pass for saturation (#304) | Identical masks on 1524 automata; no gain; peak +21% on collector_v1_pb_9 | rejected, tag `provenance/perf-304-d9e85224` |
| Absent thresholds returned uncached (#309) | CPU −6% on one input and +10% on another | rejected, tag `provenance/perf-309-d400ecc8` |
| posets move storage and dropped bucket index (#305, #308; merged upstream as posets eb41f3d) | No separated gain; peak +19% on one timeout row | pin not bumped, tag `provenance/perf-305-308-72c545f5` |

The SIMD switch description (#311, PR #315) changed text only and was not measured.

Paired full corpus (1524 SYNTCOMP26 inputs, 17 s, eight chunks alternating which binary runs first), B0 against master 50a12cc7 with every kept change (CINT):

| | Solved | Timeouts | PAR-2 (s) | CPU (s) | Largest peak |
|---|---:|---:|---:|---:|---:|
| B0 | 1334 | 190 | 7020.2 | 19193 | 5.22 GiB |
| CINT | 1339 | 185 | 6878.5 | 18881 | 5.13 GiB |

CINT gains 06, 07, Alarm_a5f99bc6, amba_case_study_unreal_pb_2 and collector_v2_pb_15, and loses none. No verdict is opposite, and no input is more than 1 s and 10% slower. The 142 s PAR-2 drop is far outside the 12.1 s noise floor. Wall times halve on several precompute-bound solves: 06 17.0 → 6.7 s, collector_v3_pb_9 9.2 → 3.8 s, 05 12.1 → 5.8 s, 09 7.4 → 3.7 s. Without a regression to resolve, no 60 s pass was run. The byte-identity checks beyond 17 s were stopped early by the owner's call. #302's 60 s rerun finished 101 of its 356 undecided inputs: 19 newly identical, 79 still timing out on both sides, 3 finishing on one side only, no mismatch. #314's check skipped the 347 inputs on which #302's own 17 s check timed out on both sides.

Gates on CINT: G0, G1 (40/40 against B0, no verdict change or coverage loss), G2s (proxy pass to G3: geometric mean 2.35% better, best target 10.24%), G3 (both panels pass), G4 and G5 (1579 inputs, no opposite verdict or frontend error) pass.

BDD node creation inside Spot's translation still dominates the slow grid and workstation inputs, which none of these changes touch.

## 2026-10-21 — Leaner TLSF preparation and forward results, no corpus speedup

Archive `refopt-20261021`; master d3746918 frozen as the control (B0), the shipping configuration `otf_sparse_formula_parity`, serial 8 GiB scopes, CPU time collected. Screens used a 24-input panel picked from `costmodel-20261020` and `spotfast-ablation-20261019` labels (forward-search, sparse-proof, large-source, Moore, near-cap and control inputs), full portfolio, 17 s, three rotated reps per binary.

| Change | Screen against B0 | Outcome |
|---|---|---|
| Forward safety builds its dense replay snapshot only on request (#295) | 20/24 solved by both; −1.01% allocated bytes on both forward-search inputs (DHAT), peak unchanged; instructions −0.15% to 0% | kept |
| Sparse derived statistics computed only on request | ≈0 bytes and ≈1e-7 instructions | rejected, tag `provenance/refopt-p1b-70d1eca7` |
| Parity and obligation workers reuse the parent's TLSF preparation (#296) | 20/20 solved, no opposite verdict, per-input CPU geometric mean 0.964; separated on large sources only: CPU −31%/−23%/−7%/−7.5%, wall −24%/−20%/−7%/−7% on the four largest | kept |
| TLSF converted through tlsf-tools' conversion-only entry point (#297, tlsf-tools#75) | old and new paths byte-identical on 1586 TLSF files; conversion of the largest sources 33% faster with 20% lower peak RSS (0.244 → 0.163 s, 206 → 163 MB) | kept |

The parse count falls from 80 to 56 over 64 invocations with the reuse. The conversion-only change saves 0.03–0.08 s per conversion, below the panel's noise, so it was measured directly instead of screened.

Paired full corpus (1524 SYNTCOMP26 inputs, 17 s, eight chunks alternating which binary runs first), master B0 against the combined stack 5e7a4bb1 (C3):

| | Solved | PAR-2 (s) | CPU (s) |
|---|---:|---:|---:|
| B0 | 1336 | 6979.2 | 19162 |
| C3 | 1335 | 7003.2 | 19197 |

No opposite verdict and no conflict. The single difference is `06.ltl`, solved by B0 at 16.81 s. In three more paired runs at 17 s, B0 solved it each time (16.57–16.96 s) and C3 did not; in the six panel runs earlier the same day B0 timed out each time. At 60 s both solve it every time with overlapping times (B0 17.26–18.51 s, C3 17.53–18.71 s), so it is a near-cap input, not a separated loss, though C3 may be up to about 2% slower on it. The rest of the PAR-2 gap is within the 12.1 s noise floor. The stack saves resources where TLSF preparation dominates and simplifies the frontend; it does not speed up the corpus.

Gates on C3: G0, G1 (40/40 against B0, no verdict change or coverage loss), G3 (both panels tied: 163/180 and 167/180), G4 and G5 (1579 inputs, no opposite verdict) pass. G2s fails its preselected optimization rule at −0.20% geometric: its LTL targets do not touch TLSF preparation, and no target regresses past the ceiling.

## 2026-10-20 — isl does not compress Acacia's downsets

Archive `islprobe-20261020`; offline only, no build of Acacia. The question was whether a downset backed by isl, Verdoolaege's integer set library, could hold an antichain more compactly as a union of boxes merged by `isl_set_coalesce`. Winning-region antichains were captured through the published Python bindings (acacia-boomslang 2026.2.2) on the 40 slowest inputs the backward arm decides as realizable, and coalesced with islpy 2026.2.2.

The bindings returned 13 realizable regions; 21 came back unrealizable through their simpler pipeline and 6 ran past 60 s. Twelve of the 13 regions are a single maximal vector (5 to 10480 dimensions), so there is nothing to merge. The remaining one, `collector_v1_pb_9_pe_`, has 512 maximal vectors in 2827 dimensions with entries in {−1, 0, 1}. On its 531 distinct coordinates, coalescing 8 boxes leaves 8 basic sets (43 s) and 32 boxes leave 32 (197 s); 128 boxes did not finish in 600 s. A staircase of boxes is not convex, so isl keeps one piece per element, and each operation costs seconds where the dominance check costs microseconds. Not pursued; isl would also have been a new dependency.

## 2026-10-20 — A feature-based arm selector finds no coverage on 4 cores

Archive `costmodel-20261020`; frozen d68568db (revision 8c77fcfc), SYNTCOMP26, serial 8 GiB scopes, CPU time collected. Each of the seven default arms was run alone through `--arms`: on all 1524 inputs at 2 s, and on the 311 hard inputs (race at least 0.5 s or undecided in `spotfast-ablation-20261019`) at 17 s. No two arms gave opposite verdicts; 1352 inputs are decided by some arm alone. Features are the TLSF source counts from `tlsfbenchgraph --source-features` (interface sizes and `guard_*` operator counts and depths); models are random forests evaluated with 5-fold cross-validation grouped by family.

The race on four cores was simulated as the fastest chosen arm's solo time. Without a contention penalty, the simulation reproduces the measured gap between the seven-arm default and the four-arm subset in `route-20261019` (4 hard inputs in both). *Corrected 2026-10-21:* this agreement on one aggregate number does not show that arms do not slow each other; the measured four-arm subset's 8 subset-only solves in `route-20261019` are real race effects that a minimum-of-solo-times model cannot produce.

| Arms per input (4 cores, 17 s) | Solved | Lost vs. all seven | PAR-2 (s) |
|---|---:|---:|---:|
| All seven | 1352 | 0 | 6459 |
| Four-arm subset of `route-20261019` | 1338 | 14 | 6954 |
| Best four chosen on the training folds | 1326 | 26 | 7359 |
| Model's top four | 1336 | 16 | 6990 |
| Parity plus the model's top three | 1339 | 13 | 6888 |
| Best four per input (oracle) | 1352 | 0 | 6459 |

No selector gains an input. *Corrected 2026-10-21:* the oracle row follows from the model, not the data: scoring a set by the minimum of its solo times makes all seven arms weakly dominate every subset, and even an oracle choosing one arm per input matches them. The supported conclusion is narrower: the tested source-feature predictors do not justify pruning arms.

Dropping only the arms the model gives under 3% chance of deciding (the threshold chosen on the other folds) loses no input and cuts simulated race CPU by 10.8%, mostly on the hard inputs and mostly the obligation, GR(1) and backward arms. At 4% it loses 5. Rules shallow enough to write as a few comparisons save at most 3.2% without a loss. Not admitted: it is simulated rather than measured, depends on an embedded forest, and sits one threshold step from losing inputs.

## 2026-10-20 — Neither TLSF normalization nor a 4-core arm subset helps

Archive `route-20261019`; release build of fc229b77 (9e56d40d) carrying opt-in
`--parity-normalization`, `--legacy-normalization` and `--memory-share-workers`, kept
unmerged at tag `provenance/fc229b771a7e` (tlsf-tools `provenance/61f3a4f77a4f`); control the frozen d68568db (src identical
to master 6c8067ef). SYNTCOMP26, 17 s, serial 8 GiB scopes, cooled to 70 °C, rotated order.

**Normalization.** A 40-input panel (37 family groups, seeded, drawn from the
`spotfast-ablation-20261019` attribution rows) was run alone on three routes
(`both:parity:spot`, `real:small:backward`, `unreal:formula:spot-guarded-sparse`)
under off, match-safe and route-safe. Match-safe changes 39 of the 40 objectives and
route-safe 36; inputs whose effective objective equals the off one were skipped
(1 and 4 per route). No route changes a decision under either policy. Paired PAR-2
moves by at most 1.0 s per route, and the largest per-input change is 1.3 s.
Normalization itself costs at most 61 ms. The candidate with every option off
reproduces the frozen control (30/40 at 17 s, PAR-2 377.1 vs 376.9 s, identical results).
CPU time was not collected in this screen; wall time and scope memory peaks were.

**Four cores.** On CPUs 0–3 (four physical cores), memory allowances held at the
seven-arm values. Parity alone and parity with GR(1) lifting each lose 6 of the 40 panel
inputs. The four-arm subset (parity, GR(1) lifting, backward, sparse formula) solves
31 against 30 on the panel; the extra input, `robot-grid-reach-repeated-with-obstacles-6d-real`,
repeats in three alternating runs (13.0–13.2 s against three timeouts). On 348 targeted
inputs (the 190 eight-core timeouts, 32 inputs decided after 4 s, and every input a
dropped arm won):

| 4 cores | Solved | PAR-2 (s) | CPU (s) | Sum of memory peaks (GiB) |
|---|---:|---:|---:|---:|
| Seven-arm default | 157 | 6816 | 13998 | 272.6 |
| Four-arm subset | 153 | 7015 | 12812 | 233.7 |

The subset gains 8 inputs, all decided between 12.3 and 16.9 s, and loses 12 that
the default decides in 0.1–1.8 s (two in 6.5 and 13.8 s), all decided only by the
dropped forward and obligation arms. Freeing cores saves 8.5% CPU and 14% memory but
costs coverage, so nothing is admitted and the flags stay unmerged.

## 2026-10-19 — Same-session three-way on SYNTCOMP26 (v2.6.0)

Archive `threeway-syntcomp26-20261019`; ltlsynt 2.16, Acacia-Bonsai 1.x (TACAS'23) and
v2.6.0 (d2870f79, frozen 7db1df7b) in one session on the 1,524 SYNTCOMP26 inputs,
native TLSF, serial 8 GiB scopes. The 60 s column reruns only inputs undecided at 17 s
and recycles the rest. The 1.x rows that hit the memory cap and printed REALIZABLE
(3 at 17 s, 17 at 60 s) are counted as resource limits, not solves. Plots and tables
are on the v2.6.0 release.

| Tool | 17 s solved (R/U) | 17 s PAR-2 (s) | 60 s solved (R/U) | 60 s PAR-2 (s) |
|---|---:|---:|---:|---:|
| ltlsynt 2.16 | 1264 (581/683) | 9421 | 1290 (594/696) | 29524 |
| Acacia-Bonsai 1.x | 812 (457/355) | 24779 | 854 (480/374) | 82407 |
| Acacia-Bonsai 2.6.0 | 1335 (610/725) | 6985 | 1353 (622/731) | 21583 |

Against ltlsynt, v2.6.0 alone decides 81 inputs and ltlsynt alone 10 at 17 s (79 and 16
at 60 s); no verdict disagreements.

## 2026-10-19 — The antichain arms' Spot shortcut adds nothing to the default

Archive `spotfast-ablation-20261019`; one release build of 8c77fcfc (d68568db), the
first `docker_default` member, with `--spot-fast` at its default and `off`, paired at
17 s on the full panel.

| `--spot-fast` | Solved (R/U) | PAR-2 (s) |
|---|---:|---:|
| default (`det`) | 1334 (609/725) | 6995.4 |
| `off` | 1334 (609/725) | 6996.4 |

No input is decided by only one side and no verdicts disagree; per-input time ratio
geomean 1.000. The parity arm covers what the shortcut decides. The default is kept.

The same archive holds a 17 s attribution pass with `--route-records` (shortcut wins
labelled `spot_fast` since #287), plus the 18 inputs v2.6 decides only at 60 s. Of the
1,335 inputs decided at 17 s, 511 are won by Spot (parity or obligation arm, or the
shortcut) and 823 by the antichain or OxiDD arms; 1 has no winner record. 146 of these
winners have dropped phase records and are counted by their recorded route. The
shortcut wins 90; all 90 are decided with it off. The 60 s additions split 10 Spot and
8 antichain/OxiDD.

## 2026-10-19 — Gates for the strpm removal (#284)

Archive `gates-284-20261019`; candidate 36c0fcdf (6fd831be) against v2.6.0 (d2870f79).
G1 40/40, PAR-2 101.9 to 34.9 s; G2s 7.3% fewer cycles; G3 solved counts 149→150,
163→162 and 167→167, where the SYNTCOMP25 difference is one input at the cap that both
decide on a 51 s remeasurement (17.4 vs 16.8 s), so no coverage loss; G4 593 ok and 31
permitted timeouts; G5 no opposite verdicts.

## 2026-10-18 — Tier 4 needs no strpm-simd (#282)

Archive `tier4-spot282-20261018`; one diagnostics build of 176b321c, tier 4 forced to
Spot's `solve_parity_game` or left on strpm-simd, paired per input with rotated order,
serial 8 GiB scopes, cooled.

| Inputs | strpm-simd | Spot only |
|---|---:|---:|
| 223 tier-4 wins of `otd-OTF-owner20261016` at 17 s | 223 decided, PAR-2 202.8 s | 223 decided, PAR-2 201.3 s |
| 8 further tier-4 wins at 60 s | 8 decided | 8 decided |

No verdict conflicts. Game construction and solve took 41.6 s with strpm-simd and
39.5 s with Spot (per-input geometric mean 0.88); no input is more than 1 s slower
either way. strpm-simd needed Spot's fallback on 28 of the 223. The port is removed
and tier 4 solves with Spot directly.

## 2026-10-18 — Shipping selection: keep the four members

Archive `ship-select20261018`: fresh 17 s full-corpus rows on master 45840ec3 for the
three non-default `docker_default` members. The first member's row is
`otd-OTF-owner20261016` (frozen 74eeebee), so this is not a same-session comparison.

| Member | Arms | Solved (R/U) | PAR-2 (s) | Timeout CPU/wall | Scope peak p90 / max |
|---|---:|---:|---:|---:|---:|
| `otf_sparse_formula_parity` | 7 | 1339 (613/726) | 6928 | — | 0.92 / 5.24 GiB |
| `otf_sparse_formula_gr1_real_prepass` | 6 | 1226 (573/653) | 11031 | 4.1 | 0.86 / 5.11 GiB |
| `otf_sparse_formula` | 5 | 1199 (552/647) | 11880 | 3.6 | 0.80 / 2.52 GiB |
| `best_decomp_rank_bucketed_semantic_mona` | 0 | 1067 (517/550) | 16358 | 2.7 | 0.80 / 1.70 GiB |

No verdict conflicts. The four together decide 1,340: the only addition over the first
member is `round_robin_arbiter_unreal1_pb_4_7_pe_` at 16.4 s, inside the near-cap
window. Owner decision: keep the four in this order. The second is the no-parity
fallback, the third the lean member, the fourth the synthesis member. Synthesis
success and controller size are unmeasured (#280).

## 2026-10-18 — G5 passes without a waiver (#265)

Archive `g5-tlsf-tools20261018`; shipping preset built from 232f3fe8, SYNTCOMP26, 17 s.

All 1,524 inputs convert: SyFCo for 1,517, the tlsf-tools inspector for the seven
`finding_nemo` inputs whose strong next SyFCo rejects (listed as not independently
cross-checked). 0 opposite verdicts, 0 frontend errors, spot checks 10/10;
17 native-only and 1 converted-only answers. GATE PASS.

## 2026-10-17 — Arms do not contend on 8 cores (#277)

Archive `armaccounting20261017`; build be2cdef0 with `ACACIA_PHASE_RECORDS`, 300-input
panel at 17 s (185 default timeouts, 15 near-cap, 100 controls), 8-core i7-11850H.
Recording overhead on the controls: median wall 0.97–0.98x, CPU 1.05–1.06x, no
verdict change.

On the timeouts every arm's worker uses about 16.9 s of CPU in 17 s and a median of
0.2–0.3 GiB (GR(1) up to 4.1 GiB); the cgroup peak has median 1.2 GiB, max 5.2 GiB.
Exactly matched duplicate work across arms is about 160 s of CPU on the whole panel,
under 1% of the timeouts' 16.5k s. Cancelled losers stop within about 5 s of CPU in
total; post-decline CPU is GR(1)'s exact-solve fallback and the backward arm's
continuation after its optional pre-pass. Sharing, staging and a launch classifier
have no measured upside on this machine; #277 was closed on this 8-core evidence,
with fewer-core settings left unmeasured.

## 2026-10-17 — Parity arm translation: no general setting wins (#261)

Archive `parity261-translation20261017`; Spot 2.16 `ltl2tgba`, the 36 tier 1–3 inputs
of `otd-parity-engines20261015` (5 tier 1, 1 tier 2, 30 tier 3), 60 s per command.
Each variant changes one setting of the production translation and is checked
against it for tier, verdict and language. The automaton dumps (39 GB) are not
packed; `large-outputs.sha256` lists them and `experiment/run.sh` regenerates them.

| Variant | Translation, matched inputs | Failures | Tier/verdict changes |
|---|---|---:|---:|
| production (`--high`) | 26.8 s on 36 | 0 | — |
| `--medium` | 7.0 s vs 19.4 s on 35 | 1 | 0 |
| `--low` | 15.0 s vs 3.2 s on 34 | 2 | 0 |
| `tls-impl=0` | 27.9 s vs 26.8 s on 36 | 0 | 0 |
| simplification off | 47.4 s vs 19.3 s on 32 | 4 | 0 |
| `--any` (no determinization) | — | 34 nondeterministic | — |

Translation is concentrated in three inputs: `workstation_resupply_pb_5` (16.3 s),
`robot-grid-reach-repeated-with-obstacles-6d-real` (7.4 s) and
`workstation_resupply_pb_4` (1.5 s); the other 33 take under 0.4 s each.
`--medium` cuts the two workstation inputs to 4.1 s and 0.4 s but the robot-grid
input no longer finishes in 60 s, so it trades one input for another rather than
dominating. Splitting construction from determinization is far slower (30–60+ s
per input) than Spot's integrated deterministic translation.

Tier-3 conversion effort (`autfilt` low/medium/high) makes no measurable difference, and
8 of the 30 tier-3 inputs are outside its scope. Going straight to tier 4 (no
Büchi conversion, strpm solve) is 0.79× overall; on `pb_5` the solve takes 50 s
against 0.1 s for the Büchi game. The separate-process probe's orient/classify/game
times are dominated by HOA re-parsing and are not production costs; only
translation is compared.

No setting changes. A medium-then-high fallback would be fitted to two inputs.

## 2026-10-17 — No default arm can be dropped (#264)

Archive `portfolioloo20261017`; frozen OTF binary 74eeebee, SYNTCOMP26, 17 s.

Leave-one-out: removing any one of the seven default arms and rerunning the inputs
that arm alone decided loses them all, with no verdict conflicts.

| Arm removed | Inputs lost |
|---|---:|
| `both:parity:spot` | 112 |
| `both:gr1-real-lift:oxidd` | 17 |
| `real:small:backward` | 8 |
| `unreal:formula:spot-guarded-sparse` | 8 |
| `real:small:forward` | 5 |
| `both:obligation:spot` | 5 |
| `unreal:automaton:forward` | 1 |

Contention: the 185 default timeouts rerun with the full default, without
`unreal:automaton:forward` and without `real:small:forward`. The fresh default
decides none of them. Dropping `unreal:automaton:forward` decides one
(`robot-grid-reach-repeated-with-obstacles-6d-real`, REALIZABLE at 14.7 s), which
only offsets the one input it alone decides; dropping `real:small:forward` decides
none. At 17 s freeing an arm's CPU and memory does not pay for the arm, so the
default keeps all seven. Per-arm CPU and memory attribution needs instrumentation
the frozen binary lacks; that measurement and the sharing/staging question follow
on a new build.

## 2026-10-16 — OTD close: ship the parity portfolio

Owner decision: ship OTF, `otf_sparse_formula_parity`, as the first
`docker_default` member; retain `otf_sparse_formula_gr1_real_prepass` second.
OTF extends the previous shipping portfolio with `both:parity:spot`. Campaign
`otd-OTF-owner20261016` supplies the generated rows below; closeout runs only
unit tests and static checks. Archive publication and registry rows are the driver's
follow-up, not a claim made by this source update.

Owner exception, 2026-10-15, THIS campaign only: frozen OTF is measured on the full 1,524-input corpus at 17 s; the primary 60 s column follows the 2026-10-16 derivation disclosed below. The standing rule that the Acacia baseline is always rerun remains in force. OTF-vs-archived-master and OTF-vs-archived-ltlsynt comparisons are INDICATIVE: master is a different revision and both references are from separate sessions. Archived master is PMC4 / PR #244 head 188cef33, not frozen OTM 06e32c99. Fresh OTF and archived master use native TLSF end-to-end wall clocks (including parsing; fresh OTF also charges the policy/log wrapper). Archived ltlsynt uses preconverted LTL and excludes SyFCo conversion; its Strict-semantics limitation and conversion failures remain historical limitations. Its PAR-2 is reported separately on that historical clock, with no cross-clock timing delta, speedup, or combined score. The ltlsynt 60 s column is derived, not a fresh full-corpus 60 s pass. Selected inputs use three cooled OTF repetitions and frozen OTM according to the owner shortcut below; confirmations annotate differences and never replace primary rows or establish a full-corpus matched claim.

Owner decision 2026-10-16: the primary OTF 60 s column is derived, following the archived ltlsynt labelling. Retain every measured-60 row, including existing measurements of inputs already decided at 17 s. Otherwise recycle a decided row from the complete digest-bound 17 s pass, retaining its original time and marking it recycled-17. Only 17 s undecided inputs missing a 60 s measurement are run. The report gives measured and recycled counts next to the archived-reference disclosure. Winner tally uses phase records and run indices from whichever run supplied each row. The archived ltlsynt 60 s reference likewise contains 1,270 recycled 17 s rows plus 254 measured 60 s rows.

Confirmation selection per cap is the union of every coverage difference between OTF and archived master (a completed verdict on exactly one side, including timeouts, memory failures and errors on the other) and every input where either side completed a verdict in [0.8*cap, cap] inclusive. Common unsolved inputs, including common timeouts and failures, are excluded from confirmation and reported as common unsolved. The windows are [13.6,17] s and [48,60] s. Every selected input requires three cooled OTF repetitions. Frozen OTM runs repetition 1 everywhere; only after that repetition finishes normally with both treatments complete, repetitions 2 and 3 skip OTM when archived master and OTM r1 are both undecided, preserving all three OTM runs for near-cap selections, archived-master decisions and OTM r1 decisions. Skips are explicit skipped-by-rule records. A primary difference reproduced in three full pairs is confirmed-paired; an eligible skipped gain with three reproducing OTF decisions is confirmed-shortcut. The report discloses the rule and OTM skipped-invocation counts per cap.

OTF 60 s: 551 measured at 60 s, 973 recycled from 17 s. Owner decision 2026-10-16: the primary 60 s OTF column is derived. Keep every measured 60 s observation; otherwise recycle a decided 17 s row from the same frozen binary, retaining its 17 s time. Run at 60 s only inputs not decided at 17 s and not already measured at 60 s. Provenance: measured-60 or recycled-17. Winning arms use phase records from the run supplying each row.

Serial, 8 GiB/no swap, memory.high=max including ancestors, no deadline, cooled to 70 C before every launch. One primary treatment; paired order rotates. No same-session full-corpus comparison or noise floor is claimed. All 1,524 rows remain in coverage/PAR-2 denominators. Primary observations and failed launch attempts remain preserved.

Confirmation selection (adjudication-plan schema version 2; repetition clause superseded by the owner shortcut below): Every OTF coverage gain/loss vs archived master, including timeouts, memory failures and errors on either side, OR a completed verdict by OTF or archived master in [0.8*cap, cap] inclusive. Inputs unsolved on both sides are excluded and reported as common unsolved. Selected inputs require 3 cooled frozen OTM/OTF pairs at the same cap.

Owner shortcut decision 2026-10-16, per cap: OTF has 3 cooled repetitions on every selected input. OTM repetition 1 covers every selected input and must finish normally with both treatments complete before repetitions 2 or 3. In repetitions 2 and 3 skip OTM only when archived master and OTM repetition 1 are both undecided; near-cap completed-verdict selections keep all 3 OTM runs. Archived-master decisions (including OTF losses) and OTM repetition-1 decisions also keep all 3 OTM runs. Record skipped-by-rule explicitly, never as missing or a timeout. An eligible skipped input is a confirmed OTF gain only when all 3 OTF repetitions reproduce the primary gain. Remaining order, cooling and conflict stops are unchanged.

OTM invocations skipped-by-rule per cap: {"17": 220, "60": 192}

### 17 s native end-to-end columns

| Column | Solved (R/U) | PAR-2 sum (s) | Mean (s) |
|---|---:|---:|---:|
| OTF (fresh-measured) | 1339 (613/726) | 6928.151 | 4.546 |
| master-archive (measured) | 1222 (569/653) | 11156.318 | 7.320 |

#### 17 s archived ltlsynt preconverted clock (separate)

Conversion excluded. Historical Strict limitation and 7 conversion failures retained. No native-vs-ltlsynt PAR-2 delta or speedup.

| Column | Solved (R/U) | Historical PAR-2 sum (s) | Mean (s) |
|---|---:|---:|---:|
| ltlsynt216-archive (measured) | 1263 (576/687) | 9421.852 | 6.182 |

OTF vs master-archive: **INDICATIVE**, +118/−1 solves. Common unsolved: 184.
Native-clock PAR-2 delta: -4228.167 s (descriptive; revision/session/wrapper differences disclosed). Confirmed vs frozen OTM in all 3 pairs: +1/−1. Confirmed OTF gains under shortcut: +110.

OTF vs ltlsynt216-archive: **INDICATIVE**, +85/−9 solves. Common unsolved: 176.

| Column | MEMOUT | Scope peak complete | Process RSS complete |
|---|---:|---:|---:|
| OTF | 0 | 1524/1524 | 1524/1524 |
| master-archive | 0 | 1524/1524 | 1524/1524 |
| ltlsynt216-archive | 0 | 1517/1524 | 1517/1524 |

Winning arms (worker:requested:effective:route): `{'0:backward:backward:backward': 138, '1:forward:forward:solve_game': 22, '0:backward:backward:real': 247, '2:spot-guarded-sparse:spot-guarded-sparse:solve_game': 72, '2:spot-guarded-sparse:spot-guarded-sparse:unreal_formula': 199, '5:parity:spot-fast:deterministic_buchi': 173, '3:forward:forward:solve_game': 64, '5:parity:strpm-simd:acd_parity': 223, '3:forward:forward:unreal_automaton': 2, '6:obligation:obligation:original_game': 23, '0:backward:backward:equivariance': 8, '4:oxidd:oxidd:direct': 25, '4:oxidd:oxidd:R': 6, '1:forward:forward:real': 1}`; 0 missing, 136 uncertain of 1339 completed OTF solves. accepted parent_winner phase events from the run supplying each row (measured-60 uses 60 s; recycled-17 uses 17 s), no deadline and no --route-records. Memory outcomes, separate scope peaks/process RSS, completeness, exact Acacia-only/ltlsynt-only sets, per-column provenance and every paired outcome are in summary.json and per-instance.tsv.

### 60 s native end-to-end columns

| Column | Solved (R/U) | PAR-2 sum (s) | Mean (s) |
|---|---:|---:|---:|
| OTF (derived) | 1354 (622/732) | 21540.570 | 14.134 |
| master-archive (measured) | 1255 (578/677) | 34165.000 | 22.418 |

#### 60 s archived ltlsynt preconverted clock (separate)

Conversion excluded. Historical Strict limitation and 7 conversion failures retained. No native-vs-ltlsynt PAR-2 delta or speedup.

| Column | Solved (R/U) | Historical PAR-2 sum (s) | Mean (s) |
|---|---:|---:|---:|
| ltlsynt216-archive (derived) | 1286 (589/697) | 29864.297 | 19.596 |

OTF vs master-archive: **INDICATIVE**, +99/−0 solves. Common unsolved: 170.
Native-clock PAR-2 delta: -12624.430 s (descriptive; revision/session/wrapper differences disclosed). Confirmed vs frozen OTM in all 3 pairs: +0/−0. Confirmed OTF gains under shortcut: +96.

OTF vs ltlsynt216-archive: **INDICATIVE**, +82/−14 solves. Common unsolved: 156.

| Column | MEMOUT | Scope peak complete | Process RSS complete |
|---|---:|---:|---:|
| OTF | 0 | 1524/1524 | 1524/1524 |
| master-archive | 0 | 1524/1524 | 1524/1524 |
| ltlsynt216-archive | 2 | 1517/1524 | 1517/1524 |

Winning arms (worker:requested:effective:route): `{'0:backward:backward:backward': 143, '1:forward:forward:solve_game': 22, '0:backward:backward:real': 246, '2:spot-guarded-sparse:spot-guarded-sparse:solve_game': 74, '2:spot-guarded-sparse:spot-guarded-sparse:unreal_formula': 199, '5:parity:spot-fast:deterministic_buchi': 176, '3:forward:forward:solve_game': 65, '5:parity:strpm-simd:acd_parity': 231, '3:forward:forward:unreal_automaton': 2, '6:obligation:obligation:original_game': 23, '0:backward:backward:equivariance': 9, '4:oxidd:oxidd:direct': 28, '4:oxidd:oxidd:R': 6, '1:forward:forward:real': 1}`; 0 missing, 129 uncertain of 1354 completed OTF solves. accepted parent_winner phase events from the run supplying each row (measured-60 uses 60 s; recycled-17 uses 17 s), no deadline and no --route-records. Memory outcomes, separate scope peaks/process RSS, completeness, exact Acacia-only/ltlsynt-only sets, per-column provenance and every paired outcome are in summary.json and per-instance.tsv.

### Column provenance and master revision gap

- master-archive 17 s: archive `postmem20261012-closing`, TSV SHA-256 `dda41008750ea430e565eb1cc0f8f0aa37aa2bc233021d3910bc8101d09b3579`, binary `b3953b437ede826eabfeb54a5d6f2deb3f79f59153189b7ae79a3c6b35b7951f`, revision `188cef33e8d5002109fca8d81becfd67c8524e70`, UTC 2026-10-12T11:04:05.731600Z–2026-10-12T15:08:38.647146Z, measured {'measured-17s': 1524}, clock `native-tlsf-end-to-end`.
- master-archive 60 s: archive `postmem20261012-closing`, TSV SHA-256 `fb30a8c1d0b2adfe49e4324db65addc1490fe609fd5119c7756e15d33b2f2ef0`, binary `b3953b437ede826eabfeb54a5d6f2deb3f79f59153189b7ae79a3c6b35b7951f`, revision `188cef33e8d5002109fca8d81becfd67c8524e70`, UTC 2026-10-12T15:10:51.292957Z–2026-10-13T02:39:27.617094Z, measured {'measured-60s': 1524}, clock `native-tlsf-end-to-end`.
- ltlsynt216-archive 17 s: archive `legacy20261008-closing`, TSV SHA-256 `f308a5ddf4e3ac09d44b49bfe9254a7a6f55b2bbad455144802f3a324147d678`, binary `fb05e43f140c9e546c828045c59b10df57091197130539c6a8b848ba8a437f8c`, revision `spot-2.16`, UTC 2026-10-08T22:29:32.196564Z–2026-10-09T07:29:39.940243Z, measured {'measured-17s': 1524}, clock `preconverted-ltl-conversion-excluded`.
- ltlsynt216-archive 60 s: archive `legacy20261008-closing`, TSV SHA-256 `655bbfe5ccdb8cd41493d9f3572ed69d75ad6e47741ccb0d82bee1cf92aa2b95`, binary `fb05e43f140c9e546c828045c59b10df57091197130539c6a8b848ba8a437f8c`, revision `spot-2.16`, UTC 2026-10-08T22:29:32.196564Z–2026-10-10T08:06:03.498607Z, derived {'measured-60s': 254, 'recycled-17s': 1270}, clock `preconverted-ltl-conversion-excluded`.

Fresh OTF provenance: {"flags": "", "label": "OTF", "preset": "otf_sparse_formula_parity", "revision": "74eeebee0120da7863d4c89036c4781e1d7b5dd7", "sha256": "1a846b909a10f8838d29cffc4ebc3f2dbb21f383ac9f58eba5c523759f6da32e", "tool": "acacia"}

```text
58dba0f1 Make the portfolio deadline test see every fake child
ed666cdf Merge remote-tracking branch 'origin/master' into fix/obligation-arm-misses
1a3f33e0 Trust the obligation arm's synthesized strategies too
53c3521b Merge remote-tracking branch 'origin/master' into diag/memory-blocked-attribution
c48592af Trust the obligation arm's decisions and check its controllers
234b9d28 Attribute native and search-stage progress in phase records
```

Confirmed-paired means the primary gain/loss reproduced in all three frozen OTM/OTF pairs. Confirmed-shortcut gains use the disclosed archived-master/OTM-r1 undecided rule and three completed OTF repetitions. Reversed, mixed and unreproduced differences remain disclosed; paired results never substitute for archived full-corpus master scores. There is no automatic admission decision.

### Confirmed outcomes and winner provenance

At 17 s, **+110 confirmed-shortcut gains and +1/−1 confirmed-paired** reproduce
against frozen OTM under the disclosed rules. Near-cap reversals remain disclosed:
`collector_v2_pb_15_pe_` has OTF 0/3 versus OTM 3/3 decisions; `07`, a primary
gain, has OTF 0/3 versus OTM 2/3. These confirmations never replace primary rows.
At 60 s, **+96 confirmed gains and zero confirmed losses**. There are **no
verdict conflicts**. Raw full-corpus coverage remains +118/−1 and +99/−0 against
archived master, separately from confirmation.

The parity arm produced **112 of the 118 new 17 s decisions** with complete winner telemetry; one more (`ordered-visits-choice-real`) is likely but its telemetry was dropped. Antichain arms
provide most of the decisions that archived ltlsynt lacks, so the portfolio's
complementarity remains useful. Winner telemetry is uncertain on **136/129 rows**
at 17/60 s; it does not justify inferring an arm for those rows. Accepted winner
records use the actual run supplying each primary row. Evidence:
`otd-OTF-owner20261016`.

### Gate decisions

G2s's membership-default 6% per-input cycle ceiling **failed on three inputs**:
`arbiter_with_buffer6` **+12.6%**, `arbiter_with_buffer_pb_5_pe_` **+32.9%**, and
`lift_pb_3_pe_` **+8.2%**. Cycles sum all race workers; the owner accepted these
regressions as the cost of the always-on parity worker, including where it does
not win. This remains an accepted failure, not a gate pass.

G5 failed only on **seven SyFCo strong-next conversions**, the
`finding_nemo_pb_{1..7}_pe_` inputs: SyFCo 1.2.1.2 cannot express strong next in
`ltlxba`. The owner explicitly waived that conversion exception (#265).
The remaining comparison has 1,499 MATCH, no opposite verdicts or frontend errors,
17 NATIVE_ONLY and one near-cap CONVERTED_ONLY; spot-check 10/10 and the conversion
check pass. Evidence for both decisions: `otd-gates-OTF20261016`.

### Package decisions and engine selection

- **A — obligation memory sharing:** the measured memory-share-alone option gains
  `chomp_pb_3_6_pe_` at 180 s, reproduced in three repetitions at about 100 s.
  The default memory share is unchanged; this option is not shipped as the default.
  `screen.py` had overwritten each label's summary with the last one-input call;
  the correction regenerates the summary from all raw rows and the complete input
  list. The original observations are retained. Evidence: `otd-A-obligation20261015`.
- **B — implication simplification:** screen found one translation rescue that the
  worker cannot turn into a decision; 524 deferred boundaries were not run end to end.
  No production change. Evidence: `otd-B-implication20261014`.
- **S — downset rescreen:** negative; no replacement of the shipping downset
  representation. Evidence: `otd-S-downsets20261014`.
- **C — deterministic acceptance tiers:** the experimental conversion into the
  counting game is superseded by the parity arm's structural routing. Tiers 1–3
  retain deterministic Büchi and Spot's game solver; tier 4 retains parity and
  uses adaptive strpm-simd followed by Spot fallback. Evidence:
  `otd-C-parity-route20261015`.

The tier 1–3 engine comparison used 36 selected inputs at 60 s, serial 8 GiB
scopes, with no verdict conflicts. **Spot's explicit solver beats symbolic GR(1)**
on these tiers; keep Spot as the only production engine there.

| Tier 1–3 engine | Solved / 36 | PAR-2 total (s, approximate) |
|---|---:|---:|
| Spot explicit game solver | 36/36 | 44 |
| strpm-simd | 35/36 | 149 |
| Symbolic GR(1) | 34/36 | 265 |
| K-bounded antichain search | 33/36 | 427 |

These are selected-panel engine comparisons, not full-corpus scores. Evidence:
`otd-parity-engines20261015`. The [arm contract](coverage-first/parity-arm.md)
describes production trust (no recheck; independent checks in tests), timing and
GPL-3.0 port provenance; the [decision record](decisions.md) records
the owner exceptions and shipped default.

## 2026-10-13 — post-memory frontend close: admit the obligation arm (#244)

Campaign `postmem20261012-closing`: all 1,524 SYNTCOMP26 inputs, fresh matched
PMC4/PMM races, serial and rotated, cooled to 70 °C, 8 GiB with zero swap and
external caps without a solver-visible invocation deadline. PMC4 is #244 head
`188cef33`; PMM is post-memory master `b95777aa` containing #235/#240/#241.
Frozen default membership preserves PMM's five arms and adds `both:obligation:spot`.
The driver supplied the measurements; closeout ran no solvers.

| 17 s series | REAL | UNREAL | Solved | PAR-2 total (s) | PAR-2 mean (s) |
|---|---:|---:|---:|---:|---:|
| PMC4 candidate, fresh paired | 569 | 653 | 1222 | 11156.317827 | 7.320419 |
| PMM incumbent, fresh paired | 562 | 652 | 1214 | 11441.203366 | 7.507351 |
| ltlsynt216, archived separate-session reference | 576 | 687 | 1263 | 9421.852115 | 6.182318 |
| TACAS23, archived separate-session reference | 457 | 355 | 812 | 24803.908447 | 16.275530 |

| 60 s series | REAL | UNREAL | Solved | PAR-2 total (s) | PAR-2 mean (s) |
|---|---:|---:|---:|---:|---:|
| PMC4 candidate, fresh measured | 578 | 677 | 1255 | 34164.99997 | 22.417979 |
| PMM incumbent, fresh measured | 570 | 677 | 1247 | 35137.094832 | 23.055837 |
| ltlsynt216, archived derived separate-session reference | 589 | 697 | 1286 | 29864.296577 | 19.595995 |
| TACAS23, archived derived separate-session reference | 480 | 374 | 854 | 82460.128477 | 54.107696 |

**Admit the obligation arm: eight confirmed REAL gains, zero confirmed losses at
either cap.** The gains are `Automata32S.ltl`, `chomp_pb_2_4_pe_.ltl`,
`chomp_pb_2_5_pe_.ltl`, `chomp_pb_2_6_pe_.ltl`, `chomp_pb_3_3_pe_.ltl`,
`chomp_pb_3_4_pe_.ltl`, `chomp_pb_4_2_pe_.ltl`, `chomp_pb_4_3_pe_.ltl`.
Paired PAR-2 deltas (candidate minus incumbent) are **−284.885539 s at 17 s**
and **−972.094862 s at fresh 60 s**. No 60 s noise floor is assumed.

Raw 17 s outcomes have nine gains (eight REAL, one UNREAL) and one REAL loss.
Three cooled alternating repetitions adjudicate `07.ltl` as no confirmed loss
(candidate 2/3 solves, incumbent 0/3) and `g-unreal-310.ltl` as no confirmed gain
(3/3 for both). Counts and PAR-2 above retain the original observations.
Fresh 60 s gives exactly eight REAL gains and zero losses; no changed solves lie
within 2 s of that cap. Both fresh series have zero verdict conflicts, ERROR,
MEMOUT, CRASH, SYFCO-FAIL or AUDIT-ERROR. Their TIMEOUT/UNKNOWN counts are
301/1 and 309/1 at 17 s, and 268/1 and 276/1 at 60 s.

| Cap | Acacia series | Acacia-only vs ltlsynt | Common with ltlsynt | ltlsynt-only | Both unsolved | Net solved deficit |
|---|---|---:|---:|---:|---:|---:|
| 17 s | PMC4 | 78 | 1144 | 119 | 183 | 41 |
| 17 s | PMM (M) | 79 | 1135 | 128 | 182 | 49 |
| 60 s | PMC4 | 77 | 1178 | 108 | 161 | 31 |
| 60 s | PMM (M) | 77 | 1170 | 116 | 161 | 39 |

The historical LEGFINAL net deficits were 56/42 at 17/60 s. PMC4's deficits
are 7 REAL + 34 UNREAL = 41 and 11 REAL + 20 UNREAL = 31. These net counts
are distinct from ltlsynt-only sets. The comparator counts come from
`legacy20261008-closing` plus `legacy20261008-closing-amend1`, reused by owner
decision as separate-session references. Their 60 s observations remain derived.
Acacia includes native TLSF parsing in timed work; reference clocks exclude
preconversion and retain the SyFCo Strict-semantics limitation. TACAS23 retains
its killed-child signal-handling caveat: apparent solves are an upper bound,
with 3/14 audit-invalid verdicts rescored at 17/60 s.

Both Acacia 60 s series are **fully fresh measured**: completed 60-rerun and
60-recycled passes form exact disjoint 1,524-input unions, every source cap is
60 s, and no short-cap row is reused. The driver selected this fallback because
the unchanged recycling adjudicator retained candidate `03.ltl`'s parent winner
route configuration difference and two incumbent missing worker-sequence findings
without structured transport differences. Validation samples, strict/adjudicated
findings and hash-bound repetitions remain evidence; they do not gate these
fresh rows. `FINAL-REPORT.md` retains the earlier preparation narrative; the
completed `join-17/` and `join-60/` outputs supply these final numbers.

Scope peak and process RSS coverage is 1,524/1,524 per fresh series at each cap.
Maximum scope peaks are candidate/incumbent 5235.3/5235.2 MiB at 17 s and
5234.9/6257.3 MiB at 60 s; every fresh median is 62.7 MiB. References cover
1,517/1,524 rows, with seven unexecuted conversion failures; their partial
medians are suppressed. Missing/dropped route records remain incomplete, so
observed spans are neither CPU totals nor complete counterfactual attribution.

**Rejected idea:** expand `favor_event_univ` below the acceptance-width guard.
The broader translation screen gives **zero gains on all 883 changed inputs**, with 17 s losses on
`GF-G-contradiction6/7`, `g-unreal-39`, `infinite-race-unequal-25`, `sort42`,
`sort50`, and 60 s losses on `g-unreal-311`, `sort50`, `sort42`,
`infinite-race-unequal-26`, `thermostat-GF-unreal1`. Eleven of twelve loss
observations stall in translation. Keep the guarded default and retain
`feat/event-univ-policy` without a PR. Evidence: `postmem20261011-p1` and the
loss diagnosis in `postmem20261011-p0`. The bypass study was never decisive;
general construction sufficed on 55/57 inputs, while obligation synthesis
decided 11/15 Acacia-missed obligations (`postmem20261011-p2`).

See the [dated decisions](decisions.md) and
[arm contract](DATA-STRUCTURES.md#obligation-arm). Executables are archived as
`binary-PMM-99ca4055`, `binary-PMC1-d52363fa`, `binary-PMC2-1f64f527`,
`binary-PMC3-f2449fb4`, `binary-PMC4-b3953b43`. PMM records master's configuration;
once #244 merges, PMC4 is the same-configuration previous-revision G1 baseline
for the next change. Issues #245–#251 remain follow-ups. Archive modes are
normalized to 0644 (#245); restores must explicitly restore executable permissions.
The new index rows await the driver's upload and remote fetch-verification.

## 2026-10-11 — compact action payloads (#208, #200), development measurements

Development-only six-input profiles found 0.46–28.28% nonempty rows and
65.4–98.3% action-table savings. Robot construction moved from 8 GiB OOM to a
14.10 MB table and search, still capped at 60 s. Morning2s UNREAL-forward table
fell 5.48 → 1.73 GB, peak 7.93 → 3.76 GB, both capped at 180 s. These single
observations establish no corpus gain; the later issue-sweep admission is below.
Evidence: `legacy20261010-issue-sweep`, exact original narrative
`refactor-closeout20261017`.

## 2026-10-08 — admit the 3 s equivariance default (#224)

A fresh matched full-corpus 17 s screen, one binary with no invocation deadline,
solved 1,206 with the 3 s equivariance allowance versus 1,205 unbounded. Sole gain
collector_v3_pb_9 at 9.76 s, zero losses/conflicts, identical memory outcomes;
paired PAR-2 −23.3 s against a 12.1 s noise floor. Archive `cov20261007-eqfull`.

## 2026-10-07 — coverage-first screens: negative results (#201, #202, #204, #206, #209)

No-deadline cooled serial screens (`cov20261007-screens`) found no robust
admission gain from native structure-guard scaling, complementary GR(1), structural
variable order or R typed roles. All 103 mp-class duals still failed exact reduction.
Extended weakening made 41/82 frontier inputs eligible, but no proof at 17 s even
with 2 s/attempt and 8 s total: 181/215 attempts exhausted their allowance.
These historical parent-precheck observations remain historical after #259 removed
those prechecks; exact narrative is in `refactor-closeout20261017`.

## 2026-10-07 — matched R × equivariance ablation (#207)

Matched full-corpus 17 s R × equivariance ablation, one frozen binary with no
invocation deadline, archive `cov20261006-p1ablation`. The original rounded rows
are retained. Three REAL gains support R; one near-cap UNREAL loss was confirmed.
Disabling equivariance gave no marginal coverage win; its later bounded default
has separate admission evidence. This is a development corpus, not a holdout.

| Leg | R pre-pass | Equivariance | REAL | UNREAL | Solved | PAR-2 (s) |
|---|---|---|---:|---:|---:|---:|
| A | off | off | 556 | 648 | 1,204 | 11,752 |
| B | off | on | 555 | 648 | 1,203 | 11,777 |
| C | on | off | 559 | 647 | 1,206 | 11,701 |
| D (shipping) | on | on | 558 | 647 | 1,205 | 11,729 |
| INC (frozen) | on | on | 559 | 647 | 1,206 | 11,711 |

## Extended UNREAL weakening experiment, not admitted

Status: retired by the maintainer in #403 (A12); see the superseding
2026-11-17 entry in [decisions.md](decisions.md). The results below are historical.

Extended weakening remains opt-in. The supplied 82-input 17 s no-deadline screen
had zero gains: 136/149 extended attempts were cancelled, none proved UNREAL.
Broadened positive G/F scopes have correctness evidence, not fresh admission data.
The larger-allowance negative screen is above. Original protocols and distinctions:
`refactor-closeout20261017`, `cov20261006-p1ablation`, `cov20261007-screens`.

## 2026-10-06 — Basic UNREAL weakening instrumentation

Instrumentation and starvation corrections established no coverage gain. Missing
lifecycle/budget telemetry remains UNKNOWN. See [phase records](legacy-phase-records.md)
and exact historical implementation text in `refactor-closeout20261017`;
measurement campaigns: `cov20261006-p0exit`, `cov20261006-p1ablation`.

## 2026-10-06 — Worker and native-route attribution (#210)

Generated correctness/contract panels validated typed causes, parent acceptance,
killed-worker metadata and transport loss handling. These tests establish no
performance gain or exhaustive runtime branch coverage. The original report and
734-origin census are in `refactor-closeout20261017`; original campaign
`cov20261006-p0exit`. Living schema/cause contracts: [phase records](legacy-phase-records.md).

## 2026-10-06 — Gate and memory-collector implementation

The real Linux normal/timeout/child-OOM/invocation-OOM panel passed externally on
2026-10-06 (`cov20261006-p0exit`). Correctness fixtures validate G4/G5 and retained
cgroup collection. This is collector validation, not a speedup; historical incomplete
memory rows remain incomplete. Exact report: `refactor-closeout20261017`.

## 2026-10-04 — Optimization closing campaign and shipping default

Full 1,524-input measured 17 s rows and derived 60 s views from
`opt20260927-p4close`, with corrected gates in `opt20260927-p4gates`.
All 320 sampled recycling outcomes matched, but legacy-arm validation was
outcome-only (#210). Final versus S4 had +32/−0 solves at 60 s, +31/−3 at 17 s;
three S4-only solves were near the cap. Memory medians selected mostly MEMOUT
rows and cannot characterize the cohort. Conversion checker exceptions and advisory
G2s failure remain disclosed in the original report, `refactor-closeout20261017`.

| Series | 60 s solved | 60 s PAR-2 (s) | 17 s solved | 17 s PAR-2 (s) |
|---|---:|---:|---:|---:|
| ltlsynt 2.16 | 1,286 | 29,758.035 | 1,265 | 9,302.164 |
| Final candidate, 1234G-R | 1,238 | 36,118.463 | 1,205 | 11,652.535 |
| E5 | 1,228 | 37,415.465 | 1,194 | 12,027.355 |
| S4, master's four-arm default | 1,206 | 39,856.456 | 1,177 | 12,548.461 |
| TACAS23 | 847 | 83,039.001 | 811 | 24,788.493 |

## v2.4.2 released three-way

Released v2.4.2 three-way comparison, full 1,524-input SYNTCOMP26, 17 s,
8 GiB/no swap, `plots-spot-otf-threeway-20260909`. Virtual best is derived,
not runnable. Acacia 1.x may misreport killed workers and is not a sound speedup
baseline. Historical configuration names identify the actual measurements.

| Series | Solved | PAR-2 total (s) |
|---|---:|---:|
| ltlsynt 2.15.1.dev | 1,257 | 9,515.826 |
| Acacia `otf_sparse_formula` (arriving) | 1,171 | 12,736.344 |
| Acacia `best_four_arm_contradiction` (departing) | 1,123 | 14,254.294 |
| Acacia virtual best of those two presets | 1,173 | 12,684.211 |
| Acacia 1.x | 811 | 24,785.471 |

## E5 selection and per-arm legs, unreleased

Unreleased E5 selection from isolated 60 s legs over 1,524 inputs;
`gr1-par2-20260923-perarm-m1` and `opt20260927-p4close` preserve the historical
selection/closing context. Arm unions are derived virtual bests, not measured
races; the later native race replaced this candidate. Exact source text is in
`refactor-closeout20261017`.

| Isolated-leg subset | Solved / 1,524 | PAR-2 total (s), rounded |
|---|---:|---:|
| Previous four arms {1,2,3,4} | 1,217 | 38,787 |
| Best four {1,2,3,5} | 1,225 | 38,006 |
| Selected E5 {1,2,3,4,5} | 1,238 | 36,260 |
| Runner-up five {1,2,3,5,6} | 1,235 | 36,789 |

## O5 optimized checkpoint, unreleased

Selected 152-input 60 s screen, with rotated legs and pinned runtime provenance:
`opt20260927-o5`, `binary-O5-1b216a40`, `binary-C5s216-1c3b09ff`.
The same-Spot candidate/control solved set is identical; old E5 uses a different
runtime. The apparent first-pass coverage gain was cap-sensitive and not credited.
This selected screen is not a full-corpus gain.

| Binary | Spot | Solved | PAR-2 total (s) |
|---|---|---:|---:|
| E5 (frozen) | 2.15.1.dev fork runtime | 115 | 4,653.542 |
| C5s216 (C5 source, same Spot as O5) | 2.16 | 116 | 4,589.171 |
| O5 | 2.16 | 116 | 4,543.499 |

## Rejected or deferred ideas

- Removed experimental DPA route/share controls, classify-copy, negative-objective Büchi, Delta2 fallback and lazy parity arms (#365): no useful coverage gain. `pipeline-posets-20261103`: classify tier 4 on all 5 completed graphs; negative DBA constructor admitted 1/16 (timed out), solved 0/16, 15 syntax rejects; Delta2 added 2 constructor admissions, 1 extra solve (already solved by Spot: 0.23 s vs 9.8 s). `sprint-performance-20261102`: lazy BFS solved 0/9 load_balancer at 17 s. Default eager positive DPA and #365 telemetry fixes remain.
- Sparse forward move compaction: a confirmed near-cap regression defeated the
  five-pair gate. Archive `demand-sparse-20260916`.
- Scheduling hints in sparse unreal search: no confirmed deployed gain and a
  validated `g-unreal-115` slowdown. Archive `demand-sparse-20260916`.
- Replacing the real forward arm with real Spot sparse: five gains could not
  offset the systematic workstation loss. Archive `demand-sparse-20260916`.
- Fixed 14-family lifting registry: name/family-dependent selection contaminated
  the evaluation cohort; withdrawn. Archives `param-lift-20260922`,
  `gr1-par2-20260923`.
- Sequential Python lifting as the solver route: withdrawn for the source-bound
  native generic path; native differential oracles remain in tests. Archive
  `gr1-par2-20260923`.
- Broad structural witness selector: confirmed regressions at 17 s and 120 s.
  Archive `witness-lifting-20260918`.
- Symbolic-row treatment: no solver change was admitted after confirmation.
  Archive `symbolic-rows-20260917`.
- Semantic whole-letter quotient: G1 gains were outweighed by a severe G2s
  cycle regression. Archive `benchmarking-root-legacy`.

- Optimization candidates stopped at pre-registered gates:
  - critical-picker scratch reuse, for no useful gain;
  - sparse backward vectors (#196): 67% of coordinates are non-bottom, and the model reads
    2.56 times as much;
  - backward rank reuse and SIMD: rank work is 5.8% of cycles;
  - raising Spot's acceptance-set limit (#201): the specs need up to 5,052 sets, and
    translation still times out.

  Record: [decisions](decisions.md); archives `opt20260927-p2a`,
  `opt20260927-p2b1`, `opt20260927-p2b2`, `opt20260927-p2b3`,
  with exact narrative in `refactor-closeout20261017`.

## Legacy worker phase observation

Generated-spec and release transport checks validate identity, disabled-observer
behavior and completed/censored stage separation. They establish no bottleneck,
speedup or performance admission. Later measured attribution is below;
`legacy20261008-screens` and `refactor-closeout20261017` preserve original context.

## 2026-10-10 — grouped sparse guarded oracle admitted at campaign close (#235)

Closing full-corpus campaign `legacy20261008-closing`: all four 17 s series are
fresh and rotated; 60 s is derived from each series' measured short rows and fresh
tail, with strict validation and hash-bound adjudications. No incumbent loss at
either cap. Three near-cap UNREAL gains reproduced 3/3. The 28-input screen is
`legacy20261008-screens`; same-K search/check cost was 0.54–0.67 of incumbent
on three primary targets. No 60 s noise floor is assumed.

Six recycling findings were driver-adjudicated; amendment
`legacy20261008-closing-amend1` preserves exact additional telemetry findings
without changing original coverage/PAR-2. Missing telemetry remains incomplete.
Native Acacia includes parsing; comparator clocks exclude preconversion and retain
Strict-semantics limitations. TACAS23 audit rescoring and killed-worker caveat remain;
its apparent solves are an upper bound. Exact report: `refactor-closeout20261017`.

| Cap and provenance | Grouped candidate REAL / UNREAL / total | Incumbent total | Candidate / incumbent PAR-2 (s) | Candidate / incumbent resource limits | ltlsynt 2.16 total | TACAS23 total |
|---|---:|---:|---:|---:|---:|---:|
| 17 s, fresh measured | 559 / 648 / **1,207** | 1,205 | 11,624.2 / 11,730.2 | 1 / 1 | 1,263 | 812 |
| 60 s, validated derived | 570 / 674 / **1,244** | 1,240 | 35,538.3 / 36,073.7 | 1 / 2 | 1,286 | 854 |

## 2026-10-10 — guarded eventual/universal translation diagnostics

The frontend census found 40/1,524 inputs prospectively exceeding the installed
64-set width in at least one shipping translation frame (2.62%). It solves nothing
and does not establish executed-route eligibility. Isolated reruns correct shared
Spot/BDD-state effects; `census20261013-isolated` preserves originals and corrections.
Guarded simplification gains are in the issue-sweep row below. Broader rewriting
was rejected by `postmem20261011-p1`. Exact narrative: `refactor-closeout20261017`.

## Post-campaign issue sweep (2026-10-10/11)

Measured issue sweep, `legacy20261010-issue-sweep`. These original rows retain
scope, negative outcomes and admission dispositions; detailed reports/reviews are
in that campaign and `refactor-closeout20261017`. No new run occurred here.

| Issue | Measured outcome and disposition |
|---|---|
| [#200](https://github.com/gaperez64/acacia-bonsai/issues/200#issuecomment-6101183224) | MONA streaming preserved all 41 solves on the 80-input 60 s screen, but the median peak ratio was 0.998 and one peak increase reproduced. The dense action table remained the binding limit. Closed with a negative result; streaming was not landed. |
| [#201 / PR #240](https://github.com/gaperez64/acacia-bonsai/pull/240) | Guarded Spot simplification added three verified UNREAL answers on the 40 eligible inputs: 21 → 24 solved at 17 s and 24 → 27 at 60 s, with no losses. PAR-2 fell 684.8 → 582.9 s and 2,018.9 → 1,660.4 s respectively; G1 passed. Merged. The robot-grid memory failure remained. |
| [#202](https://github.com/gaperez64/acacia-bonsai/issues/202#issuecomment-6103867430) | Replay retained five current contention losses, two near the cap. Controlled attribution tied the three large slowdowns to shared memory-hierarchy interference. One/two heavy-stage slots, three repetitions each, recovered none: all 18 runs timed out at 60 s. Closed with attribution and a negative scheduler result; no scheduling change was admitted. |
| [#205](https://github.com/gaperez64/acacia-bonsai/issues/205#issuecomment-6103727482) | The 1,524-input census found 45 equivariant attempts and 24 representative entries; nine remained unsolved in the 17 s shipping race. The optimistic direct phase-saving ceiling was 2.638 s per input, 5.274 s summed over those nine. This bounds direct decoder/filter work, not end-to-end race savings or possible coverage. Closed with census evidence; E-B was not implemented or admitted. |
| [#208 / PR #241](https://github.com/gaperez64/acacia-bonsai/pull/241) | Compact payloads preserved 41/41 solves on the 80-input 60 s memory/control screen. The largest robot grid changed from an 8 GiB MEMOUT to a timeout at about 395 MiB. On 100 random inputs at 17 s, solves rose 72 → 73 and PAR-2 fell 994.3 → 970.6 s; the gain reproduced 3/3 with no losses. G1 passed; merged. |
| [#209](https://github.com/gaperez64/acacia-bonsai/issues/209#issuecomment-6106940978) | The 64-input standalone 60 s screen solved the same 29 inputs with U off, revised U on, and master U on. PAR-2 was 4,272.9 / 4,298.1 / 4,322.7 s respectively. U became cheaper but added no unique solve and retained overhead. Closed with a negative result; U remains outside the shipping default. |
| [#212](https://github.com/gaperez64/acacia-bonsai/issues/212#issuecomment-6104280960) | All three native/converted pairs matched in all three 60 s repetitions. Collector now solves natively in 8.14–8.24 s; Alarm solves on both routes in 17–20 s; robot grid reaches the memory limit on both. Closed as the reported coverage discrepancy no longer reproduced, without claiming equal route speed. |
| [#213](https://github.com/gaperez64/acacia-bonsai/issues/213#issuecomment-6103867590) | Structural OxiDD cache sizing improved the isolated native arm but had no useful shipping-race effect: 60 easy inputs × three repetitions had a paired median −1.5 ms and unchanged median peak memory. The 100-input 17 s screen remained 72 → 72 solved, PAR-2 996.6 → 1,000.2 s, within noise. Closed with a negative admission result; the cache change was not landed. |

## 2026-10-13 — residual construction and simplifier closeout (#250/#247)

The completed 53-input 60 s panel gave LAR/ACD 53 decisions each, PS/DS/SD
30/13/9, both shortcuts disabled. Matched primary-worker BA/generic/parity
translations complete 23/45/25. Generic gains 29 BA timeouts and loses seven BA
completions; shared-HOA ACD completes 45/45 versus LAR 44/45, with mixed costs.
These are research constructions, not Acacia solves or production admission.
Neither BA simplifier rescues a 60 s translation; all 22 paired completions keep
identical sizes and each variant loses one completion. Evidence:
`residual-construction20261013`, `spot-implication20261013`,
`postmem20261011-p2`, `postmem20261012-closing`, `binary-PMC4-b3953b43`.
The archive excludes six regenerable HOA/stdout files over 50 MiB and one pycache;
`EXCLUSIONS.tsv` retains sizes, digests and reasons. Decisive conversion repetitions
remain a gap. Separate trust screens: `obligation-trust20261013`,
`binary-PMC5-0b8b9be1`; freezing that candidate alone did not admit it.
Exact stage tables and final exclusions: `refactor-closeout20261017`.

## 2026-10-15 — deterministic-Buchi solver choice for the parity arm

On the 36-input 60 s comparison Spot decides 36/36 (PAR-2 43–44 s),
K-bounded antichains 33/36 (427 s), strpm-simd 35/36 (149 s), symbolic GR(1)
34/36 (265 s), with zero conflicts. Archive `otd-parity-engines20261015`;
route and trust contract: [parity arm](coverage-first/parity-arm.md).

## 2026-10-13 — memory/search attribution (#251)

Two evidence-directed 30-job panels at 60 s, 8 GiB/no swap each returned seven
verdicts (two REAL, five UNREAL) and 23 TIMEOUTs, no MEMOUTs. Timeout obstructions
were eight search, one construction and 14 mixed. Mixed identifies competing
workers in different stages, not equal costs or a critical path. Frozen PMC4 and
supplemental PMO251 are distinct treatments; no obligation arm ran on this panel.
Dense and sparse query clocks have different origins; incomplete stage growth and
missing memory brackets stay unknown. This selection estimates neither full-corpus
coverage nor an observer-only speedup. Archive `memory-search20261013`; exact
per-job tables and limitations: `refactor-closeout20261017`.

## 2026-10-17 — documentation and archival closeout

No performance result is added. `refactor-closeout20261017` preserves exact retiring
narratives, the old RESULTS/decision tables, and all 173 PR 3 deleted source files.
Publication/index registration remains with the driver; original campaigns retain
their status, including partial `gr1-par2-20260923`. The old Markdown containers
are replaced by living [runtime](DATA-STRUCTURES.md), [record](legacy-phase-records.md)
and [decision](decisions.md) references. Existing measured rows remain unchanged.

## Earlier representation and normalization studies

The exact controlled-vector study retained in `refactor-closeout20261017` found
no reproducible catastrophic bare-vector regression: twins performed the same
solver work and dominant kernel, and the memory-limit follow-up had identical
outcomes and 8 GiB peaks. No separate always-512-bit experiment was run.

The historical TLSF-normalization screen retained the same 2,621 states and
57,080 edges over three targets under every schedule. At 5 s all schedules solved
one of nine workers; 20 s REAL confirmations added no alternate-only win.
This rejected normalization as a coverage mechanism. Original study text and
exact rows are in `refactor-closeout20261017`; earlier root reports are in
`benchmarking-root-legacy`. These are selected historical studies, not new runs.

## 2026-11-05: Lean DPA closeout; tlsf-tools #105 retained

Archive [`lean-20261104`](https://github.com/gaperez64/acacia-bonsai/releases/download/evidence-2026-11-05-lean/lean-20261104.tar.gz).
P0 profile: `dpa_poll` 52–58% / `successors` 26–28% self time;
the cause was a quadratic guard scan per row.

Kept tlsf-tools #105: OxiDD arm alone, 106 inputs, 17 s, **24 → 37 → 38 solved**, zero losses;
geomean time **0.41× then 0.90×**, peak memory **1.00× then 0.87×**.

SYNTCOMP26 portfolio: **1365/1385/1390** solved at **17/60/120 s**, PAR-2 **6025.2/17975.6/33891.3 s**;
master `fb4928a5`: **1362/1385/1390**, PAR-2 **6199.3/18148.2/34068.5 s**.
At 17 s: +3 solves, no loss; at 120 s: one swap (`finding_nemo_pb_6` was 119.2 s against the cap).

Rejected: removing the 100k state cap (+1/69; 24 quick declines became 17 s runs at 3–4 GiB);
vectorizable tuple hash + DAG SoA (0.974×, below the 5% bar);
sharingtrie #367 (equal coverage, PAR-2 within noise; peak memory +1–3%, +9% on >500 MB SYNTCOMP25 cases).

Strict-auditor unknowns: 268, traced to recording-pipe overflow on sub-second inputs; parent-confirmed unknowns: 44.
G3/G4/G5 pass.

## 2026-11-06: Generated-input property tests (#369); one wrong verdict traced to Spot

Archive [`fuzz-20261105`](https://github.com/gaperez64/acacia-bonsai/releases/download/evidence-2026-11-06-fuzz/fuzz-20261105.tar.gz).
tlsf-tools gaperez64/tlsf-tools#109 adds a total bytes→TLSF generator and three libFuzzer targets (round trip, refused allocations, DPA contract), a coverage witness and 30 recorded library mutants (all caught); this repository adds a GCC replayer for SyFCo agreement, DPA≡Spot and portfolio agreement.

Found and fixed in tlsf-tools: big operators printed without required parentheses (meaning-changing `tlsf2tlsf`/`tlsfnorm` output); parser, expansion and target-adaptation crashes or partial state on refused allocations; set deduplication dropping strong next under finite semantics (#108 tracks the proof). Successful conversions unchanged: 1,826 frozen records, 4,202 translation receipts, 67,971 old/new comparisons.

Local campaigns on the final code: round trip 264k runs, allocation 272k, DPA 64k, no findings. Replayer over the grown corpora (947 round-trip and 2,443 DPA inputs with finite companions): SyFCo agreement 1,884 checked, 0 failed after passing `-os` for Strict sources (all 16 first-run failures were Strict); DPA≡Spot 2,138 Büchi checks plus parity, 0 failed; portfolio agreement 3,390 inputs × 8 arms, **1 conflict**.

The conflict is a Spot bug, open in #375 (upstream [spot#666](https://gitlab.lre.epita.fr/spot/spot/-/issues/666)): `spot::obligation_synthesis()` answers REALIZABLE for `G i -> G(i <-> XXX false)` (equivalent to `F !i`) in Spot 2.16.0 and 2.16.2, so `both:obligation:spot` can win the race with a wrong REALIZABLE. Owner decision: keep the arm until Spot fixes it. The ltlsynt and G5 SyFCo references read Strict specs non-strictly (#373).

## 2026-11-07: Seven default arms; real:small:forward leaves the shipped portfolio (#359)

Archive [`arms-20261107`](https://github.com/gaperez64/acacia-bonsai/releases/download/evidence-2026-11-07-arms/arms-20261107.tar.gz). Same frozen v2.8.0 binary (`4b4c9da8`) throughout, arms chosen with `--arms`, each call in its own 8 GiB scope.

Screen at 17 s, each seven-arm candidate on the inputs its dropped arm won in `lean-20261104` (eight arms on the 520-input union): dropping `both:gr1-real-lift:oxidd` loses 16/35, `both:parity:oxidd` 23/55, `both:parity:spot` 27/378, `both:obligation:spot` 4/24 (three `chomp` inputs still unsolved at 60 s), `real:small:forward` 1/28.

SYNTCOMP26, all 1,524 inputs, paired; 60 s reruns each portfolio's own 17 s timeouts:

| Portfolio | 17 s solved | 17 s PAR-2 (s) | 60 s solved | 60 s PAR-2 (s) |
|---|---:|---:|---:|---:|
| Seven arms, without `real:small:forward` | **1367** | **5978.5** | **1386** | **17841.7** |
| Eight arms (v2.8.0) | 1365 | 6023.9 | 1384 | 18055.2 |
| Seven arms, without `both:obligation:spot` | 1360 | 6177.7 | — | — |

No verdict conflicts. Without `real:small:forward` the only loss is `generalized_buffer_pb_5` (1.3 s with eight arms, unsolved at 60 s); the freed worker gains `chomp_pb_4_4` and two round-robin arbiters at 17 s, and `load_balancer_unreal1_pb_4_8` and `sort53` at 60 s. Dropping `both:obligation:spot` (which would remove the #375 exposure) loses `08` and four `chomp` inputs with no gain, so it stays. The seven-arm default build (`a16046d7`) passes G3 (SYNTCOMP25 165/180 and SYNTCOMP26 168/180 on both binaries, no verdict change or coverage loss), G4 and G5.

## 2026-11-10: Native-arm census; the formula unrealizability arm translates at low effort (#378, #370)

Archive [`native-20261107`](https://github.com/gaperez64/acacia-bonsai/releases/download/evidence-2026-11-10-native/native-20261107.tar.gz).

Census: the four native arms alone (17 s, phase records) on the 677 inputs won by non-native arms in `lean-20261104` or undecided decide 346, including 258 of the 366 `both:parity:spot` wins. Of 194 inputs the portfolio decides and they miss, the right-polarity arms are still translating at the cap on 112, searching on 66, in I/O preparation on 11.

Symmetry census (tlsf-tools `tlsfsym`, exact check on the canonicalized objective, a lower bound): 154 of 1,524 inputs have a verified group (143 symmetric ranges, 11 cyclic), against 45 for the automaton detector (#205); 3 of the 194 native misses and 23 of the 138 inputs undecided by every portfolio are symmetric.

Spot translation effort (new `--translation-effort`, same frozen binary, all 1,524 TLSF inputs, 17 s):

| Run | Decided | PAR-2 (s) |
|---|---:|---:|
| Native arms, all high | 1190 | 12182.2 |
| Native arms, all low | 1190 (+21/−21) | 12067.1 |
| Native arms, all medium | 1190 (+22/−22) | 12064.8 |
| Native arms, forward and formula arms low | **1206** (+23/−7) | **11541.0** |
| Native arms, forward and automaton arms low | 1188 (+10/−12) | 12235.4 |
| Shipped portfolio, all high | 1367 | 5981.2 |
| Shipped portfolio, all low | 1363 (+3/−7) | 6090.1 |
| Shipped portfolio, automaton arm low | 1367 | 5991.1 |
| **Shipped portfolio, formula arm low** | **1368** (+1/−0) | **5948.4** |

At 60 s (each configuration rerunning its own 17 s timeouts) the shipped portfolio with the formula arm at low decides 1387 against 1385, PAR-2 17776.7 against 17935.6 s, no loss. No run had a verdict conflict. No level dominates per input: phase records on the 42 inputs where high and low disagree show each level stalling where the other finishes, so no translation time budget can choose. The shipped preset now sets `unreal:formula:spot-guarded-sparse` to low; the build passes G3 (SYNTCOMP25 165/180, SYNTCOMP26 169/180 against 168, no verdict change or coverage loss) , G4 and G5 (0 opposite verdicts, 0 frontend errors). Two thirds of the translation-stuck inputs still stall at low. The first translation probe (`c2-translation`) used SyFCo formulas, not the native frontend's, and is kept only as disclosed; `c3-native-formula` repeats it on the formulas Acacia translates.

## 2026-11-14: Split sparse Oracle payloads (C01); retain the storage saving

Solver code changed in `src/solver/spot_lazy_game.hh`. The Oracle selects a scan
or grouped table once; prepared scan entries own their three maps, and grouped
entries own only their grouped payload, including the missing-destination
threshold memo. The existing inline scan records and one allocation per grouped
record are retained. The shared checked boundary in `spot_letter_oracle.hh` is
unchanged. Memo charges, budgets, eviction checkpoints and failure cleanup retain
their previous behavior. The 27 Oracle telemetry keys are unchanged; the ten
observed worker records have identical sets of 139 keys and match all 70 selected
work fields, as well as formulas, interfaces and worker construction boundaries.

Measured on this host ABI: **96 bytes less per grouped prepared rank**, eight
bytes less per scan record, and eight bytes more fixed Oracle storage. The C++
allocation-refusal probe matches baseline counts: scan/grouped **21/25** for
present destinations and **24/27** with a missing destination. Every one of the
97 refused allocations per binary remains resource-limited, clears all predicate
caches and identities, and restarts with the exact predicate.

The first `docker_default` preset resolved to `otf_sparse_formula_parity`.
Baseline and candidate use the normal release profile (`-O3`, native `-Ofast`,
LTO), built with two jobs and `CARGO_BUILD_JOBS=2`. Ten frozen TLSF inputs come
from `prof/solved-2to17.tsv` of campaign kd20261111 in archive `sprint-20261111`; each is decided by
`--arms unreal:formula:spot-guarded-sparse:frozen-graph`. Work is fixed at K=2
for the two AllLights inputs and K=32 for the other eight, with weakening,
equivariance and Spot fast paths off. Three serial alternating pairs per input
run on CPU 15, each in its own `MemoryMax=8G`, `MemorySwapMax=0` systemd scope.
`perf stat` measures user-mode instructions and cycles; GNU time records maximum
process RSS, including waited-for children. No build or other test ran during
the final timing screen, and no run hit the external 60-second guard.

All **60/60 verdicts are UNREALIZABLE**. Candidate/base geometric means of the
per-input medians are **1.001252 instructions, 0.993903 cycles, 0.997165 peak
RSS**. Median RSS is lower on all ten inputs; `g-unreal-112` falls from 169,108
to 167,828 KiB (1.25 MiB). Keep the measured storage saving; the small cycle
change is not a speedup claim or an admission result. The capsule contains the
full per-input table and raw counters, including the earlier candidate before
the final teardown-order correction and the incomplete first RSS parsing attempt.

Verification: the exact Oracle test checks 184,647 predicates, 1,122 fixed-K
matches, 960 corrupted certificates, 2,384 inconclusive stops and 3,146 forced
evictions. The complete checked unit suite passes **100/100**. Full pytest with
the rebuilt Acacia and matching Spot 2.16 bindings passes **2,268 tests and 58
subtests**, with five optional tests skipped. Ruff passes. The additional release
unit run is **93/94**: tlsf-tools' allocation-witness test does not reach A3, A4,
C7 and C8; that target does not include the changed solver header and passes in
the checked profile. No subproject fix is included.

Evidence: local capsule `acacia-c01-20261114`, archived with this sprint's
evidence. It holds the frozen binaries (baseline `e126c9ed5452`, candidate
`270bb8d6c5ea` on `ac90948faf69`), input hashes, build identities, raw counters
and test logs. G3, G4 and G5 results are recorded in the pull request.

## 2026-11-15: Index sparse search ranks through node IDs (#397)

Remove only the sparse search interner's rank copy. During search, normalized
node ranks are immutable, node IDs are append-only offsets, and node moves
cannot throw. The index stores cached hash/ID pairs and compares complete
entries for every matching hash. Failed node growth rolls back exactly its
index iterator before propagating the failure; enqueue order and
resource/cancellation outcomes remain unchanged. Proof, losing-antichain,
Oracle and independent verifier ranks keep their owners. `rank_interner_bytes`
accounts for index payload without reading moved-out nodes; its exclusion of
hash-node overhead and buckets is unchanged.

Matched normal release builds use the first `docker_default` preset,
`otf_sparse_formula_parity`, from `origin/master` at `02d40827` and the candidate,
with GCC 15.3.1, private Spot 2.16 (64 acceptance sets), one compile job and one
LTO thread. The earlier 32-set setup failed the same G4 row on both binaries;
final matched runs use the repository's 64-set setting and retain those earlier
observations separately. The same ten fixed-K formula-arm inputs as C01 run in
three serial alternating pairs on CPU 2, each in a 5 GiB/no-swap scope. All
**60/60 verdicts are UNREALIZABLE**, with valid process RSS on all 60 runs and
no multiplexed hardware counters. Candidate/base geometric means of per-input
medians are **0.998887 instructions, 0.999805 cycles and 0.993931 process RSS**.
Median RSS falls on all ten inputs; `heim-double-x12` falls from 197,536 to
195,648 KiB. These counters support the storage saving; they establish no
wall-time speedup or G3 admission. RSS is the `wait4` maximum for the reaped
process tree, not a concurrent cgroup peak; cgroup peaks are not collected by
this probe and carry explicit missing reasons.

For 4,096 ranks at supports 1/8/64, matched release probes remove **4,096
allocations** and **192/416/2,208 KiB** of requested allocation bytes. The storage
check requires those allocation/byte savings and a paired median RSS geometric
mean below baseline; it rejects a derived control using the measured baseline
allocation rows as the candidate. Ten further paired worker records preserve all **139
telemetry names**; all **128 fields** outside timings, worker PID and the two
intended byte changes agree. Each retained-payload estimate falls by exactly its
interner-payload saving. Actual and forced collisions, copied/moved ranks,
growth/rehash, allocation refusal, limits, cancellation and moved-result
destruction pass the focused unit gate; hash-only, missing-rollback and whole
hash-group rollback mutants are rejected at the intended checks. The
lifetime/growth Memcheck probe reports zero errors. G0 passes **95/95 Acacia**
and **100/100 Posets** tests. G1 passes **40/40** with no verdict change or
coverage loss. G4 passes with **592 OK and 33 permitted timeouts**; all 625
expected invocations are present, with no false-verdict marker. The Oracle
gate still rejects 960 corrupted certificates, and sparse normalization and
mass/equality checks pass.

G5 passes all **1,524 instances**, with zero missing conversions, opposite
verdicts or frontend errors. Preparation converts all 1,586 source inputs with
zero skips; all ten spot-checks pass, with no tlsf-tools fallback pairs. Native
solves 1,356 instances and times out on 168; the converted route solves 1,317
and times out on 207. There are 41 native-only and two converted-only answers.
These are coverage results, not timing comparisons.

Review follow-up repeats all six rank/interner/Oracle/provider tests, all three
mutants, the allocation probes, the storage gate and its no-saving control.
They pass, including rejection of all 960 corrupt certificates; the separate
Memcheck lifetime probe again reports zero errors. Reclassifying the retained
G4 records still reports `GATE PASS`; the full corpus campaigns were not rerun.

Evidence: archive `acacia-397-20261116` (release `evidence-2026-11-16-acacia-397`,
`partial`: everything except G3), with session environments removed from Meson logs
and a redaction ledger of the changed paths. Round-2 review found no new
implementation defects and repeated the focused tests, mutants, allocation probes,
storage controls and Memcheck (zero errors) without rerunning full campaigns.

G3 ran on the benchmarking machine (archive `g3-20261116`): a release build of this
change merged onto master c9d40890 against the 2.9.0 gate binary passes both panels,
SYNTCOMP25 165/180 and SYNTCOMP26 169/180, with no verdict change or coverage loss and
PAR-2 within 0.9 s.
