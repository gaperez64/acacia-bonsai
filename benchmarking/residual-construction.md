# Residual construction at 60 seconds (#250, 2026-10-13)

Applying the prewritten `DECISION-RULE.md` selects **no production interface or default
change**. Generic deterministic construction supports research on a typed,
acceptance-aware automaton-provider boundary, with an explicit conversion policy at
that boundary. A production game adapter needs a separate correctness and coverage
gate. The matched comparison is mixed, and decisive conversion outcomes still need
the rule's serial, cooled, alternating repetitions. These research decisions and
saved automata are **not Acacia solves**.

Evidence: `residual-construction20261013` in [evidence-index.tsv](evidence-index.tsv),
with `postmem20261011-p2` for selection, `postmem20261012-closing` for archived PMC4
worker records, and `binary-PMC4-b3953b43` for its executable. The preparation
`README.md` and `REPORT.md` retain their original pending-run wording; the completed
`measured-60-247/DONE.json`, `analysis-60-247/summary.json` and phase tables take
precedence. This closeout reads completed measurements; it runs no new timing campaign.

## Measured boundary and panel

The panel intersects the 55 P2 `general_construction_suffices` inputs with fresh
PMC4 60-second unsolved rows, leaving **53**. `Automata32S.ltl` and
`chomp_pb_4_3_pe_.ltl` are removed because PMC4 solves them. The selection audit
retains all 57 original classifications and both PMM/PMC4 caps; historical PMM
outcomes do not select the new panel.

Spot **2.16** ordinary `ltlsynt --realizability` decisions disable both shortcuts
(`--obligation-synthesis=no --bypass=no`) and use LAR, PS, DS, SD and ACD with default
synthesis decomposition. Every input has a completed best algorithm, ranked by
internal total time, scoped wall time, then fixed order LAR/PS/DS/SD/ACD. That
algorithm is replayed with `--decompose=no`. Translator-internal `ltl-split` remains
at its installed default and is a separate control.

Matched `ltl2tgba` translations use the identical captured bytes from the first
subjob of PMC4's primary `real:small:backward` worker, native TLSF frontend and
basic weakening, **before** the guarded eventual/universal rewrite. All modes use
`--high` and `simul=0,ba-simul=0,det-simul=0,tls-impl=1,wdba-minimize=2`:
BA/Small/SBAcc (`-B --small`), generic deterministic TELA (`-D -G`), and
deterministic parity (`-D -P`). This boundary does not replay a guard-firing PMC4
translation, other worker orientations or all components. Spot's deterministic
preference may change simplification internally, so completion alone cannot
attribute the gap to the acceptance representation.

The 45 completed generic deterministic HOAs feed both
`autfilt -P --any --low -x acd=0/1`; each pair binds the **same input HOA SHA-256**.
The replay includes parsing and minimal acceptance cleanup, not an isolated kernel
timer. Saved-HOA parsing separates six already-parity/generalized-Büchi inputs
(three Büchi, three `all`) from 39 other TELA inputs. The archived text classifier
labels those 39 unclassified; parsing their saved acceptance conditions establishes
this split. The six simple acceptances may bypass LAR/ACD kernels, so their tiny
conversion costs do not compare the kernels. Conversion completion alone
says nothing about equal downstream splitting or game-solving cost.

## Decisions and observed stage costs

Times below are **seconds: median (number of observed completed phase reports)**
from `analysis-60-247/phase-table.tsv`. A phase can complete in a run that later
times out; those reports are retained. Missing times are unknown, not zero. All
unfinished ordinary runs below are timeouts, not errors or memory failures.
Preprocessing has **no separate CLI timer**: translation includes simplification,
and the unallocated internal remainder includes bookkeeping. Never subtract a
60-second cap to infer preprocessing or an unentered phase.

| Algorithm | REAL | UNREAL | Unfinished | Preprocessing | Construction | Parity conversion | Splitting | Solving |
|---|---:|---:|---:|---|---|---|---|---|
| LAR | 27 | 26 | 0 | unobserved | 0.320591 (53) | 0.004192 (53) | 0.018853 (53) | 0.000765 (53) |
| PS | 15 | 15 | 23 | unobserved | 1.415740 (31) | 0.002733 (31) | 0.076931 (30) | 0.000361 (30) |
| DS | 10 | 3 | 40 | unobserved | 2.626435 (44) | 1.373400 (13) | 0.015865 (13) | 0.000818 (13) |
| SD | 7 | 2 | 44 | unobserved | 2.636675 (44) | 3.034950 (9) | 0.007591 (44) | 0.000782 (9) |
| ACD | 27 | 26 | 0 | unobserved | 0.317096 (53) | 0.001547 (53) | 0.027097 (53) | 0.000248 (53) |

