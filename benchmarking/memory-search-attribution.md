# Attributing games after compact action construction

The closed panel driver and its execution procedures are preserved in the
campaign archive. The recorded runs used serial invocations at 60 s, 8 GiB and
zero swap, with phase records and invocation-bound captures. Historical plans
bind executable and source digests, flags, resource settings and cooldown policy.
Restore the archived tool snapshot when reproducing this campaign.

Selection requires a legacy construction/search stage still unfinished at a fresh
17 or 60 s cap, or a paired nonsolve-to-solve gain in an i208 screen. Native declines
and native preparation alone do not qualify a legacy construction transition.
Historical MEMOUT and paired gains come first. Up to three slots cover fresh
construction/search evidence outside the earlier screens; other slots cycle through
historical transition, unfinished-stage and source-size strata. Source byte count
and digest break ties. Names identify manifest rows and never select a solver route.
The capacities are evaluation choices, not solver thresholds. `panel.json` retains
both closing observations, old-screen evidence, source hashes and selection flags;
`classification.tsv` covers every fresh 17/60 s row.

The shared P0 mapping is `benchlib.phase_plan_stage`; `PhaseRecords.worker_events`
joins native summaries by emitting PID and explicit native arm without changing raw
wire objects. `legacy-phase-table.py` adds native lifecycle intervals and `plan_stage`.
`classify-coverage.py` exposes `last_plan_stage`. The retained phase reader can
refresh interval tables from explicit invocation directories:

```sh
python3 benchmarking/legacy-phase-table.py \
  --invocation candidate=RESTORED_PHASE_DIRECTORY --output _bm-logs.i251/phases
```

Native preparation maps to IO/action construction, direct/seed solver work to
K-search, and explicit target checks to independent verification. Original-game
obligation work maps to K-search; its completed outcome includes preparation and
its same-library check, whose separate timing remains unknown. A native route can
contain nested work and has no K identity unless one was actually recorded.
`summary-only` durations have no reconstructible endpoints and can include zero
counters for unentered work; they do not establish a completed search or checker.
Only explicit completion events establish completion. Missing endpoints stay
unknown. Only the last entered native interval whose stage and route match the
signalled terminal record, or an explicitly cancelled obligation interval, gets a
censored lower bound. Enclosing or earlier missing completions are never filled in.
Writer failures, live drop counters and sequence gaps preserve partial delivery.

P0 invocation attribution treats explicit MEMOUT separately. On nonsolves, only
surviving/signalled workers vote; earlier completed declines do not. Their latest
stage evidence maps to translation, construction, search or verification; a newer
terminal stage takes precedence when its entry packet is missing. Different
known categories yield `mixed`; absent worker/stage evidence yields `unknown`.
Observed terminal route labels can identify an obstruction without supplying an
entry time or duration. This rule makes no claim about the longest critical path.

Frozen PMC4 has `acacia_enable_diagnostics=false`. Its existing sparse counter
`first_useful_rank_query_ms` measures the first successful oracle query that uses a
rank, from row-store factory creation; boundary shortcuts and failed queries do not
count. Its phase metrics and captures are retained, including incomplete searches.
The frozen executable cannot emit the new dense marker. Missing dense query/growth
observations remain unknown in that treatment.

The additional dense marker is behind `ACACIA_PHASE_RECORDS`, independent of the
compile-time diagnostics option. A useful dense query is one completed standard
rank/action application during search. `first_useful_action_query_ns` records its
absolute monotonic time. `query_snapshot_ns`, `completed_action_applications`,
`query_rank_support` and `query_rank_maximum` sample the source rank after the
application, at the first four applications and then powers of two (at most 65
samples per stage). A stage change resets the count. Observation off performs no
rank scan, counter update, clock or resource sample. These are rank samples, not
unique-rank counts or total retained memory. Timestamp metrics are excluded from
cap-independence equality; deterministic sampled counts and rank values remain
compared.

Use `--observation-binary <binary>` for a separately built supplemental treatment
containing the marker and, for existing dense demand checkpoints,
`acacia_enable_diagnostics=true`. It must support native TLSF and use the intended
portfolio; this worktree predates the obligation-arm source, so its generic
correctness builds are not replacements for frozen PMC4. The driver must build the
observation treatment from the intended shipping source/configuration. The script
records its separate executable digest and label and never substitutes it for the
frozen baseline. No new solver option or limit was introduced here.

External `memory.current` and `memory.peak` samples poll every 20 ms in the workload
cgroup. Verification entry is joined to samples immediately before and after it,
with a global maximum bracket of 100 ms. Values are bracketed observations, not
exact instantaneous boundary memory or process RSS. Missing brackets and unentered
checkers remain unknown. Final scope peak/events come from the existing retained
cgroup observer. Byte-estimate counters retain their names and are not RSS.

`analyze --output <run-directory>` rebuilds `per-job.tsv`, `analysis.json`, each
job's phase table and `REPORT.md`. K sequences contain one entry per `k-attempt`
occurrence, retaining repeated K values for distinct attempts; an unmatched nested
`search` supplies a fallback when the enclosing attempt was not recorded. Nested
search/check stages remain separate. Worker identities, query definitions/origins,
completed search/check stages, raw rank/support metrics, existing diagnostic snapshots
and memory brackets remain separate. Partial capture history tails are discarded
without repairing them or inferring completion. The recommendation section is a blank
evidence-based template; it changes no K, row or memo setting.

## 2026-10-13 — 60 s attribution results (#251)

The driver completed both 30-job panels and analyzed
both result directories. This section only interprets those retained records. Each
binary returned the same seven verdicts (two REALIZABLE, five UNREALIZABLE) and 23
TIMEOUTs; neither panel recorded a MEMOUT. The timeout attribution is identical:
eight K-search, one construction, and 14 mixed invocations. No timeout is exclusively
translation- or verification-limited, and none has an unknown invocation obstruction.
Mixed means that surviving workers occupy different mapped stages, not that those
stages take equal time or form one critical path. Selection is evidence-directed;
these 30 inputs do not estimate full-corpus coverage or a runtime improvement.

### Treatments and provenance

Every number in the **frozen** tables below comes from **PMC4**, executable SHA-256
`b3953b437ede826eabfeb54a5d6f2deb3f79f59153189b7ae79a3c6b35b7951f`, at
`/home/gperez/GIT-repos/acacia-bonsai/build_scratch/issues/restore/binary-PMC4-b3953b43/_bm-logs.postmem/frozen/PMC4/src/acacia-bonsai`.
Its records are in `_bm-logs.i251/frozen-60s` (archive `memory-search20261013`).
Every number in the **observation** tables comes from **PMO251**, built from this
branch, executable SHA-256
`1746c36a87c7633122b24216cd234a75678868149e36ce90e9c523e3d1d710e9`, at
`/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.issues/frozen/PMO251/src/acacia-bonsai`.
Its records are in `_bm-logs.i251/observation-60s` (archive `memory-search20261013`).
The shared panel manifest has SHA-256
`4fb38cf9263087790b8ab9db162f88914c12e8bbd85d7ad17f6c4668eb8b9bd1`.
All 60 jobs are attempt 1, use the recorded default invocation (`-T`, no extra flags),
and have a 60 s external cap, 8 GiB scope limit and zero swap. Cooldown readiness is
recorded separately from solver time. Source and executable digests are bound in each
`attempt.json`/`run.json`; this analysis checked the current bytes against those bindings.

