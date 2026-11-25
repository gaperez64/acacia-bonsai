# Results

[Measurement protocol](README.md) and [evidence index](evidence-index.tsv).

## 2026-11-25: Component output ownership restores AIGER +dc (#466)

Archive `dc466-20261125`.

The decomposed deterministic Spot fast path attached the global output cube to
every component. Spot's `+dc` encoder consequently granted don't-cares for other
components' outputs. Projecting the cube onto each machine's APs repairs that
ownership error; the original BDD dictionary can be retained. The fresh-dictionary
replay reported under #280 also repaired the output properties, so it did not
isolate dictionary layout as the cause.

All encoding boundaries now check local output declarations, disjoint component
output supports, and agreement with explicit output groups restricted to each
machine's APs. These checks throw an internal error in every build before encoding.
Unused interface outputs remain in the explicit groups and receive encoder defaults.
The CLI accepts `+dc` again; its default remains `isop`.

**Decision-path impact: yes; local G3 passed (below).** Spot 2.16.3 reads
`synthesis-outputs` in `split_2step(aut, synthesis_info&)`. Its AUTO dispatcher
computes unsigned `ap().size() - bdd_nodecount(output_bdd)` to choose EXPL or
SEMISYM. The global cube can underflow that count. Projection preserves ownership
of APs actually present, but can change game construction, solve timing, strategy
choices and the selected portfolio arm. Formula realizability simplification runs
earlier and does not read this property. The game solver consumes the resulting
arena; extraction and Mealy simplification also read the property after the decision.
The projection can affect decision performance.

**Local G3: GATE PASS.** Candidate `5e98f175` against master `7343ca0c` ran on
this machine under the approved local regime: `--scope-mode instance`, the local
6G cap patch, no swap, 17 s, and both 180-input panels. Candidate and
baseline-vs-baseline control shared one master baseline binary.

| Campaign | SYNTCOMP25 solved / PAR-2 | SYNTCOMP26 solved / PAR-2 | Gate |
|---|---|---|---|
| candidate vs master | 165 → 165 / 732.136 → 730.264 s | 167 → 167 / 572.893 → 572.689 s | PASS |
| control (master vs master) | 165 → 165 / 729.362 → 731.309 s | 167 → 167 / 573.829 → 568.088 s | PASS |

Both campaigns had **0 verdict changes and 0 coverage losses**. The candidate's
PAR-2 changes fall within the control variation; no speedup claim is made.

**Repeated #280 smoke panel.** The original 33 inputs (panel list included in
`dc466-20261125`) were rerun at 17 s using
`benchmarking/synthesis-size.py`, the original TLSF map/corpus, fresh
`tests/controller-check` invocations with 60 s / 200,000-state check budgets,
and the original 15 modes plus `optim`. All 528 mode controllers verified.
Two further members (omitted mode on candidate and master) also verified all 33,
for **594/594 verified**, with no synthesis/verification failures, timeouts,
resource limits or unsupported checks. Totals below use all the same 33 inputs.

| Mode | Verified / 33 | Total ANDs+latches | vs isop |
|---|---:|---:|---:|
| `isop` | 33 | 24,390 | +0.00% |
| `isop+dc` | 33 | 24,466 | +0.31% |
| `isop+ud` | 33 | 33,908 | +39.02% |
| `isop+dc+ud` | 33 | 33,888 | +38.94% |
| `ite` | 33 | 86,860 | +256.13% |
| `ite+dc` | 33 | 86,801 | +255.89% |
| `ite+ud` | 33 | 86,879 | +256.21% |
| `ite+dc+ud` | 33 | 86,664 | +255.33% |
| `both` | 33 | 24,099 | -1.19% |
| `both+dc` | 33 | 24,156 | -0.96% |
| `both+ud` | 33 | 29,974 | +22.89% |
| `both+dc+ud` | 33 | 32,477 | +33.16% |
| `both+ud+sub0` | 33 | 30,013 | +23.05% |
| `both+ud+sub1` | 33 | 28,282 | +15.96% |
| `both+ud+sub2` | 33 | 30,015 | +23.06% |
| `optim` | 33 | 4,485 | -81.61% |

