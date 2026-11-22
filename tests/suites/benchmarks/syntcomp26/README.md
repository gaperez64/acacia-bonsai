# SYNTCOMP 2026 panel

`all.list` is the official 1,524-input realizability selection: unique `inst` names
in `tlsfReal/results.csv`, Zenodo `10.5281/zenodo.21451603`. It excludes 62 files
from the 1,586-file `v2026` release ZIP, whose SHA-256 is
`8a7038322ec6c4f7ca7754d3660c3e3b6c79fecab8c41b1304ed0a0589a89843`.
`tlsf-sources.tsv` maps those logical names to TLSF filenames.

Use the [2025 corpus setup](../syntcomp25/README.md) and
[shared lookup rules](../../../../benchmarking/README.md#corpus-and-panel-selection).
Both suites use the same verified upstream corpus. Historical native conversion
hashes/I/O/timing metadata remain in `conversion.tsv`; `skipped.tsv` has only its
header. Converted pairs and `sources.tsv` are no longer suite inputs.

## Panel selection

`panel.list` selects 180 inputs with the 2025 verdict-proportional/family-round-robin
algorithm and seed `20260804`. Here stratification uses official Acacia solver
ID 2 (`decomp-mona`) and ltlsynt ID 6 (`lar`) from `tlsfReal/solvers.md`. An answer
requires `statusSolve=ok`, `resultSolve` 0 or 1 and `timeSolveWall <17` s;
missing, failed or later results count as unanswered.

| Stratum | Pool | Selected | Realizable | Unrealizable | Unknown |
|---|---:|---:|---:|---:|---:|
| easy | 661 | 40 | 23 | 17 | 0 |
| border | 174 | 65 | 22 | 43 | 0 |
| gap | 422 | 60 | 20 | 40 | 0 |
| open | 267 | 15 | 0 | 0 | 15 |

`panel.tsv` records official reference times, strata, verdicts, families and source
campaign. ltlsynt converts TLSF independently through SyFCo with explicit
`--semantics`. Earlier import details are archived in docs-20261122.
