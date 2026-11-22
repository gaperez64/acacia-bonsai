# Results

[Measurement protocol](README.md) and [evidence index](evidence-index.tsv).

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
