# Compact action payloads (#208, #200)

Development branch `perf/action-payload-layout`, based on
`fd6634f45600cf874130e7eea7c3c2ab0f86147e`; changes remain uncommitted.
The owner requested measurements before representation changes and reserves the
admission campaigns for the driver. These are single-instance observations, not
an admission screen.

## Selection and measurement

Six inputs were selected from the closing candidate-60s rows and their phase
records: both standalone REAL arms had at least 10 seconds in search. Among
14 eligible inputs, farthest-first sampling uses normalized log2(1+x) state,
edge, action-payload and endpoint counts, starting with the longest combined
search observation. Names occur only in evaluation metadata and this report.
The selection, including original record paths, is in
`build_scratch/i208/_bm-logs.measurements/selection.tsv`.

Each observation ran alone in a fresh 8 GiB, zero-swap workload cgroup, using
the existing scope runner. Panel runs use `perf record -e cycles:u -F 199`,
DWARF stacks and the monotonic clock; the two memory cases omit perf.
The same caps and profiler settings are used for both representations.
No CPU quota or explicit affinity was set. These diagnostics were not rotated
or thermally adjudicated; those controls belong to the driver admission screen.
The table estimate includes object sizes and allocated capacity, excluding
allocator/list-node/BDD overhead. A default row is an empty endpoint sequence;
density is nonempty destination rows divided by actions times states.
Scope peaks are drained cgroup `memory.peak`, including perf where present.

The predecessor share attributes weighted samples at the sampled instruction,
including inline ancestry, rather than attributing all physical callers.
Buffer reset and rank-copy construction are excluded. Search intervals include
completion, cancellation and explicit stopped-stage records. Forward arms have
no predecessor; their endpoint-loop share is recorded separately.

## Measured results

All MB/GB below are decimal; scope limits are binary GiB. The structural counts
are for complete tables. R uses the completed compact table because the dense
run could not finish construction. Every other completed dense/compact pair has
identical states, actions, logical/nonempty rows and endpoints.

| ID | Input | States | Actions | Logical rows | Nonempty rows | Endpoints | Density |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 00 | `round_robin_arbiter_unreal1_pb_3_9_pe_` | 755 | 64 | 48,320 | 1,694 | 14,444 | 3.5058% |
| 01 | `amba_decomposed_arbiter_pb_9_pe_` | 49 | 538,112 | 26,367,488 | 2,159,617 | 9,371,137 | 8.1905% |
| 02 | `reversible-lane-r-real` | 8,293 | 2,336 | 19,372,448 | 146,035 | 2,401,620 | 0.7538% |
| 03 | `workstation_resupply_pb_4_pe_` | 366 | 9,236 | 3,380,376 | 279,917 | 1,430,209 | 8.2806% |
| 04 | `amba_case_study_unreal_pb_2_pe_` | 35 | 10,368 | 362,880 | 102,624 | 240,618 | 28.2804% |
| 05 | `infinite-race-u16` | 7,204 | 615 | 4,430,460 | 20,484 | 187,114 | 0.4623% |
| R | `robot_grid_pb_8_8_pe_` | 16,578 | 66,216 | 1,097,728,848 | 314,128 | 853,216 | 0.0286% |
| M | `Morning2s_d6c5ac79` | 348 | 443,233 | 154,245,084 | 24,499,105 | 165,591,457 | 15.8832% |

The backward predecessor loop varies strongly: 0.57–18.29% of all worker cycles,
0.59–18.47% during search. Five of six are below 7% during search. Forward search
endpoint work ranges from 1.42% to 16.32%. Sparsity supports compaction without
claiming that the old one-profile 21% predecessor share generalizes.

| ID | Backward predecessor, all | Backward predecessor, search | Forward endpoint loop, all | Forward endpoint loop, search | Search samples B / F |
| --- | --- | --- | --- | --- | --- |
| 00 | 0.74% | 0.74% | 1.42% | 1.42% | 5,952 / 5,950 |
| 01 | 4.62% | 6.98% | 9.96% | 13.20% | 7,850 / 7,419 |
| 02 | 0.70% | 3.35% | 4.11% | 12.76% | 2,565 / 3,587 |
| 03 | 1.79% | 1.86% | 15.46% | 16.12% | 11,499 / 11,435 |
| 04 | 18.29% | 18.47% | 16.26% | 16.32% | 2,652 / 8,649 |
| 05 | 0.57% | 0.59% | 12.33% | 13.57% | 11,403 / 5,146 |

