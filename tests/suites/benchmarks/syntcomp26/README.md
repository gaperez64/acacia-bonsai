# SYNTCOMP 2026 panel construction

`all.list` is the exact 1,524-instance LTL-realizability selection reported in
the official SYNTCOMP 2026 result artifact, rather than every file in the
release ZIP. The official `v2026` `tlsf.zip` contains 1,586 TLSF files; the
selection is the 1,524 unique `inst` names in `tlsfReal/results.csv` from
Zenodo record `10.5281/zenodo.21451603`. Thus 62 release files that were not in
the official realizability run are not part of this suite.

The release ZIP has SHA-256
`8a7038322ec6c4f7ca7754d3660c3e3b6c79fecab8c41b1304ed0a0589a89843`.
All 1,524 selected files were converted successfully by
`benchmarking/convert-tlsf-corpus-native.py`, using the
`tlsf-frontend-inspect` helper linked to the same frontend as
`acacia-bonsai -T`. No SyFCo or standalone tlsf-tools executable was used.
`tests/ltl/syntcomp26/conversion.tsv` records source/formula/partition hashes,
I/O counts, and TLSF semantics/target metadata; `skipped.tsv` is empty apart
from its header.

`panel.list` applies the same 180-instance construction as the 2025 panel: 40
easy, 65 border, 60 gap, and 15 open; verdict-proportional allocation within
each stratum; family-round-robin sampling; and seed `20260804`. The answer and
family rules are exactly those implemented and described for the 2025 panel.

For panel stratification, Acacia is official solver ID 2 (`decomp-mona`) and
`ltlsynt` is official solver ID 6 (`lar`) in `tlsfReal/solvers.md`. A result
counts as answered at the local paper cap only when `statusSolve` is `ok`,
`resultSolve` is 0 or 1, and `timeSolveWall` is strictly below 17 seconds.
Missing rows, other exit codes, and results at or above 17 seconds are treated
as unanswered. This yields the following pools before deterministic sampling:

| Stratum | Pool | Selected | Realizable | Unrealizable | Unknown |
|---|---:|---:|---:|---:|---:|
| easy | 661 | 40 | 23 | 17 | 0 |
| border | 174 | 65 | 22 | 43 | 0 |
| gap | 422 | 60 | 20 | 40 | 0 |
| open | 267 | 15 | 0 | 0 | 15 |

`panel.tsv` preserves every selected instance's official reference times,
stratum, verdict, family, and source campaign. To reproduce the selection,
first materialize the official references and exact TLSF list, convert through
the linked frontend, and invoke the common sampler:

```sh
python3 benchmarking/syntcomp-results-reference.py results.csv \
  --tlsf-dir v2026/tlsf --corpus tests/ltl/syntcomp26 \
  --reference syntcomp26-official-tgcc --selection syntcomp26.tlsf.list \
  --series official_acacia_decomp_mona=2 \
  --series official_ltlsynt_lar=6 --expected 1524 --cap 17 \
  --source-description 'SYNTCOMP 2026 tlsfReal/results.csv'

python3 benchmarking/convert-tlsf-corpus-native.py v2026/tlsf \
  tests/ltl/syntcomp26 --native-inspect build/tests/tlsf-frontend-inspect \
  --selection syntcomp26.tlsf.list \
  --list-output tests/suites/benchmarks/syntcomp26/all.list

python3 benchmarking/make-panel.py \
  --reference syntcomp26-official-tgcc \
  --acacia official_acacia_decomp_mona --ltlsynt official_ltlsynt_lar \
  --corpus tests/ltl/syntcomp26 \
  --output tests/suites/benchmarks/syntcomp26/panel \
  --cap 17 --seed 20260804 --easy 40 --border 65 --gap 60 --open 15
```