LAR and ACD each decide all 53 inputs (27 REAL, 26 UNREAL); PS, DS and SD decide
subsets and add no unique input beyond them. The 158 default decisions agree on
every overlap. Including the decomposition controls gives **210 ordinary decisions**.
Best completed algorithms are ACD 25, PS 15, LAR 11, SD 2, DS 0; this is an
evaluation ranking, never a per-input production policy.

The 53 best-algorithm no-decomposition controls decide 52 inputs with matching
verdicts. PS on `LedMatrix.ltl` times out despite completing with decomposition.
For the other 52, the no-decomposition/default internal-total ratio has median
1.002 and range 0.965–1.113. Thus decomposition can affect feasibility on one
input, but does not explain the broad matched deterministic-construction gap.

| Matched translation | Completed / 53 | Timed out | Construction median (reports) |
|---|---:|---:|---|
| BA/Small/SBAcc, current | 23 | 30 | 1.356230 (23) |
| Generic deterministic TELA, `-D -G` | 45 | 8 | 0.249246 (45) |
| Deterministic parity, `-D -P` | 25 | 28 | 0.062516 (25) |
| BA, `tls-max-ops=1` | 22 | 31 | 1.302420 (22) |
| BA, `tls-impl=0` | 22 | 31 | 1.317020 (22) |

Different completion populations make these medians descriptive, not a matched
speedup. Generic deterministic translation completes on **29** BA timeouts, loses
**seven** BA completions, and shares **16** completions; one input times out in
both. The seven BA-only completions are `arbiter_with_cancel_pb_6_pe_.ltl`,
`full_arbiter_enc_pb_8_pe_.ltl`, `full_arbiter_enc_pb_10_pe_.ltl`,
`simple_arbiter_enc_pb_11_pe_.ltl`, `simple_arbiter_with_hints_pb_10_pe_.ltl`,
`workstation_resupply_pb_4_pe_.ltl` and `workstation_resupply_pb_5_pe_.ltl`.
On those 16 common completions, the generic/BA construction-time ratio has median
0.023923 and maximum 0.966793; state ratio median 0.026177, maximum 0.230769.
The edge ratio has median 0.008277 but maximum **879.691**: on
`ltl2dba_theta_pb_16_pe_.ltl`, generic has 2 states/262,148 edges versus BA
59/298. Deterministic parity completes on ten BA timeouts but loses eight BA
completions. Neither deterministic representation is a universal replacement.

| Same-HOA conversion | Completed / 45 | Timed out | Conversion median (reports) |
|---|---:|---:|---|
| LAR | 44 | 1 | 0.002999 (44) |
| ACD | 45 | 0 | 0.001086 (45) |

On `sort53.ltl`, LAR replay times out at 60 seconds; ACD conversion completes in
0.006077 seconds (0.134365 seconds scoped wall). Ordinary LAR does decide it,
but conversion takes 41.9922 seconds and total wall is 56.544919, versus ACD
conversion 0.004068 seconds and wall 14.782609. Conversely,
`ltl2dba_theta_pb_16_pe_.ltl` has ACD replay wall 6.356048 versus LAR 2.483626,
with ACD 49 states/6,422,626 edges versus LAR 128/4,303. Replay and ordinary
construction inputs are different boundaries; the replay timeout is not an
ordinary LAR decision failure. These observations support an explicit research
conversion policy, not an unconditional ACD production default.

## PMC4 K/search/check context

The archive retains 1,060 worker rows across PMM/PMC4 and 17/60-second records.
This table uses only the **53 PMC4 60-second workers per legacy arm**.
`K workers` counts workers with an observed K; the range is observed values.
Completed times are medians of each worker's cumulative completed stage durations;
censored columns are medians of observed lower bounds. They can overlap within
a worker and across workers. The race runs concurrently; neither stage nor
worker sums are invocation wall time. `K-attempt` encloses search/check, so it
must not be added to them.

| Worker | Rows | K workers | K range | Translation | Translation censored | K-attempt | Search | Search censored | Check | Check censored |
|---|---:|---:|---|---|---|---|---|---|---|---|
| REAL backward | 53 | 21 | 2–99 | 1.904506 (22) | 60.007128 (31) | unobserved | 0.017374 (12) | 56.617429 (16) | 0.000002 (21) | unobserved |
| REAL forward | 53 | 21 | 2–99 | 1.905709 (22) | 60.007150 (31) | unobserved | 3.080622 (16) | 10.253469 (2) | unobserved | unobserved |
| UNREAL spot-guarded-sparse | 53 | 24 | 2–99 | 0.394433 (25) | 60.011924 (28) | 31.927785 (22) | 26.759844 (22) | 16.356238 (19) | 10.198510 (22) | 2.268975 (4) |
| UNREAL forward | 53 | 24 | 2–99 | 2.462901 (38) | 60.051107 (15) | unobserved | 30.030965 (22) | 7.423975 (18) | unobserved | unobserved |