The six panel payloads shrink by 65.4–98.3%; M shrinks by 68.4%.
Bytes/state sums all retained actions for one state; bytes/row divides by the
logical state × action slots. Neither includes allocator/list-node/BDD overhead.

| ID | Table MB: dense → compact | Bytes/logical row: dense → compact | Bytes/state: dense → compact |
| --- | --- | --- | --- |
| 00 | 1.286 → 0.145 | 26.6058 → 3.0005 | 1,702.8 → 192.0 |
| 01 | 740.262 → 126.775 | 28.0748 → 4.8080 | 15,107,386.1 → 2,587,251.9 |
| 02 | 487.594 → 21.643 | 25.1695 → 1.1172 | 58,795.9 → 2,609.8 |
| 03 | 95.945 → 16.216 | 28.3830 → 4.7972 | 262,145.1 → 44,307.2 |
| 04 | 11.281 → 3.900 | 31.0875 → 10.7486 | 322,315.2 → 111,441.6 |
| 05 | 107.902 → 1.854 | 24.3547 → 0.4185 | 14,978.1 → 257.4 |
| R | 4950.525† → 14.102 | 24.0087† → 0.0128 | 298,620.1† → 850.6 |
| M | 5477.126 → 1730.902 | 35.5092 → 11.2218 | 15,738,867.8 → 4,973,856.3 |

† R's dense values are the last reported retained prefix before OOM:
12,438 actions, 206,197,164 logical rows, 59,014 nonempty rows and 160,278 endpoints;
density 0.028620%. They are not paired whole-table values. Its completed compact
table has 66,216 actions and density 0.028616%. Inference: the completed action
count would require at least 26.35 GB of dense row-vector descriptors alone
(24 bytes × 1,097,728,848 logical rows), before endpoints or handles.

| ID | Arm | Outcome: dense → compact | Seconds: dense → compact | Scope peak MB: dense → compact |
| --- | --- | --- | --- | --- |
| 00 | backward | TIMEOUT → TIMEOUT | 30.16 → 30.16 | 145.4 → 165.5 |
| 00 | forward | TIMEOUT → UNKNOWN | 30.21 → 22.07 | 259.1 → 391.9 |
| 01 | backward | TIMEOUT → TIMEOUT | 60.27 → 60.25 | 1187.4 → 540.0 |
| 01 | forward | UNKNOWN → UNKNOWN | 50.25 → 32.81 | 1259.7 → 584.8 |
| 02 | backward | TIMEOUT → TIMEOUT | 60.35 → 60.27 | 782.3 → 324.6 |
| 02 | forward | UNKNOWN → UNKNOWN | 56.14 → 43.82 | 766.3 → 276.6 |
| 03 | backward | TIMEOUT → TIMEOUT | 60.25 → 60.18 | 252.4 → 167.7 |
| 03 | forward | TIMEOUT → TIMEOUT | 60.21 → 60.21 | 256.4 → 187.1 |
| 04 | backward | UNKNOWN → UNKNOWN | 13.58 → 10.59 | 62.3 → 47.4 |
| 04 | forward | UNKNOWN → UNKNOWN | 43.86 → 28.47 | 219.5 → 184.5 |
| 05 | backward | TIMEOUT → TIMEOUT | 60.20 → 60.24 | 611.6 → 504.9 |
| 05 | forward | UNKNOWN → UNKNOWN | 28.64 → 16.22 | 310.3 → 182.6 |
| R | backward | OOM → TIMEOUT | 15.94 → 60.13 | 8589.9 → 403.4 |
| M | UNREAL forward | TIMEOUT → TIMEOUT | 180.51 → 180.30 | 7931.7 → 3758.4 |