These are portfolio observations, with the current default bounded parity
reduction; arm selection and reduction outcomes vary between runs. They are not
same-strategy encoder comparisons or a performance gate.

With omitted mode (default `isop`), **30/33 controllers are byte-identical to
master**. The other three selected different arms:

| Input | Master arm, A+L | Candidate arm, A+L |
|---|---|---|
| Shades2_235467da | backward, 2 | parity, 60 |
| Cockpitboard_efe2feaf | parity Buchi fast path, 11 | decomposed fast path, 11 |
| ltl2dba26 | backward, 26 | parity, 19 |

For each of these inputs, forcing the legacy and parity arms on both versions
produced byte-identical master/candidate controllers in all six pairs; all twelve
controllers verified. The recorded default differences are therefore explained by
portfolio selection. Omitted mode and explicit `isop` also differed on three
inputs (Shades2, ltl2dba26 and ltl2dba05), again with different selected arms.
The omitted-mode totals were 24,522 candidate and 24,471 master; the table's
explicit-isop baseline was 24,390.

**Full synthesis sweep.** All 433 REALIZABLE SYNTCOMP26 inputs were attempted
on master `7343ca0c` and candidate `5e98f175` at 17 s with default `isop`.
Every produced controller was checked by `controller-check` with 60 s /
200,000-state budgets. Both revisions produced 403 controllers and verified
**363**, with **363 common and 0 failed verifications**. Each had 30 synthesis
timeouts, 38 verification timeouts, one verification resource limit and one
verification error. On the common verified set, total ANDs+latches changed from
**492,459 → 493,363 (+0.18%)**.

Six common inputs changed size:

| Input | Master A+L | Candidate A+L |
|---|---:|---:|
| EscalatorCountingInit | 26 | 24 |
| KitchenTimerV3 | 134 | 908 |
| TwoCounters4 | 59 | 88 |
| amba_decomposed_encode_pb_14_pe_ | 1,277 | 1,276 |
| ltl2dba_C2_pb_6_pe_ | 39 | 84 |
| ltl2dba_beta_pb_2_pe_ | 112 | 171 |

EscalatorCounting and narylatch_pb_8_pe_ changed bytes at unchanged sizes;
the other 355 common controllers were byte-identical. The sweep did not record
selected-arm telemetry, so its route changes cannot be established. These size
observations do not establish a fixed-strategy encoding effect. The archive
includes the comparison for every input and the per-run controllers/checks.

Validation used master `7343ca0c` and an isolated copy of the working fix with the
same recorded submodules (`tlsf-tools a715666d`, Posets `a4387cb7`, OxiDD
`288a6efc`), GCC 15.3.1 and Spot 2.16.3. The validation workspace's existing
`tlsf-tools 92467127` checkout was preserved; an initial run exposed its mismatch
with master's native API/CLI tests, and both tests passed with the recorded pin.
Smoke, unit validation and the full synthesis sweep used
`systemd-run --user --scope -p MemoryMax=5G -p MemorySwapMax=0`, one compile/test
job and one heavy invocation at a time, the local Spot environment and a scratch
temporary directory. G3 used the approved 6G regime described above.

Whole Meson unit suites passed: release **120 OK / 6 expected failures / 0 failures**
(126 tests), diagnostic **126 OK / 6 expected failures / 0 failures** (132 tests).
Both include fifteen new independently checked controllers: the nonliteral
`x = a & b, y = true` example (with an unused output), Cockpitboard and Gamelogic
under `isop+dc`, `ite+dc`, `ite+dc+ud`, `both+dc` and `both+dc+ud`.
The new tests took 0.13-0.31 s each in release and 0.13-0.18 s in diagnostics.
The helper test also covers immediate/deferred extraction and malformed output
partitions. All 874 telemetry-inventory checks, Ruff and changed-line clang-format
checks passed, as did `git diff --check`.

Archive `dc466-20261125` contains the smoke rows, summaries and panel list,
per-run circuits/checks/phases, fixed-arm comparisons, build/unit logs, exact
arguments, source snapshot inventory and binary hashes. It also contains the
diagnosis with its measurement tables, replay matrix and standalone Spot
demonstration, plus the local G3 regime patch, candidate/control landing files
and full synthesis sweep. Inherited process environments were removed from the
archived unit-test JSONL records before the private-data check.

