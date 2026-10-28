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