R changes from construction OOM to a completed 14.10 MB table and 50.09 seconds
in search, with a 403.42 MB peak. M's complete table falls from 5.48 to 1.73 GB
and its peak from 7.93 to 3.76 GB. Both compact memory cases remain TIMEOUT;
no conclusive corpus solve is claimed.

The five pairs that both finish UNKNOWN run faster in these single observations;
the capped runs remain censored. The small-table input 00 has higher scope peaks
in both compact arms (145 → 165 MB backward, 259 → 392 MB forward). Its compact
forward arm reaches the existing node budget while the dense run is still capped;
the table reduction does not bound search memory. Matched repetitions and the full
portfolio screen are needed before any runtime or coverage admission claim.

## Representation decision

The panel is sparse, supporting compaction independently of the predecessor
share. A single vector allocation now contains one header word, sorted
(destination, absolute offset) records for nonempty rows, and the endpoint
sequence. On this LP64 build, metadata changes from 24 bytes per logical row
to 16 bytes per nonempty row plus 8 bytes per action. Endpoints remain 8 bytes
and retain their original order, with exact combined allocation rather than
geometric capacity in thousands of row vectors. One temporary count/cursor
array is allocated only while constructing an action.

The payload handle remains 24 bytes. The existing input/action list nodes and
picker splices remain; no new mutable ordering structure or thresholds are
introduced. Application visits occupied destinations in ascending order,
skipping empty rows that performed no updates. Comparisons reproduce the old
dense lexicographic ordering, including empty rows and dimensions. The optional
dominance experiment and equivariance's dense derived tables remain supported.
There are no input-specific rules or shared state between invocations.

## Correctness and reproducibility

`tests/action_payload_reference.hh` freezes the original dense actioner, with
only a namespace change. Its source equality is checked against the pinned git
header in `frozen-reference-validation.json`. On 64 generated games, tests
compare serialized tables, four picker variants over 128 calls with changing K,
complete private RNG engine state, stable action identities, predecessor input
and output ranks, dominance traversal and results, complete forward/backward
solver logs, winning regions and extracted HOA strategies byte for byte.
The existing 5,000-game guarded forward oracle also exercises the compact table.

Release measurements use the same preset, compiler, flags and dependencies:
`otf_sparse_formula_gr1_real_prepass`, release, debug symbols, LTO, and
`acacia_native_arms=false`. Native OxiDD artifacts are unavailable locally;
standalone measured legacy arms do not use them. The checked build enables all
four available legacy default arms and Python bindings, with assertions retained.
Every build uses `-j 3`. No production options or default portfolio were changed.

Raw logs, phase records, perf files, selection and numeric TSV/JSON are under
`build_scratch/i208/_bm-logs.measurements/`; frozen source/binaries are under
`build_scratch/i208/frozen/`. The original executable and observation-only dense
executable are retained separately. `profile_one.py` runs one explicitly selected
input/arm; `summarize_one.py` derives the measurements. A skipped prefix or timed
out observation is never treated as a complete table or a solved result.

Morning2s's REAL arm remained in translation at 60 seconds. Its action table was
therefore measured using the UNREAL automaton-forward arm identified by #200's
records, at 180 seconds. Its complete dense table is larger than the older
approximately 1 GB construction-prefix estimate. Robot's dense run hit 8 GiB;
its table counters describe the last retained prefix, not a completed table.

The full no-regression/coverage screen, native-enabled build and admission
decision remain with the driver. Censored single runs do not establish a speedup.

Validation: unit suite **70/70 pass**; full pytest **2,299 passed, 40 skipped,
23 subtests passed**; anti-hardcoding guard **96 passed, 1 skipped**; ruff on the
three diagnostic Python helpers and `git diff --check` pass. The config frontend
check passes (49 options); no options changed. Logs are in the measurement folder.

Frozen dense observation binary SHA-256:
`9957a482c80da96057390a6d4a2c126b824d55d4f7ce77ecc73901025de13766`.
Frozen compact binary SHA-256:
`fee751d099bb3f88b6eb36c1fc627e173e625c856c2b9e6885d8a1ff04ba280e`.
The pristine dense executable is also retained separately; `compact-manifest.json`
and `frozen-reference-validation.json` bind the payload sources and reference.