## 2026-11-25: Decoded client values make the encoded arbiters' count games tiny (#471, stage 1)

Archive `enc471-20261125`. This is a scratch prototype; no solver code changed. simple_arbiter_enc
and prioritized_arbiter_enc carry a client number on a binary bus, so the count arm (#458)
declines them. Read per client value instead, with at most one requesting and one granted value
per step, the unused codes kept distinct and client 0 fixed in the prioritized variant, their
count-quotient games stay small:

| Input | n | States | Edges | Build + solve + verify | ltlsynt / master |
|---|---:|---:|---:|---:|---|
| simple_arbiter_enc | 16 | 785 | 20,361 | 0.005 s | timeout 300 s / agree |
| simple_arbiter_enc | 32 | 3,105 | 85,953 | 0.032 s | timeout 300 s / timeout 60 s |
| prioritized_arbiter_enc | 12 | 1,182 | 31,999 | 0.009 s | agree / agree |
| prioritized_arbiter_enc | 15 | 1,878 | 51,057 | 0.015 s | killed / timeout 60 s |

All verdicts are REALIZABLE, and every decided cross-check agrees, including n=8 for both
families. The stability guarantee compares only the low `nbits(n)` grant bits, so "decoded grant
unchanged" is not exact at powers of two; the decoding must model such raw-bit constraints. The
five inputs master leaves unsolved at 17 s (prioritized_arbiter_enc n=12–15, simple_arbiter_enc
n=32) are in reach. A production version needs certified value predicates and widths from the
frontend (#472's shared families), one-value-per-bus letter orbits in the count engine, lifting
back to encoded outputs with a controller check, and a Lean lemma for the decoding.

## 2026-11-25: The GR(1) lift's axis order no longer depends on parameter names (tlsf-tools #146)

Archive `lift146-20261125`. tlsf-tools #146 orders the legacy seed discovery and U window by
the folds' structural key, keeps axes whose width provenance is incomplete, tries tied axes
within the per-run probe cap and stops at the first target-verified proof. Candidate 72bb0b5f
(tlsf-tools aba5ad84, binary `991de4be…`) against master 614eedb9 (`fbac6b52…`): G1, G3, G4 and
G5 pass. The full SYNTCOMP26 screen at 17 s and 6G solves 1,395 against 1,396 with 0 verdict
conflicts and PAR-2 5,028.8 → 5,041.0 s. The only loss, 08.ltl, and G3's one flip,
load_balancer_unreal1 6_4, sit at the cap: five alternating runs of each binary put 08.ltl at
15.9 vs 16.4 s and 6_4 at 16.80 vs 16.94 s, with overlapping ranges, and the candidate solves
08.ltl on the renamed corpus. The tlsfobf-renamed screen has 0 conflicts and no plain-only
solves.

## 2026-11-24: Bounded parity-route controller reduction cuts controller size by 3.8% (#467)

Archive `strat467-20261124` (release `evidence-2026-11-24-strat467`). Under `-s`, the
parity route used to encode the strategy it extracted, unreduced. The strategy was
positional and reachable, but it kept the expanded parity arena's memory, and no
Mealy reduction ran before AIGER encoding. `--parity-mealy-reduction
none|bisim|bisim-output|reselect` (default `reselect`, `-s` only) adds:

- **Bisimulation** over input/output-labelled edges.
- **Output-assignment reduction** (`bisim-output`).
- **Reselection** (`reselect`), which also re-solves small arenas to pick a
  strategy that reduces better.

All optional work runs in a supervised subprocess. Its budget is 200 ms or 5% of
the remaining time, whichever is smaller. Its memory cap is sized from the
scope's headroom under a finite limit, and from the worker's RSS share otherwise.
The unreduced controller is kept and is used whenever the child times out, fails,
exceeds a limit or does not produce something strictly smaller. Reduction needs
RSS sampling, which is Linux-only, so elsewhere it is skipped. Decision runs are
unchanged.

**Panel** (43 inputs chosen to include the #280 outliers, 17 s, 60 s
verification): both options verify all 43. Total ands+latches fall from 36,552
to 26,104, with no memory skips. Lights_2c5b09da drops from 10,765 to 1,475.

**Full sweep** (all 433 REALIZABLE SYNTCOMP26 inputs, same binary):

| Option | Controllers | Verified | Ands+latches on 362 common | Median |
|---|---:|---:|---:|---:|
| `none` | 402 | 363 | 589,722 | 35 |
| `reselect` | 401 | 362 | 567,456 | 35 |

`reselect` shrinks 47 controllers and grows 3. The largest cuts are Lights
(−9,290), load_balancer n=6 (−3,738), load_balancer n=5 (−1,114) and robot_grid
n=6/1 (−999); the largest increase is KitchenTimerV4 (+863). Corpus-wide the cut
is 3.8%, against 28.6% on the panel, because the panel was weighted toward the
outliers. The two prioritized_arbiter outliers stay large.

**The one lost controller is a pre-existing race (#469).** SPIReadManag verified
with `none` and timed out with `reselect`. Under `-s`, the portfolio commits to
the first published proof and suspends the others. When that is the legacy route,
its controller extraction can stall with no fallback. Over 40 shuffled 10 s runs
each, timeouts were: master 10, `none` 4, `reselect` 8. Fisher's exact test gives
p = 0.35 for `reselect` against `none` and 0.79 against master. Every timeout
selected the legacy route and attempted no reduction, so the reduction does not
cause it.

**Local G3** (instance scope, 6G cap, 17 s; base #468 head `720e1241`): PASS on
both panels with no verdict changes. SYNTCOMP25 PAR-2 went from 759.6 to 760.7 s
and SYNTCOMP26 from 577.4 to 578.0 s, within the control's spread (755.7 → 755.4
and 578.7 → 575.1 s).

## 2026-11-23: Under parent memory supervision the count arm also solves collector_v2 n=27 (#454, stage 2)

Archive `sym454e-20261122`. The arm now sizes its game from the whole scope and joins the parity
workers under the parent's headroom check, instead of taking a fixed seven-way share. Candidate
72ae5954 (binary `a366cd45…`) against master b28b0454 (`5fd7ecc5…`, with #465 and #468); G0, G1,
G3, G4 and G5 pass. G3 is flat: 166 → 166 and 168 → 168 solved, PAR-2 633.2 → 622.2 s and
499.0 → 494.7 s, inside the control's spread (168 vs 169, 499.4 vs 482.3 s on SYNTCOMP26). The
full SYNTCOMP26 screen at 17 s and 6G solves 1,397 instead of 1,390 with no loss, no verdict
conflict and PAR-2 5,263.9 → 5,007.1 s. Six gains are the arm's: collector_v1 n=11 to 15 (0.12 to
0.15 s) and collector_v2 n=27 (2.2 s); the seventh, load_balancer_unreal1 6_4 at 16.87 s, sits at
the cap. The tlsfobf-renamed screen has 0 conflicts and no plain-only solves.

## 2026-11-22: The count-quotient arm solves six more SYNTCOMP26 inputs with no conflict (#454, stage 2)

Archive `sym454d-20261122`. `both:symcount:spot` runs as the seventh worker of
`otf_sparse_formula_parity` (6c6da84a, frozen binary `3b300d6b…`, against master 7c07d934). G0, G1,
G3, G4 and G5 pass. G3 gains one solve on each panel, with PAR-2 663.3 → 623.1 s (SYNTCOMP25) and
508.8 → 476.3 s (SYNTCOMP26); the base-vs-base control moves less than 1 s. The full SYNTCOMP26
screen at 17 s and 6G solves 1,386 instead of 1,381 with 0 verdict conflicts and PAR-2
5,613.6 → 5,380.4 s. It gains collector_v1 n=11 to 15 (0.12 to 0.15 s) and ltl2dba_theta n=25
(0.41 s). The only loss, load_balancer_unreal1 6_4, sits at the cap and flips in the control too.
The tlsfobf-renamed screen has 0 conflicts and no plain-only solves. collector_v2 n=27 still times
out: the arm's 512-byte-per-element budget declines its 1.2M edges once the 6G scope is split
seven ways, though it solves in 1.5 s and 472 MB alone. The base predates #465, whose prepass
also solves ltl2dba_theta n=25.

## 2026-11-22: acacia-symcount derives the collector quotient games from their formulas (#454, stage 2)

Archive `sym454c-20261122`. `acacia-symcount` (standalone, not yet an arm) splits a spec into
index-free conjuncts, per-index templates and symmetric aggregates, translates the template and
the residual formula with Spot, and solves the count game with Spot plus `check_strategy`.
Over all 1,586 corpus files (30 s) it decides 52 with no conflict against SYNTCOMP statuses or
`lift437b-20261121`'s screen, including collector_v1 n=11 to 15 (0.02 to 0.05 s) and collector_v2
n=27 (1.2 s), which master leaves unsolved, and collector_v1 n=27 (0.5 s). With those times as an
extra arm, that screen's PAR-2 would fall from 6,253.6 to 6,025.2 s. 900 random fragment specs
at n=2 to 4 gave 631 decided verdicts, all equal to ltlsynt's; tlsfobf-renamed inputs give
identical verdicts and game sizes. Admission (stage 4) needs the arm integration first.
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
## 2026-11-22: A count-quotiented game decides seven unsolved collector inputs in milliseconds (#454, stage 1)

Archives `sym454-20261122` (prototype and checks) and `sym454b-20261122` (sibling-repo
census, master runs). Scratch prototype; no solver code changed. collector_v1 and v2 are
played on exact deterministic monitors of their specs (TLSF v1.1 standard Mealy semantics),
quotiented by S_n; letter orbits count arrivals per cell, keeping seen and unseen apart.

| Input | master `7c07d934`, 17 s / 120 s | Quotient states / joint orbits | Solve |
|---|---|---:|---:|
| collector_v1 n=11 | TIMEOUT / 19.1 s | 24 / 728 | < 1 ms |
| collector_v1 n=12 to 15 | TIMEOUT / TIMEOUT | 26 to 32 / 910 to 1,632 | ≤ 1 ms |
| collector_v1 n=27 (selection-2025, not in the 1,524) | TIMEOUT / TIMEOUT | 56 / 8,120 | 4 ms |
| collector_v2 n=27 | TIMEOUT / TIMEOUT | 810 / 402,696 | 0.10 s |

Unquotiented, collector_v1 n=12 already needs 33.6M joint pairs and the 4G cap. The quotient
matches the concrete game transition by transition (v1 n≤10, v2 n≤6), a solver-independent
verifier accepts every strategy, and lifted controllers pass Spot against the TLSF LTL (v1
n≤6, v2 n≤5); never-fire and always-fire controls are rejected. The native-20261107 `tlsfsym`
over the 217 sibling-repo specs not among the 1,524 adds no other candidate: of its 7 full
groups with n≥3, narylatch n=8 and ltl2dba_C2_unreal n=10 are solved, the collector templates
are n=3, and collector_v3 n=27 is decided by a constant output (#451). The gain is 7 inputs
at 17 s (6 in SYNTCOMP26) and 6 at 120 s (5). PAR-2 moves more than coverage: replacing the 19
collector_v1/v2 results of `lift437b-20261121`'s 17 s screen with 0.15 s (master's start-up
floor) cuts PAR-2 by up to 224 s of 6,253.6, 21 s of it on inputs master already solves
(collector_v2 n=15 takes 16.6 s). The same bound over every symmetric input with n≥3 is 687 s
(101 inputs, 18 unsolved), the ceiling for a generic stage 2. Open: deriving the monitors from
formulas (stage 2), a new arm (#359) and the Lean proof (stage 3).

## 2026-11-22: Formula symmetry cannot drive the K-bounded reduction; one-sided restrictions can decide 11 inputs (#370, #451)

Archive `sym370-20261122` (release `evidence-2026-11-22-sym370`). This is a
read-only investigation; no solver code changed.

Census join. The exact objective check of `tlsfsym` (`native-20261107`) verifies
a group on 154 of 1,524 inputs, and the automaton detector of #205 on 45. The two
sets share one input. Of the 154, 24 are unsolved by master at 17 s
(`lift437b-20261121`) or in #407's 120 s set: collector_v1, v2 and v3 (11,
REALIZABLE), ltl2dba_theta and ltl2dba_C2_unreal (11, UNREALIZABLE), and one each
of prioritized_arbiter_enc and simple_arbiter_enc (REALIZABLE). The automaton
detector finds none of the 24. The 11 cyclic-only inputs are all `shift`, which
master already solves.

Why the current reduction cannot use them. A formula-level symmetry preserves
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

One-sided restrictions. Merging a declared input family into one input restricts
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

Local G3 ran under the regime of `g3-local-20261120`: `--scope-mode instance`,
a 6G cap, 17 s, a shared master `28b8b4b9` baseline and a base-vs-base control.

| Campaign | SYNTCOMP25 solved / PAR-2 | SYNTCOMP26 solved / PAR-2 | Gate |
|---|---|---|---|
| #449 `26fccc41` | 164 → 164 / 782.8 → 778.2 s | 167 → 167 / 591.0 → 590.7 s | PASS |
| control (base vs base) | 164 → 164 / 771.2 → 770.0 s | 167 → 167 / 588.8 → 588.8 s | PASS |

No verdict changed, and both PAR-2 differences are within the control's spread.

Full-corpus screen. All 1,524 SYNTCOMP26 inputs ran at 17 s, serially, under
the same 6G scope. #449 solves 1,374 against master's 1,371, and PAR-2 falls from
6,329.8 to 6,253.6 s. The three gains are load_balancer n=9, 10 and 11, all
REALIZABLE in 5.0 to 6.6 s, and load_balancer n=8 drops from 9.7 to 5.0 s. There
are no losses and no verdict conflicts. The fold also runs on load_balancer n=7
and prioritized_arbiter n=8, 9, 10 and 12, which both binaries solve; there it
costs 0.5 to 1.4 s each. No other input changes by more than 3 s.

Renamed corpus. `benchmarking/obfuscate-corpus.py` renamed every identifier of
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

Local G3 ran under the regime of `g3-local-20261120`: `--scope-mode instance`,
a 6G cap, 17 s, a shared master `9e379f95` baseline and a base-vs-base control.

| Campaign | SYNTCOMP25 solved / PAR-2 | SYNTCOMP26 solved / PAR-2 | Gate |
|---|---|---|---|
| #448 `c9839ef4` | 163 → 164 / 799.6 → 774.7 s | 167 → 167 / 589.3 → 592.9 s | PASS |
| control (base vs base) | 163 → 163 / 792.2 → 796.4 s | 167 → 167 / 593.8 → 588.1 s | PASS |

The one new solve is simple_arbiter_enc n=14 (REALIZABLE, 10.2 s). No verdict
changed. The SYNTCOMP26 difference is within the control's spread.

Full-corpus screen. All 1,524 SYNTCOMP26 inputs ran at 17 s, serially, under
the same 6G scope. #448 solves 1,371 against master's 1,365, and PAR-2 falls from
6,471.7 to 6,340.1 s. The six gains are simple_arbiter_enc n=11 to 16, all
REALIZABLE in 6.8 to 14.9 s. There are no losses and no verdict conflicts. Among
inputs both binaries solve, the largest slowdown is 2.1 s (prioritized_arbiter_enc
n=10, 13.3 → 15.5 s). No input that master leaves unsolved runs more than 1 s
longer.

Census rerun. The 30 inputs of `census437-20261120` were rerun with the lift arm
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
| new (v2.10.0 RC) | 1379 | 5749.9 | 1399 | 16514.7 | 1404 | 30752.6 |
| master (six arms) | 1370 | 5871.9 | 1392 | 17193.1 | 1398 | 32117.5 |
| SemML | 1274 | 11614.5 | 1333 | 27836.5 | 1352 | 47818.0 |
| ltlsynt 2.16 | 1264 | 9420.7 | 1290 | 29523.6 | n/a | n/a |
| Acacia v1 | 812 | 24778.5 | 854 | 82407.0 | n/a | n/a |

No series disagrees with another on any verdict. Against master, new solves 9, 7 and 6 more inputs
at 17, 60 and 120 s, all of them unrealizable, and PAR-2 is lower by 122, 678 and 1,365 s. New at
60 s already decides more than master at 120 s.

Race winners in new (phase records, `parent_winner`) at 17 s, out of 1,379 decided:

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

- rw_arbiter n=5 and lift_unary_enc n=6 (at 6), and lift_unary_enc n=7 (at 8):
  the bound clears, then every seed window declines on a structural shape mismatch.
- load_balancer n=10, 11 and lift n=9, 10: still decline on the large template
  at 8.

These families need templates whose predicate arity is parametric. Independent ground truth is thin:
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

## Results before 2026-11-20

Original entries are archived in docs-20261122, under
`benchmarking/RESULTS-before-2026-11-20.md`. Undated entries retain that status.

- 2026-11-18: Spot 2.16.3 fixes the obligation-synthesis bug and adds three solves (#375); `spot2163-20261118`.
- 2026-11-18: The cheaper lazy KD intersection ships; theta persistence blocks do not; `gates-cpre407-20261118`.
- 2026-11-18: A faster lazy KD intersection does not move the backward search (#407); `cpre407-20261118`.
- 2026-11-18: The formula-unreal arm builds its automaton directly (#407); `ucw407-20261118`.
- 2026-11-17: Eight-arm second Docker slot (#431); `acacia-431-20261117`.
- 2026-11-17: Local G3 admits A11 and A12 under a 6G regime (#403); `g3-local-20261117`.
- 2026-11-17: Translation and recurrence experiments for #407 decide nothing; `issue407-20261117`.
- 2026-11-17: Optimizing the forward arms would not recover coverage (#378); `fwd-20261117`.
- 2026-11-16: The shipped portfolio drops `unreal:automaton:forward` (#384); `preset384-20261115`.
- 2026-11-15: Cleanup sprint report; `cleanup-20261113`.
- 2026-11-12: Shipped lazy KD build, native arms and gates; `sprint-20261111`.
- 2026-11-12: Lazy KD with subtree and pending-block bounds becomes the shipped downset; `sprint-20261111`.
- 2026-11-12: Lazy KD performance pilot; `sprint-20261111`.
- 2026-11-12: Local solver pilot; `sprint-20261111`.
- 2026-11-04: Shared deterministic routes; rank/128 retained; `pipeline-posets-20261103`.
- 2026-11-03: Lazy parity stays opt-in; accounting fixes retain the eight-arm default; `sprint-performance-20261102`.
- 2026-11-01: The oxidd parity arm joins the default portfolio; the swap for the Spot arm loses; `otfdpa-20261101`.
- 2026-10-28: Headroom becomes the default; translation sharing closes without a build; `sharegate-20261028`.
- 2026-10-28: The GFG Büchi fast path fires on one input, so it is removed rather than proved; `gfgcensus-20261028`.
- 2026-10-27: The parity worker loses inputs to its memory share; on-the-fly parity would first need a lazy automaton; `otfpar-20261027`.
- 2026-10-26: Backward reductions add little to the forward unreal engine, even computed from the whole automaton; `fwdp-20261026`.
- 2026-10-26: Building Spot's FM automaton on demand does not help; the guarded game is the bottleneck; `otf2-20261026`.
- 2026-10-25: SemML narrows the gap to 2.7.0 at 120 s but does not close it; `c120-syntcomp26-20261024`.
- 2026-10-24: SemML decides fewer inputs than 2.7.0 at both caps; `semml-syntcomp26-20261024`.
- 2026-10-24: 2.7.0 against ltlsynt and 1.x at 60 s; `rel27-60-20261023`.
- 2026-10-23: A 4M-node BuDDy growth step solves four more inputs; `bddgrowth-20261023`.
- 2026-10-23: Cheaper MONA decoding solves five more inputs; `perf-20261022`.
- 2026-10-21: Leaner TLSF preparation and forward results, no corpus speedup; `refopt-20261021`.
- 2026-10-20: isl does not compress Acacia's downsets; `islprobe-20261020`.
- 2026-10-20: A feature-based arm selector finds no coverage on 4 cores; `costmodel-20261020`.
- 2026-10-20: Neither TLSF normalization nor a 4-core arm subset helps; `route-20261019`.
- 2026-10-19: Same-session three-way on SYNTCOMP26 (v2.6.0); `threeway-syntcomp26-20261019`.
- 2026-10-19: The antichain arms' Spot shortcut adds nothing to the default; `spotfast-ablation-20261019`.
- 2026-10-19: Gates for the strpm removal (#284); `gates-284-20261019`.
- 2026-10-18: Tier 4 needs no strpm-simd (#282); `tier4-spot282-20261018`.
- 2026-10-18: Shipping selection: keep the four members; `ship-select20261018`.
- 2026-10-18: G5 passes without a waiver (#265); `g5-tlsf-tools20261018`.
- 2026-10-17: Arms do not contend on 8 cores (#277); `armaccounting20261017`.
- 2026-10-17: Parity arm translation: no general setting wins (#261); `parity261-translation20261017`.
- 2026-10-17: No default arm can be dropped (#264); `portfolioloo20261017`.
- 2026-10-16: OTD close: ship the parity portfolio; `otd-OTF-owner20261016`.
- 2026-10-13: post-memory frontend close: admit the obligation arm (#244); `postmem20261012-closing`.
- 2026-10-11: compact action payloads (#208, #200), development measurements; `legacy20261010-issue-sweep`.
- 2026-10-08: admit the 3 s equivariance default (#224); `cov20261007-eqfull`.
- 2026-10-07: coverage-first screens: negative results (#201, #202, #204, #206, #209); `cov20261007-screens`.
- 2026-10-07: matched R × equivariance ablation (#207); `cov20261006-p1ablation`.
- before 2026-11-20 (undated): Extended UNREAL weakening experiment, not admitted; `cov20261006-p1ablation`.
- 2026-10-06: Basic UNREAL weakening instrumentation; `cov20261006-p0exit`.
- 2026-10-06: Worker and native-route attribution (#210); `cov20261006-p0exit`.
- 2026-10-06: Gate and memory-collector implementation; `cov20261006-p0exit`.
- 2026-10-04: Optimization closing campaign and shipping default; `opt20260927-p4close`.
- before 2026-11-20 (undated): v2.4.2 released three-way; `plots-spot-otf-threeway-20260909`.
- before 2026-11-20 (undated): E5 selection and per-arm legs, unreleased; `gr1-par2-20260923-perarm-m1`.
- before 2026-11-20 (undated): O5 optimized checkpoint, unreleased; `opt20260927-o5`.
- before 2026-11-20 (undated): Rejected or deferred ideas; `pipeline-posets-20261103`.
- before 2026-11-20 (undated): Legacy worker phase observation; `legacy20261008-screens`.
- 2026-10-10: grouped sparse guarded oracle admitted at campaign close (#235); `legacy20261008-closing`.
- 2026-10-10: guarded eventual/universal translation diagnostics; `census20261013-isolated`.
- 2026-10-10: Post-campaign issue sweep (2026-10-10/11); `legacy20261010-issue-sweep`.
- 2026-10-13: residual construction and simplifier closeout (#250/#247); `residual-construction20261013`.
- 2026-10-15: deterministic-Buchi solver choice for the parity arm; `otd-parity-engines20261015`.
- 2026-10-13: memory/search attribution (#251); `memory-search20261013`.
- 2026-10-17: documentation and archival closeout; `refactor-closeout20261017`.
- before 2026-11-20 (undated): Earlier representation and normalization studies; `refactor-closeout20261017`.
- 2026-11-05: Lean DPA closeout; tlsf-tools #105 retained; `lean-20261104`.
- 2026-11-06: Generated-input property tests (#369); one wrong verdict traced to Spot; `fuzz-20261105`.
- 2026-11-07: Seven default arms; real:small:forward leaves the shipped portfolio (#359); `arms-20261107`.
- 2026-11-10: Native-arm census; the formula unrealizability arm translates at low effort (#378, #370); `native-20261107`.
- 2026-11-14: Split sparse Oracle payloads (C01); retain the storage saving; `sprint-20261111`.
- 2026-11-15: Index sparse search ranks through node IDs (#397); `acacia-397-20261116`.