The primary REAL backward arm has 31 translation-censored inputs and 22 with
completed translation reports, but only 21 observed K sequences. Completed BA
artifacts therefore do not imply backend completion or a verdict. On inputs
that reach it, the guarded sparse UNREAL backend also incurs substantial K,
search and check costs. One such worker has dropped records; its observed
reports remain incomplete evidence. No PMC4 preprocessing, deterministic/parity
conversion or input/output-splitting timer is exposed here. Unobserved stages
remain unobserved. Independent verification and source/game/proof binding must
survive any future interface change.

## Fixed #247 clause

Both BA variants hold original worker bytes, BA/Small/SBAcc/High and other scalar
options fixed. `tls-max-ops=1` retains `tls-impl=1`; the other variant sets
`tls-impl=0`. Separate fresh-process probes on the original worker bytes all
complete: simplification medians are 0.016774 seconds current, 0.000788
max-ops-1 and 0.000771 impl-0. Probes omit translator Boolean relabeling and
are not inline phase timers. Full translation includes its own simplification;
probe times are never subtracted or supplied as replacement inputs.

The complete 60-second panel yields **zero translation rescues for either
variant**. Each has 22 common completed BA pairs, maximum state and edge ratios
**1.0**, satisfying the fixed 1.25 size guard. Completed paired automata have
identical transition graphs, Boolean edge labels and acceptance (verified with
Spot isomorphism; serialized HOA formula names and ordering may differ). Both
variants lose the current completion on
`square5x5-real.ltl` (current 46,087 states/2,407,294 edges, construction
45.8174 seconds); each has 31 timeouts, with no translation error or memory
failure. There is no recovered automaton, and sizes of unfinished automata
remain unknown. Per-input sizes, ratios and failures are retained in
`simplifier-comparison.tsv/json` and `issue-247-screen.json`.

The rule requires a rescue **and** the common-pair size guard for the same
variant. Passing sizes without a rescue fails that conjunction. **Close #247
with the upstream report and no Acacia change.** The driver still owns checking
upstream main and sending the unsent report in `spot-implication20261013`;
this closeout does not claim either external action happened.

## Interface decision and archive exclusions

Rule 1 cannot explain the whole panel by downstream cost: matched BA loses 29
generic deterministic completions. Rule 2 does not select a preprocessing-only
change: decomposition is usually neutral and the matched construction gap
persists. Rule 3 supports researching a typed provider returning deterministic
TELA with acceptance, polarity and timing provenance; TELA/parity must never be
coerced into the existing Büchi rank game. Rule 4 supports measuring an explicit
conversion policy at that boundary using shared-HOA replay and ordinary game
context. Rule 5 selects **no production change** because completions and edge
sizes are mixed and decisive outcomes have not received the required repeats.
No new worker, external ltlsynt arm, default change or game adapter is admitted.
Future production work requires semantics, checking, matched PMC4 controls,
preservation of Acacia-exclusive solves, and full coverage/memory gates.

`residual-construction20261013` retains the original rule, completed panel,
analysis, earlier preparation/smoke evidence and the exclusion audit. Archive
members use `build_scratch/sweep-close/evidence-input/_bm-logs.i250/`; original
absolute paths inside records remain provenance. The only exclusions are the
47,625-byte `__pycache__/driver.cpython-314.pyc` and these regenerable files
larger than 50 MiB (both `automaton.hoa` and `stdout` at each location):

| Original attempt under `measured-60-247` | HOA bytes | stdout bytes |
|---|---:|---:|
| `input-028/convert-acd/attempt-0001` | 398,854,245 | 398,854,296 |
| `input-031/convert-lar/attempt-0001` | 157,726,817 | 157,726,915 |
| `input-051/translate-ba/attempt-0001` | 130,194,930 | 130,194,963 |

Inputs, exact commands, pinned tool identities, original timing/memory records,
automaton sizes and digests remain archived; `EXCLUSIONS.tsv` records every
excluded file's bytes, SHA-256 and reason. No obfuscation mappings are published.
`obligation-trust20261013` separately retains the requested #248 screens and
driver scripts, while `binary-PMC5-0b8b9be1` freezes the #255 decision-mode
candidate at source `c48592af`. That candidate is not an admitted replacement
for the PMC4 baseline.