Both binaries have `acacia_enable_diagnostics=false`. PMO251 adds the phase-switch
controlled dense marker; neither treatment contains diagnostic demand snapshots.
Existing sparse phase counters still supply support/antichain metrics for completed
US searches. The frozen manifest declares six arms, including the obligation arm, and both panels record worker identities 0–4 only. That is the expected behavior, not a discrepancy: the obligation arm starts only when the objective is a syntactic obligation, and none of the 30 panel inputs is one, so the five ordinary arms are the whole portfolio actually launched on this panel. PMO251, built from this branch (which predates the obligation arm), lists the same five arms in its Meson options. Neither treatment measures the obligation arm, and observer-only timing equivalence is not assumed; PMO251 remains a supplemental treatment.

### Reading the per-job tables

Job numbers resolve to `job-NN` in the indicated treatment directory. Input names
omit `.ltl` only for readability. Worker labels retain the recorded identities:
RB = 0, REAL backward; RF = 1, REAL forward; US = 2, UNREAL formula guarded sparse;
UF = 3, UNREAL automaton forward; N = 4, native OxiDD lift/direct route. Physical PIDs,
stage IDs and full intermediate observations remain in each `analysis.json` and
`job-NN/table/{phases.tsv,workers.json}`. Labels identify evidence, not solver dispatch.

K sequences retain subjob identity (`s1`, `s2`). `a:d:b` lists every K from a through b
in steps of d; `,99` is the separately recorded last bound after 98. Unmarked attempts
completed; `~` is censored, `!` stopped with `RESOURCE_LIMIT`, and `?` has a missing
enclosing completion. A censored nested search may coexist with a `?` enclosing
`k-attempt`. Native direct search has no recorded K and is absent from the K column.
For `generalized_buffer`, RB/RF's repeated K=2 belongs to two distinct subjobs; US/UF
also start translating a later subjob after exhausting the first subjob's K schedule.

First-query values give each worker's first recorded useful query: **D** is a
completed dense rank/action application, in seconds from invocation launch (including
scope startup), followed by milliseconds from that search's entry in parentheses.
**P** is a successful sparse oracle query using a rank, in milliseconds from the
row-store factory start. P is not invocation-relative and cannot be used to rank
workers against D. Unknown means no counter was observed, not zero work or a query
failure. Unlisted workers have no observed value for that field. Subsequent per-K
first-query records remain in the underlying JSON.

Completed-stage counts are **S** search, **V** verification, **C** certificate-check,
**A** enclosing k-attempt and **T** native target_check. A is shown separately and
never counted as another search; V and C can be nested. Only explicit completions
count. An inapplicable fixed-point verifier and native `summary-only` records do not
count. Completed checks can reject candidates; a completed search is not a verdict.

Dense growth (**D**) reports the first and last samples of the first and last sampled
search stages per worker: source-rank support **σ**, source-rank maximum **h**, and
last sampled application count **n** (a lower bound, not the terminal total). Counts
reset per stage; intermediate stages/samples are retained in JSON. Sparse growth
(**P**) spans the first and last completed search snapshots: game states **G**, sum
of rank supports **Σ**, maximum support **H**, and losing-antichain peak size **L**.
These are different quantities: H is not h, σ is not a unique-rank count, and changes
across K do not establish monotonic growth within an unfinished stage. Frozen dense
growth and uncompleted sparse checkpoints stay unknown. A single sparse snapshot
(such as US K=2 on job 07) establishes no within-stage growth.

Boundary values are **scope memory.current before→after entry, in MiB**, at the
first and last entered check for each worker (plus explicit missing brackets).
They are whole-scope observations, including competing workers, not process RSS or
exact instantaneous checker memory. Scope peak is separately labeled. PMC4 has 112
valid brackets spanning 20.253–41.232 ms; PMO251 has 111 spanning 20.178–21.124 ms.
Both have three missing early brackets on `generalized_buffer` (RF V/C and US's
first V); no value is substituted from the later scope peak. No-checker means no
applicable checker entry, even if a rank search was entered.

Censoring lists every active stage's observed lower bound, from entry to terminal
signal. Solved jobs cancel competing workers on a winner; those bounds are not
60 s timeouts. Other enclosing/native intervals with missing endpoints remain
unknown and have no inferred duration. All 150 worker delivery summaries in each
treatment are `observed`, with zero reported dropped records; delivered records do
not turn absent completions into completed stages.

### Frozen PMC4 results

Source: `_bm-logs.i251/frozen-60s/analysis.json`, `per-job.tsv`, and raw job tables.

| Job / input | Outcome | K sequence by worker | First useful query | Completed stages | Rank/support samples | Boundary scope MiB | Scope peak MiB | Current obstruction | Censoring (lower bounds) |
|---|---|---|---|---|---|---|---:|---|---|
| 00 `robot_grid_pb_8_8_pe_` | TIMEOUT | RB s1: 2~<br>RF s1: 2! | unknown | none | unknown | unknown (no checker entered) | 403.64 | K-search | cap:<br>RB search s1/K2 ≥47.460 s |
| 01 `workstation_resupply_pb_3_pe_` | REALIZABLE | RB s1: 2:3:8,11~<br>RF s1: 2:3:20<br>US s1: 2:3:32,35?<br>UF s1: 2,5,8! | US P 0.069810 ms | RB S3<br>RF S7/V1/C1<br>US S11/V11/A11<br>UF S2 | US P K2→K32: G657→3144,Σ3918→18510,H6→6,L647→684 | RF V K20 189.30→189.30; C K20 189.30→189.30<br>US V K2 117.95→118.45; V K32 188.03→188.03 | 206.31 | solved | winner cancellation:<br>RB search s1/K11 ≥10.779 s<br>US search s1/K35 ≥0.205 s<br>N native preparation ≥12.918 s |
| 02 `evasion-real` | TIMEOUT | RB s1: 2~<br>RF s1: 2!<br>US s1: 2:3:29,32?<br>UF s1: 2:3:11,14~ | US P 0.082274 ms | US S10/V10/A10<br>UF S4 | US P K2→K29: G3→32058,Σ78→535970,H76→80,L3→169 | US V K2 62.81→65.68; V K29 1023.65→997.38 | 1031.53 | K-search | cap:<br>RB search s1/K2 ≥36.897 s<br>US search s1/K32 ≥3.968 s<br>UF search s1/K14 ≥35.330 s |
| 03 `robot_grid_pb_7_7_pe_` | TIMEOUT | RB s1: 2~<br>RF s1: 2! | unknown | none | unknown | unknown (no checker entered) | 1235.40 | translation + K-search | cap:<br>RB search s1/K2 ≥51.927 s<br>US translation ≥60.016 s<br>UF translation ≥60.025 s |
| 04 `collector_v1_pb_11_pe_` | TIMEOUT | RB s1: 2~<br>RF s1: 2!<br>US s1: 2? | unknown | none | unknown | unknown (no checker entered) | 2390.75 | K-search | cap:<br>RB search s1/K2 ≥45.657 s<br>US search s1/K2 ≥12.277 s |
| 05 `robot-to-target0` | TIMEOUT | RB s1: 2~<br>RF s1: 2,5! | unknown | RF S1 | unknown | unknown (no checker entered) | 1024.98 | translation + construction + K-search | cap:<br>RB search s1/K2 ≥41.372 s<br>US translation ≥59.841 s<br>UF IO preparation ≥49.631 s |
| 06 `Morning2s_d6c5ac79` | TIMEOUT | US s1: 2? | unknown | none | unknown | unknown (no checker entered) | 2276.99 | translation + construction + K-search | cap:<br>RB translation ≥60.067 s<br>RF translation ≥60.066 s<br>US search s1/K2 ≥50.313 s<br>UF IO decoding ≥20.967 s<br>N native preparation ≥60.010 s |
| 07 `lift_gr1+_pb_7_pe_` | TIMEOUT | US s1: 2,5?<br>UF s1: 2,5! | US P 0.108456 ms | US S1/V1/A1<br>UF S1 | US P K2→K2: G3366→3366,Σ31562→31562,H18→18,L627→627 | US V K2 1046.28→1051.53 | 4700.37 | construction + K-search | cap:<br>RB IO preparation ≥13.158 s<br>RF IO preparation ≥14.517 s<br>US search s1/K5 ≥54.594 s |
| 08 `arbiter_pb_10_pe_` | TIMEOUT | RB s1: 2~<br>RF s1: 2! | unknown | none | unknown | unknown (no checker entered) | 1731.31 | translation + K-search | cap:<br>RB search s1/K2 ≥31.211 s<br>US translation ≥59.977 s<br>UF translation ≥59.947 s<br>N native direct ≥59.535 s |
| 09 `round_robin_arbiter_unreal2_pb_6_pe_` | UNREALIZABLE | — | unknown | N T1 | unknown | N T (K absent) 1060.23→1009.74 | 1885.35 | solved | winner cancellation:<br>RB Booleanization ≥1.852 s<br>RF Booleanization ≥1.902 s<br>US translation ≥26.951 s<br>UF translation ≥26.940 s |
| 10 `collector_v1_pb_15_pe_` | TIMEOUT | — | unknown | none | unknown | unknown (no checker entered) | 4956.57 | translation + construction | cap:<br>RB preprocessing ≥2.331 s<br>RF preprocessing ≥2.325 s<br>US translation ≥59.953 s<br>UF translation ≥59.953 s |
| 11 `collector_v1_pb_13_pe_` | TIMEOUT | — | unknown | none | unknown | unknown (no checker entered) | 4645.55 | translation + construction | cap:<br>RB IO preparation ≥50.745 s<br>RF IO preparation ≥53.900 s<br>US translation ≥60.033 s |
| 12 `collector_v1_pb_12_pe_` | TIMEOUT | — | unknown | none | unknown | unknown (no checker entered) | 4077.81 | construction | cap:<br>RB IO decoding ≥2.972 s<br>RF IO decoding ≥5.991 s<br>US Booleanization ≥36.148 s<br>N native preparation ≥60.135 s |
| 13 `lift_pb_6_pe_` | TIMEOUT | RB s1: 2~<br>RF s1: 2,5!<br>US s1: 2:3:8,11?<br>UF s1: 2,5,8~ | US P 0.248531 ms | RF S1<br>US S3/V3/A3<br>UF S2 | US P K2→K8: G3→7684,Σ4→40818,H2→8,L3→1362 | US V K2 241.60→241.58; V K8 650.03→650.03 | 743.18 | K-search | cap:<br>RB search s1/K2 ≥55.365 s<br>US search s1/K11 ≥21.612 s<br>UF search s1/K8 ≥30.349 s<br>N native direct ≥59.621 s |
| 14 `thermostat-GF-unreal1` | UNREALIZABLE | US s1: 2,5<br>UF s1: 2,5,8~ | US P 0.088189 ms | US S2/V2/A2<br>UF S2 | US P K2→K5: G12→668,Σ327→21432,H61→64,L9→166 | US V K2 1280.35→1281.12; V K5 1312.59→1312.59 | 1313.03 | solved | winner cancellation:<br>RB translation ≥28.720 s<br>RF translation ≥28.720 s<br>UF search s1/K8 ≥3.778 s |
| 15 `reversible-lane-r-real` | TIMEOUT | RB s1: 2~<br>RF s1: 2!<br>US s1: 2:3:11,14?<br>UF s1: 2,5,8~ | US P 0.055459 ms | US S4/V4/A4<br>UF S2 | US P K2→K11: G4→29143,Σ22→500270,H10→22,L4→337 | US V K2 58.31→59.24; V K11 465.41→465.41 | 838.60 | K-search | cap:<br>RB search s1/K2 ≥13.673 s<br>US search s1/K14 ≥30.407 s<br>UF search s1/K8 ≥46.917 s |
| 16 `Morning_88c5c1c3` | TIMEOUT | US s1: 2,5,8?<br>UF s1: 2,5,8! | US P 0.102925 ms | US S2/V2/A2<br>UF S2 | US P K2→K5: G27→29782,Σ258→371908,H13→17,L18→235 | US V K2 175.48→195.60; V K5 1667.81→1668.31 | 3152.71 | translation + construction + K-search | cap:<br>RB translation ≥60.047 s<br>RF translation ≥60.044 s<br>US search s1/K8 ≥40.472 s<br>N native preparation ≥60.074 s |
| 17 `simple_arbiter_enc_pb_32_pe_` | TIMEOUT | RB s1: 2,5~<br>RF s1: 2,5! | unknown | RB S1<br>RF S1 | unknown | unknown (no checker entered) | 1399.67 | translation + K-search | cap:<br>RB search s1/K5 ≥59.798 s<br>US translation ≥59.997 s<br>UF translation ≥59.984 s |
| 18 `round_robin_arbiter_pb_8_pe_` | REALIZABLE | US s1: 2? | unknown | N T1 | unknown | N T (K absent) 925.28→844.76 | 1371.82 | solved | winner cancellation:<br>RB translation ≥30.504 s<br>RF translation ≥30.504 s<br>US search s1/K2 ≥28.691 s |
| 19 `F-G-contradiction-110` | UNREALIZABLE | RB s1: 2~<br>RF s1: 2,5~<br>US s1: 2:3:20<br>UF s1: 2,5,8~ | US P 0.075746 ms | RF S1<br>US S7/V7/A7<br>UF S2 | US P K2→K20: G8→738,Σ89→8255,H17→17,L8→55 | US V K2 89.46→89.84; V K20 705.77→706.01 | 729.49 | solved | winner cancellation:<br>RB search s1/K2 ≥12.151 s<br>RF search s1/K5 ≥0.568 s<br>UF search s1/K8 ≥0.737 s |
| 20 `infinite-race-u13` | UNREALIZABLE | RB s1: 2~<br>RF s1: 2,5!<br>US s1: 2:3:14<br>UF s1: 2:3:50,53~ | US P 0.089893 ms | RF S1<br>US S5/V5/A5<br>UF S17 | US P K2→K14: G25→7333,Σ449→127086,H23→23,L12→348 | US V K2 71.25→71.48; V K14 427.08→427.08 | 481.46 | solved | winner cancellation:<br>RB search s1/K2 ≥23.034 s<br>UF search s1/K53 ≥1.653 s |
| 21 `thermostat-F-real` | TIMEOUT | RB s1: 2:3:98,99<br>RF s1: 2:3:20,23!<br>US s1: 2:3:32,35?<br>UF s1: 2:3:17,20~ | US P 0.058583 ms | RB S34<br>RF S7<br>US S11/V11/A11<br>UF S6 | US P K2→K32: G18→33660,Σ259→586288,H19→20,L10→165 | US V K2 55.71→57.12; V K32 388.81→388.81 | 390.84 | K-search | cap:<br>US search s1/K35 ≥6.340 s<br>UF search s1/K20 ≥15.503 s |
| 22 `helipad-real` | TIMEOUT | US s1: 2:3:8,11?<br>UF s1: 2,5,8~ | US P 0.087629 ms | US S4/V3/A3<br>UF S2 | US P K2→K11: G4→10265,Σ38→204348,H18→28,L4→1585 | US V K2 883.45→883.76; V K11 1094.74→1094.74 | 1094.75 | translation + K-search + verification | cap:<br>RB translation ≥59.999 s<br>RF translation ≥59.999 s<br>US verification s1/K11 ≥0.070 s<br>UF search s1/K8 ≥40.663 s |
| 23 `generalized_buffer_pb_6_pe_` | TIMEOUT | RB s1: 2<br>RB s2: 2~<br>RF s1: 2<br>RF s2: 2!<br>US s1: 2:3:98,99<br>UF s1: 2:3:98,99 | US P 0.046132 ms | RB S1<br>RF S1/V1/C1<br>US S34/V34/A34<br>UF S34 | US P K2→K99: G7→14696,Σ24→157868,H9→20,L7→102 | RF C/V K2 unknown (no bracket)<br>US V K2 unknown (no bracket); V K5 55.00→56.50; V K99 491.70→491.70 | 717.25 | translation + K-search | cap:<br>RB search s2/K2 ≥59.828 s<br>US translation ≥14.174 s<br>UF translation ≥59.669 s |
| 24 `items_processing-real` | TIMEOUT | — | unknown | none | unknown | unknown (no checker entered) | 1130.12 | translation + construction | cap:<br>RB translation ≥59.977 s<br>RF translation ≥59.977 s<br>US translation ≥59.982 s<br>UF IO preparation ≥7.822 s |
| 25 `thermostat-GF-real` | TIMEOUT | RB s1: 2~<br>RF s1: 2!<br>US s1: 2:3:11,14?<br>UF s1: 2:3:32,35~ | US P 0.093990 ms | US S4/V4/A4<br>UF S11 | US P K2→K11: G12→13447,Σ269→278366,H50→50,L9→614 | US V K2 130.49→131.68; V K11 1080.98→1080.98 | 1448.14 | K-search | cap:<br>RB search s1/K2 ≥35.880 s<br>US search s1/K14 ≥16.231 s<br>UF search s1/K35 ≥9.880 s |
| 26 `lift_gr1+_pb_6_pe_` | TIMEOUT | RB s1: 2~<br>RF s1: 2,5!<br>US s1: 2,5,8?<br>UF s1: 2,5! | US P 0.090170 ms | RF S1<br>US S2/V2/A2<br>UF S1 | US P K2→K5: G1586→16547,Σ13682→148167,H16→16,L410→500 | US V K2 182.82→183.07; V K5 1805.41→1806.16 | 1953.26 | construction + K-search | cap:<br>RB search s1/K2 ≥42.854 s<br>US search s1/K8 ≥36.040 s<br>N native preparation ≥59.958 s |
| 27 `lift_pb_8_pe_` | TIMEOUT | — | unknown | none | unknown | unknown (no checker entered) | 1034.16 | translation + construction + K-search | cap:<br>RB Booleanization ≥13.249 s<br>RF Booleanization ≥13.253 s<br>US translation ≥60.016 s<br>N native direct ≥59.292 s |
| 28 `infinite-race-u17` | TIMEOUT | RB s1: 2~<br>RF s1: 2,5!<br>US s1: 2:3:11,14?<br>UF s1: 2:3:53,56~ | US P 0.083718 ms | RF S1<br>US S4/V4/A4<br>UF S18 | US P K2→K11: G25→15882,Σ479→281657,H25→25,L12→617 | US V K2 80.41→80.85; V K11 560.23→560.23 | 688.90 | K-search | cap:<br>RB search s1/K2 ≥57.318 s<br>US search s1/K14 ≥22.517 s<br>UF search s1/K56 ≥4.433 s |
| 29 `infinite-race-u12` | UNREALIZABLE | RB s1: 2~<br>RF s1: 2,5!<br>US s1: 2:3:14<br>UF s1: 2:3:53,56~ | US P 0.068704 ms | RF S1<br>US S5/V5/A5<br>UF S18 | US P K2→K14: G25→5730,Σ449→99228,H23→23,L12→310 | US V K2 70.56→70.81; V K14 461.65→461.65 | 471.86 | solved | winner cancellation:<br>RB search s1/K2 ≥23.360 s<br>UF search s1/K56 ≥0.596 s |

### Observation PMO251 results

Source: `_bm-logs.i251/observation-60s/analysis.json`, `per-job.tsv`, and raw job tables.

| Job / input | Outcome | K sequence by worker | First useful query | Completed stages | Rank/support samples | Boundary scope MiB | Scope peak MiB | Current obstruction | Censoring (lower bounds) |
|---|---|---|---|---|---|---|---:|---|---|
| 00 `robot_grid_pb_8_8_pe_` | TIMEOUT | RB s1: 2~<br>RF s1: 2! | RB D 13.170 s (0.174 ms)<br>RF D 12.823 s (0.358 ms) | none | RB D s1/K2: σ16578→16569,h1→1,n≥131072<br>RF D s1/K2: σ1→1,h0→0,n≥32768 | unknown (no checker entered) | 397.16 | K-search | cap:<br>RB search s1/K2 ≥46.925 s |
| 01 `workstation_resupply_pb_3_pe_` | REALIZABLE | RB s1: 2:3:8,11~<br>RF s1: 2:3:20<br>US s1: 2:3:32,35?<br>UF s1: 2,5,8! | RB D 0.300 s (0.002 ms)<br>RF D 0.297 s (0.010 ms)<br>US P 0.063450 ms<br>UF D 0.250 s (0.010 ms) | RB S3<br>RF S7/V1/C1<br>US S11/V11/A11<br>UF S2 | RB D s1/K2: σ169→169,h1→1,n≥1024; s1/K11: σ169→117,h10→10,n≥4194304<br>RF D s1/K2: σ1→15,h0→1,n≥4096; s1/K20: σ1→8,h0→16,n≥4194304<br>US P K2→K32: G657→3144,Σ3918→18510,H6→6,L647→684<br>UF D s1/K2: σ1→14,h0→1,n≥65536; s1/K8: σ1→14,h0→7,n≥262144 | RF V K20 190.74→190.74; C K20 190.74→190.74<br>US V K2 117.76→118.75; V K32 187.52→187.77 | 209.87 | solved | winner cancellation:<br>RB search s1/K11 ≥10.730 s<br>US search s1/K35 ≥0.656 s<br>N native preparation ≥12.928 s |
| 02 `evasion-real` | TIMEOUT | RB s1: 2~<br>RF s1: 2!<br>US s1: 2:3:29,32?<br>UF s1: 2:3:11,14~ | RB D 22.492 s (0.051 ms)<br>RF D 22.620 s (0.073 ms)<br>US P 0.076882 ms<br>UF D 0.437 s (0.018 ms) | US S10/V10/A10<br>UF S4 | RB D s1/K2: σ12145→11705,h1→1,n≥131072<br>RF D s1/K2: σ1→24,h0→0,n≥131072<br>US P K2→K29: G3→32058,Σ78→535970,H76→80,L3→169<br>UF D s1/K2: σ1→34,h0→1,n≥16384; s1/K14: σ1→15,h0→10,n≥67108864 | US V K2 63.07→65.97; V K29 1025.47→999.15 | 1033.36 | K-search | cap:<br>RB search s1/K2 ≥37.605 s<br>US search s1/K32 ≥4.590 s<br>UF search s1/K14 ≥35.968 s |
| 03 `robot_grid_pb_7_7_pe_` | TIMEOUT | RB s1: 2~<br>RF s1: 2! | RB D 8.107 s (0.097 ms)<br>RF D 7.936 s (0.261 ms) | none | RB D s1/K2: σ9753→9742,h1→1,n≥131072<br>RF D s1/K2: σ1→1,h0→0,n≥16384 | unknown (no checker entered) | 1229.31 | translation + K-search | cap:<br>RB search s1/K2 ≥51.979 s<br>US translation ≥60.012 s<br>UF translation ≥60.021 s |
| 04 `collector_v1_pb_11_pe_` | TIMEOUT | RB s1: 2~<br>RF s1: 2!<br>US s1: 2? | RB D 14.260 s (0.077 ms)<br>RF D 11.531 s (0.170 ms) | none | RB D s1/K2: σ2061→14,h1→0,n≥2097152<br>RF D s1/K2: σ1→12,h0→0,n≥4096 | unknown (no checker entered) | 2462.52 | K-search | cap:<br>RB search s1/K2 ≥45.800 s<br>US search s1/K2 ≥12.558 s |
| 05 `robot-to-target0` | TIMEOUT | RB s1: 2~<br>RF s1: 2,5! | RB D 19.833 s (0.044 ms)<br>RF D 19.931 s (0.074 ms) | RF S1 | RB D s1/K2: σ15231→15215,h1→1,n≥65536<br>RF D s1/K2: σ1→36,h0→1,n≥131072; s1/K5: σ1→37,h0→4,n≥262144 | unknown (no checker entered) | 1024.39 | translation + construction + K-search | cap:<br>RB search s1/K2 ≥40.267 s<br>US translation ≥59.826 s<br>UF IO preparation ≥48.793 s |
| 06 `Morning2s_d6c5ac79` | TIMEOUT | US s1: 2? | unknown | none | unknown | unknown (no checker entered) | 2163.19 | translation + construction + K-search | cap:<br>RB translation ≥60.055 s<br>RF translation ≥60.055 s<br>US search s1/K2 ≥50.048 s<br>UF IO decoding ≥18.953 s<br>N native preparation ≥60.006 s |
| 07 `lift_gr1+_pb_7_pe_` | TIMEOUT | US s1: 2,5?<br>UF s1: 2,5! | US P 0.098140 ms<br>UF D 0.620 s (0.020 ms) | US S1/V1/A1<br>UF S1 | US P K2→K2: G3366→3366,Σ31562→31562,H18→18,L627→627<br>UF D s1/K2: σ1→13,h0→1,n≥33554432; s1/K5: σ1→17,h0→4,n≥16777216 | US V K2 1055.60→1060.60 | 4696.98 | construction + K-search | cap:<br>RB IO preparation ≥13.910 s<br>RF IO preparation ≥14.556 s<br>US search s1/K5 ≥54.674 s |
| 08 `arbiter_pb_10_pe_` | TIMEOUT | RB s1: 2~<br>RF s1: 2! | RB D 29.132 s (0.395 ms)<br>RF D 26.881 s (0.570 ms) | none | RB D s1/K2: σ43→42,h1→1,n≥2097152<br>RF D s1/K2: σ1→1,h0→0,n≥524288 | unknown (no checker entered) | 1743.26 | translation + K-search | cap:<br>RB search s1/K2 ≥30.994 s<br>US translation ≥60.012 s<br>UF translation ≥59.980 s<br>N native direct ≥59.566 s |
| 09 `round_robin_arbiter_unreal2_pb_6_pe_` | UNREALIZABLE | — | unknown | N T1 | unknown | N T (K absent) 1169.77→1024.27 | 1888.35 | solved | winner cancellation:<br>RB Booleanization ≥1.915 s<br>RF Booleanization ≥1.805 s<br>US translation ≥26.755 s<br>UF translation ≥26.745 s |
| 10 `collector_v1_pb_15_pe_` | TIMEOUT | — | unknown | none | unknown | unknown (no checker entered) | 4939.68 | translation + construction | cap:<br>RB preprocessing ≥3.711 s<br>RF preprocessing ≥3.264 s<br>US translation ≥59.987 s<br>UF translation ≥59.990 s |
| 11 `collector_v1_pb_13_pe_` | TIMEOUT | — | unknown | none | unknown | unknown (no checker entered) | 4645.57 | translation + construction | cap:<br>RB IO preparation ≥50.722 s<br>RF IO preparation ≥53.753 s<br>US translation ≥60.014 s |
| 12 `collector_v1_pb_12_pe_` | TIMEOUT | — | unknown | none | unknown | unknown (no checker entered) | 4073.19 | construction | cap:<br>RB IO decoding ≥3.075 s<br>RF IO decoding ≥5.830 s<br>US Booleanization ≥36.198 s<br>N native preparation ≥60.171 s |
| 13 `lift_pb_6_pe_` | TIMEOUT | RB s1: 2~<br>RF s1: 2,5!<br>US s1: 2:3:8,11?<br>UF s1: 2,5,8~ | RB D 4.757 s (0.008 ms)<br>RF D 4.632 s (0.028 ms)<br>US P 0.235291 ms<br>UF D 2.589 s (0.025 ms) | RF S1<br>US S3/V3/A3<br>UF S2 | RB D s1/K2: σ3045→2437,h1→1,n≥1048576<br>RF D s1/K2: σ1→1,h0→1,n≥16384; s1/K5: σ1→3,h0→3,n≥262144<br>US P K2→K8: G3→7684,Σ4→40818,H2→8,L3→1362<br>UF D s1/K2: σ1→9,h0→1,n≥131072; s1/K8: σ1→12,h0→3,n≥1048576 | US V K2 242.84→243.06; V K8 659.66→659.91 | 747.70 | K-search | cap:<br>RB search s1/K2 ≥55.276 s<br>US search s1/K11 ≥23.340 s<br>UF search s1/K8 ≥31.176 s<br>N native direct ≥59.570 s |
| 14 `thermostat-GF-unreal1` | UNREALIZABLE | US s1: 2,5<br>UF s1: 2,5,8~ | US P 0.078630 ms<br>UF D 15.803 s (0.019 ms) | US S2/V2/A2<br>UF S2 | US P K2→K5: G12→668,Σ327→21432,H61→64,L9→166<br>UF D s1/K2: σ1→41,h0→1,n≥1048576; s1/K8: σ1→19,h0→2,n≥4194304 | US V K2 1280.43→1280.70; V K5 1312.20→1312.45 | 1313.08 | solved | winner cancellation:<br>RB translation ≥27.863 s<br>RF translation ≥27.863 s<br>UF search s1/K8 ≥3.752 s |
| 15 `reversible-lane-r-real` | TIMEOUT | RB s1: 2~<br>RF s1: 2!<br>US s1: 2:3:11,14?<br>UF s1: 2,5,8~ | RB D 46.649 s (0.036 ms)<br>RF D 46.493 s (0.077 ms)<br>US P 0.057095 ms<br>UF D 0.604 s (0.021 ms) | US S4/V4/A4<br>UF S2 | RB D s1/K2: σ8293→8277,h1→1,n≥65536<br>RF D s1/K2: σ1→26,h0→1,n≥262144<br>US P K2→K11: G4→29143,Σ22→500270,H10→22,L4→337<br>UF D s1/K2: σ1→22,h0→1,n≥65536; s1/K8: σ1→18,h0→3,n≥134217728 | US V K2 59.12→59.33; V K11 464.55→464.55 | 832.85 | K-search | cap:<br>RB search s1/K2 ≥13.440 s<br>US search s1/K14 ≥30.145 s<br>UF search s1/K8 ≥46.311 s |
| 16 `Morning_88c5c1c3` | TIMEOUT | US s1: 2,5,8?<br>UF s1: 2,5,8! | US P 0.089826 ms<br>UF D 0.778 s (0.050 ms) | US S2/V2/A2<br>UF S2 | US P K2→K5: G27→29782,Σ258→371908,H13→17,L18→235<br>UF D s1/K2: σ1→1,h0→0,n≥8192; s1/K8: σ1→12,h0→7,n≥16777216 | US V K2 163.50→195.92; V K5 1658.97→1658.97 | 3102.41 | translation + construction + K-search | cap:<br>RB translation ≥60.006 s<br>RF translation ≥60.004 s<br>US search s1/K8 ≥40.224 s<br>N native preparation ≥60.012 s |
| 17 `simple_arbiter_enc_pb_32_pe_` | TIMEOUT | RB s1: 2,5~<br>RF s1: 2,5! | RB D 0.120 s (0.004 ms)<br>RF D 0.117 s (0.011 ms) | RB S1<br>RF S1 | RB D s1/K2: σ65→33,h1→1,n≥65536; s1/K5: σ65→65,h4→4,n≥32768<br>RF D s1/K2: σ1→3,h0→1,n≥16384; s1/K5: σ1→4,h0→2,n≥16384 | unknown (no checker entered) | 1399.41 | translation + K-search | cap:<br>RB search s1/K5 ≥59.789 s<br>US translation ≥59.984 s<br>UF translation ≥59.969 s |
| 18 `round_robin_arbiter_pb_8_pe_` | REALIZABLE | US s1: 2? | unknown | N T1 | unknown | N T (K absent) 911.69→833.91 | 1268.91 | solved | winner cancellation:<br>RB translation ≥31.568 s<br>RF translation ≥31.572 s<br>US search s1/K2 ≥29.662 s |
| 19 `F-G-contradiction-110` | UNREALIZABLE | RB s1: 2~<br>RF s1: 2,5~<br>US s1: 2:3:20<br>UF s1: 2,5,8~ | RB D 5.729 s (0.027 ms)<br>RF D 5.768 s (0.044 ms)<br>US P 0.072840 ms<br>UF D 5.025 s (0.013 ms) | RF S1<br>US S7/V7/A7<br>UF S2 | RB D s1/K2: σ9223→9223,h1→1,n≥32768<br>RF D s1/K2: σ1→24,h0→1,n≥262144; s1/K5: σ1→24,h0→0,n≥8192<br>US P K2→K20: G8→738,Σ89→8255,H17→17,L8→55<br>UF D s1/K2: σ1→18,h0→1,n≥262144; s1/K8: σ1→11,h0→3,n≥1048576 | US V K2 89.85→89.96; V K20 705.78→706.03 | 733.25 | solved | winner cancellation:<br>RB search s1/K2 ≥12.235 s<br>RF search s1/K5 ≥0.336 s<br>UF search s1/K8 ≥0.457 s |
| 20 `infinite-race-u13` | UNREALIZABLE | RB s1: 2~<br>RF s1: 2,5!<br>US s1: 2:3:14<br>UF s1: 2:3:50,53~ | RB D 2.081 s (0.022 ms)<br>RF D 2.105 s (0.040 ms)<br>US P 0.069945 ms<br>UF D 0.488 s (0.013 ms) | RF S1<br>US S5/V5/A5<br>UF S17 | RB D s1/K2: σ6010→5814,h1→1,n≥65536<br>RF D s1/K2: σ1→40,h0→1,n≥131072; s1/K5: σ1→64,h0→4,n≥262144<br>US P K2→K14: G25→7333,Σ449→127086,H23→23,L12→348<br>UF D s1/K2: σ1→17,h0→1,n≥32768; s1/K53: σ1→10,h0→49,n≥2097152 | US V K2 71.16→71.64; V K14 424.64→424.89 | 482.15 | solved | winner cancellation:<br>RB search s1/K2 ≥22.043 s<br>UF search s1/K53 ≥1.253 s |
| 21 `thermostat-F-real` | TIMEOUT | RB s1: 2:3:98,99<br>RF s1: 2:3:20,23!<br>US s1: 2:3:32,35?<br>UF s1: 2:3:17,20~ | RB D 1.408 s (0.015 ms)<br>RF D 1.421 s (0.017 ms)<br>US P 0.066359 ms<br>UF D 0.309 s (0.011 ms) | RB S34<br>RF S7<br>US S11/V11/A11<br>UF S6 | RB D s1/K2: σ1569→1035,h1→1,n≥1024; s1/K99: σ1569→1157,h98→98,n≥512<br>RF D s1/K2: σ1→32,h0→0,n≥1024; s1/K23: σ1→55,h0→8,n≥262144<br>US P K2→K32: G18→33660,Σ259→586288,H19→20,L10→165<br>UF D s1/K2: σ1→16,h0→1,n≥65536; s1/K20: σ1→16,h0→6,n≥33554432 | US V K2 55.20→55.87; V K32 389.70→389.70 | 391.73 | K-search | cap:<br>US search s1/K35 ≥6.510 s<br>UF search s1/K20 ≥14.220 s |
| 22 `helipad-real` | TIMEOUT | US s1: 2:3:8,11?<br>UF s1: 2,5,8~ | US P 0.087803 ms<br>UF D 13.403 s (0.021 ms) | US S3/V3/A3<br>UF S2 | US P K2→K8: G4→3309,Σ38→65008,H18→28,L4→787<br>UF D s1/K2: σ1→1,h0→0,n≥262144; s1/K8: σ1→18,h0→2,n≥67108864 | US V K2 883.28→883.60; V K8 977.25→977.25 | 1091.23 | translation + K-search | cap:<br>RB translation ≥59.947 s<br>RF translation ≥59.947 s<br>US search s1/K11 ≥14.301 s<br>UF search s1/K8 ≥39.457 s |
| 23 `generalized_buffer_pb_6_pe_` | TIMEOUT | RB s1: 2<br>RB s2: 2~<br>RF s1: 2<br>RF s2: 2!<br>US s1: 2:3:98,99<br>UF s1: 2:3:98,99 | RB D 0.078 s (0.001 ms)<br>RF D 0.078 s (0.006 ms)<br>US P 0.048044 ms<br>UF D 0.081 s (0.005 ms) | RB S1<br>RF S1/V1/C1<br>US S34/V34/A34<br>UF S34 | RB D s1/K2: σ19→12,h1→0,n≥128; s2/K2: σ109→67,h1→1,n≥2097152<br>RF D s1/K2: σ1→3,h0→1,n≥64; s2/K2: σ1→18,h0→0,n≥8388608<br>US P K2→K99: G7→14696,Σ24→157868,H9→20,L7→102<br>UF D s1/K2: σ1→3,h0→0,n≥32; s1/K99: σ1→17,h0→94,n≥16384 | RF C/V K2 unknown (no bracket)<br>US V K2 unknown (no bracket); V K5 55.05→56.79; V K99 493.27→493.27 | 719.33 | translation + K-search | cap:<br>RB search s2/K2 ≥59.775 s<br>US translation ≥14.477 s<br>UF translation ≥59.591 s |
| 24 `items_processing-real` | TIMEOUT | — | unknown | none | unknown | unknown (no checker entered) | 1129.98 | translation + construction | cap:<br>RB translation ≥59.950 s<br>RF translation ≥59.949 s<br>US translation ≥59.955 s<br>UF IO preparation ≥7.485 s |
| 25 `thermostat-GF-real` | TIMEOUT | RB s1: 2~<br>RF s1: 2!<br>US s1: 2:3:11,14?<br>UF s1: 2:3:35,38~ | RB D 23.771 s (0.028 ms)<br>RF D 23.769 s (0.080 ms)<br>US P 0.074109 ms<br>UF D 1.850 s (0.017 ms) | US S4/V4/A4<br>UF S12 | RB D s1/K2: σ26242→26146,h1→1,n≥131072<br>RF D s1/K2: σ1→55,h0→0,n≥262144<br>US P K2→K11: G12→13447,Σ269→278366,H50→50,L9→614<br>UF D s1/K2: σ1→18,h0→1,n≥131072; s1/K38: σ1→1,h0→26,n≥1048576 | US V K2 130.48→131.39; V K11 1081.43→1081.43 | 1436.62 | K-search | cap:<br>RB search s1/K2 ≥36.288 s<br>US search s1/K14 ≥17.867 s<br>UF search s1/K38 ≥0.411 s |
| 26 `lift_gr1+_pb_6_pe_` | TIMEOUT | RB s1: 2~<br>RF s1: 2,5!<br>US s1: 2,5,8?<br>UF s1: 2,5! | RB D 17.262 s (0.034 ms)<br>RF D 16.999 s (0.047 ms)<br>US P 0.100927 ms<br>UF D 0.270 s (0.013 ms) | RF S1<br>US S2/V2/A2<br>UF S1 | RB D s1/K2: σ4902→3972,h1→1,n≥524288<br>RF D s1/K2: σ1→1,h0→0,n≥32768; s1/K5: σ1→1,h0→3,n≥262144<br>US P K2→K5: G1586→16547,Σ13682→148167,H16→16,L410→500<br>UF D s1/K2: σ1→16,h0→1,n≥4194304; s1/K5: σ1→14,h0→4,n≥8388608 | US V K2 183.34→183.59; V K5 1796.45→1796.95 | 1954.50 | construction + K-search | cap:<br>RB search s1/K2 ≥42.774 s<br>US search s1/K8 ≥36.327 s<br>N native preparation ≥59.963 s |
| 27 `lift_pb_8_pe_` | TIMEOUT | — | unknown | none | unknown | unknown (no checker entered) | 1032.83 | translation + construction + K-search | cap:<br>RB Booleanization ≥13.917 s<br>RF Booleanization ≥14.256 s<br>US translation ≥60.004 s<br>N native direct ≥59.310 s |
| 28 `infinite-race-u17` | TIMEOUT | RB s1: 2~<br>RF s1: 2,5!<br>US s1: 2:3:11,14?<br>UF s1: 2:3:50,53~ | RB D 3.110 s (0.024 ms)<br>RF D 3.099 s (0.060 ms)<br>US P 0.073637 ms<br>UF D 1.118 s (0.013 ms) | RF S1<br>US S4/V4/A4<br>UF S17 | RB D s1/K2: σ7602→7423,h1→1,n≥65536<br>RF D s1/K2: σ1→43,h0→1,n≥262144; s1/K5: σ1→37,h0→4,n≥262144<br>US P K2→K11: G25→15882,Σ479→281657,H25→25,L12→617<br>UF D s1/K2: σ1→18,h0→1,n≥65536; s1/K53: σ1→11,h0→40,n≥8388608 | US V K2 80.41→81.17; V K11 567.22→567.22 | 646.03 | K-search | cap:<br>RB search s1/K2 ≥56.933 s<br>US search s1/K14 ≥17.445 s<br>UF search s1/K53 ≥5.076 s |
| 29 `infinite-race-u12` | UNREALIZABLE | RB s1: 2~<br>RF s1: 2,5!<br>US s1: 2:3:14<br>UF s1: 2:3:53,56~ | RB D 1.819 s (0.020 ms)<br>RF D 1.814 s (0.032 ms)<br>US P 0.065344 ms<br>UF D 0.447 s (0.014 ms) | RF S1<br>US S5/V5/A5<br>UF S18 | RB D s1/K2: σ5612→5451,h1→1,n≥65536<br>RF D s1/K2: σ1→38,h0→1,n≥131072; s1/K5: σ1→24,h0→3,n≥262144<br>US P K2→K14: G25→5730,Σ449→99228,H23→23,L12→310<br>UF D s1/K2: σ1→17,h0→1,n≥32768; s1/K56: σ1→11,h0→11,n≥1048576 | US V K2 70.81→71.06; V K14 460.82→461.07 | 474.05 | solved | winner cancellation:<br>RB search s1/K2 ≥21.557 s<br>UF search s1/K56 ≥0.452 s |


### What moved since #241, and what is still unfinished

The earlier [#241 issue-sweep result](RESULTS.md#post-campaign-issue-sweep-2026-10-1011)
used the measured compact binary **LEGI208**, SHA-256
`ac1ff537146bf551dfb81d0f7e2ff5a29079148a0dbe8ead6ab3557df7676cbc`
([baseline registry](baselines.tsv), archive `binary-LEGI208-ac1ff537`). Its largest
robot-grid invocation moved from an 8 GiB MEMOUT to a TIMEOUT at approximately
395 MiB; that historical number is not a PMC4 or PMO251 measurement. The selected
panel contains one historical MEMOUT, eight construction-to-search transitions and
one paired nonsolve-to-solve preservation control (selection flags overlap).
SELECTION.md (`_bm-logs.i251/panel/SELECTION.md` in archive `memory-search20261013`) and `panel.json` bind those historical
rows. The present panels add attribution after that transition; they are not another
paired dense-versus-compact representation screen.

For `robot_grid_pb_8_8` (job 00), **PMC4** finishes both REAL construction routes and
enters K=2; RB remains in search for at least **47.460 s**, while RF stops at
`RESOURCE_LIMIT`. The invocation remains TIMEOUT, with **423,243,776 bytes
(403.64 MiB)** scope peak and no checker entered. The action-table payload estimate
is **14,101,784 bytes per REAL worker**, not scope memory. **PMO251** records RB's
first useful dense application at **13.170380 s** after launch (**0.173906 ms** into
search) and RF's at **12.823224 s** (**0.358269 ms** into search). RB's sampled K=2
source-rank support is **16,578→16,569** (sample range **16,566–16,578**) and sampled
maximum remains **1**, at a last sampled count of **131,072** applications. PMO251's
RB search is censored at **46.925 s**, its scope peak is **416,448,512 bytes
(397.16 MiB)**, and it enters no checker. Construction memory is no longer this
invocation's cap obstruction; low sampled rank maximum does not prove search progress
is exhausted or that more K would help.

For `robot_grid_pb_7_7` (job 03), **PMC4** and **PMO251** likewise enter both REAL
K=2 searches, with RF stopping at `RESOURCE_LIMIT` and no completed REAL search or
checker. PMC4 RB is censored at **51.927 s**; both UNREAL workers remain in
translation for approximately **60 s**, so the invocation is mixed translation/search.
PMO251's RB/RF first applications are **8.106797 / 7.935514 s** after launch
(**0.096740 / 0.261398 ms** into their searches). PMO251 RB samples support
**9,753→9,742** (range **9,741–9,753**), maximum **1→1**, through at least **131,072**
applications. Its **1,229.31 MiB** scope peak cannot be interpreted as search-only
memory while the UNREAL translators are alive; PMC4's scope peak is **1,235.40 MiB**.
The former construction transition is confirmed, but neither binary provides a
search-to-verification boundary for these two robot jobs.

Among the other six selected construction-to-search transitions, `evasion-real`
(job 02) and `collector_v1_pb_11` (04) are now search-limited. `robot-to-target0`
(05) combines RB search, US translation and UF IO preparation. `Morning2s` (06)
combines REAL translation, US K=2 search, UF IO decoding and native preparation.
`lift_gr1+_pb_7` (07) combines REAL IO preparation and US K=5 search.
`arbiter_pb_10` (08) combines RB K=2/native direct search with UNREAL translation.
These descriptions hold separately in both treatments; reaching search in one
worker does not move every worker past construction. The remaining selected jobs'
actual surviving stages and bounds appear in each treatment's last two table columns.

`collector_v1_pb_12` (12) is the only exclusively construction-limited invocation:
REAL IO decoding, US Booleanization and native preparation all remain unfinished.
`collector_v1_pb_15` (10), `collector_v1_pb_13` (11), `items_processing-real` (24),
and `lift_pb_8` (27) have no entered legacy K attempt in either panel. Their mixed
obstructions include translation and construction; `lift_pb_8` additionally has a
native direct search with no K identity. Raising legacy K cannot reach these stages.
`Morning_88c5c1c3` (16), `simple_arbiter_enc_pb_32` (17), `helipad-real` (22),
`generalized_buffer` (23) and `lift_gr1+_pb_6` (26) also remain mixed, as the tables
show. The eight exclusively search-limited jobs are 00, 02, 04, 13, 15, 21, 25 and 28.

Verification is entered and completed on many sparse K attempts without a verdict.
**PMC4** `helipad-real` (22) completes US searches through K=11, but its K=11
verification is censored at only **0.070 s**; RB/RF are still translating and UF
searching. Its US boundary scope samples grow from **883.45→883.76 MiB** at K=2 to
**1,094.74→1,094.74 MiB** at K=11, while US game states grow **4→10,265** and support
sum **38→204,348** across completed searches. **PMO251** instead has US K=11 search
censored for **14.301 s**, completed checks only through K=8, last bracket
**977.25→977.25 MiB**, states **4→3,309**, and support sum **38→65,008**. This is a
change in what was observed by the cap, not evidence of a stable verification
bottleneck or a performance regression attributable to the marker.

Native mapping now identifies work that was previously unknown. In both binaries,
`arbiter_pb_10`, `lift_pb_6` and `lift_pb_8` have surviving native **direct K-search**,
with respective PMC4 lower bounds **59.535 / 59.621 / 59.292 s** and PMO251 bounds
**59.566 / 59.570 / 59.310 s**; none has an explicit completed native search or checker.
`Morning2s`, `collector_v1_pb_12`, `Morning_88c5c1c3` and `lift_gr1+_pb_6` have
surviving native **construction/preparation**. Earlier native applicability/resource
or allocation declines do not vote as surviving obstructions. Native target_check
explicitly completes for jobs 09 and 18, which return UNREALIZABLE and REALIZABLE.
Their checker-entry scope brackets are **1,060.23→1,009.74 / 925.28→844.76 MiB**
(PMC4) and **1,169.77→1,024.27 / 911.69→833.91 MiB** (PMO251). Enclosing native
preparation/direct intervals still lack completions; neither summary timing nor a
verified target check supplies those missing endpoints.

The other five solved jobs are `workstation_resupply_pb_3` (01, REALIZABLE) and
`thermostat-GF-unreal1` (14), `F-G-contradiction-110` (19), `infinite-race-u13` (20),
`infinite-race-u12` (29), all UNREALIZABLE. Job 01 preserves the #241 paired gain:
RF completes through K=20 and its verification/certificate check, in **13.006 s**
with **189.30→189.30 MiB** boundary scope memory (PMC4), versus **13.038 s** and
**190.74→190.74 MiB** (PMO251). Those individual elapsed times are descriptive,
not a matched repetition estimate. Remaining live competitors are winner-cancelled; already exited declines are not.

The refreshed #235 P0 table (`_bm-logs.i251/p0-reclassification.tsv` in archive `memory-search20261013`) reclassifies
its original **317** 17 s nonsolves, from frozen **LEGFINAL**, SHA-256
`2e0deb94b4c35de838ba053e9a83b8bea07f57b5af81ed706e96cf4dd5f05896`
(archive `legacy20261008-closing`): **87 translation, 20 search, 206 mixed,
1 memory and 3 unknown**, compared with the issue's previously reported **132 unknown**.
This is an offline remapping of those original invocations, not 129 newly solved
inputs or new PMC4/PMO251 results. It attributes native stages while preserving
missing endpoints. The three remaining unknown entries are retained as unknown;
the 30-job panels do not resolve missing evidence for the rest of that corpus.

The different treatment records also show cap sensitivity within K-search. For
`thermostat-GF-real` (25), UF completes K=32 and is censored at K=35 in **PMC4**,
while **PMO251** completes K=35 and enters K=38. For `infinite-race-u17` (28), UF
completes K=53 and enters K=56 in **PMC4**, versus completing K=50 and entering K=53
in **PMO251**. Both invocations remain TIMEOUT in both treatments. These opposing
stage differences and the `helipad-real` boundary difference warrant keeping the
records separate; a single run per binary does not isolate instrumentation overhead.

### Evidence-based recommendation

**Leave K, row limits and memo budgets unchanged.** The two panels support attributing
where workers stop and preserve the seven observed solves. They do not pair a
parameter change, establish a rank-storage ceiling, or show that checker memory is
the binding limit. Every explicit stopped legacy search reports `RESOURCE_LIMIT`;
that label alone does not identify a row limit or memo budget to raise. Twenty such
stopped searches occur in each treatment, while other arms continue. Several workers
never reach a legacy K attempt, and `thermostat-F-real`/`generalized_buffer` already
complete first-subjob searches through the recorded K=99 bound without a verdict.
No automatic resource increase or solver-default change is supported.

One supported follow-up is **CPU attribution during K=2 search**, a measurable experiment owned
by the driver. Its evidence targets are jobs 00/03's RB subjob 1 K=2 and job 04's
RB/US subjob 1 K=2: construction finishes, useful dense applications are observed
where supported, and the searches remain censored. Keep the same K/row/memo settings,
60 s cap, scope limits, source bytes and portfolio. Use a separately hashed observation/profiling binary and three
serial, cooled, order-reversed paired repetitions against frozen PMC4. Attribute CPU
samples only inside those recorded search intervals, retain first-query latency,
application/support samples, completed stages, explicit stop reasons and scope memory,
and report the observer/profiler overhead separately. The measurable question is
which operations account for CPU time after the first useful query until the cap,
and whether their shares repeat across the jobs and repetitions. PMC4 lacks dense
first-query times, so compare profile shares over recorded search intervals and use
the query-relative window only for the observation binary. A consistent dominant
operation can justify a generic implementation experiment; inconsistent shares
support no such experiment. Admission would still require paired coverage/runtime
evidence; unfinished searches establish no new solve. This proposal does not require a higher K or budget and is not
an experiment already performed here. No K/row/memo tuning experiment, verifier-budget
increase, or scheduler change is supported by the present evidence.

### Archived evidence

Both panels, the panel definition and selection, cooldown and attempt records, phase
records, captures, scope-memory samples and the per-job tables are archived as
`memory-search20261013`; the PMO251 observation executable is archived as
`binary-PMO251-1746c36a` (see [evidence-index.tsv](evidence-index.tsv) and
[baselines.tsv](baselines.tsv)). Restore with
`python3 -s scripts/acacia-evidence.py fetch --campaign memory-search20261013 --dest DIR`.
