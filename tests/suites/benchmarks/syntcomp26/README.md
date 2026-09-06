# SYNTCOMP 2026 panel construction

`all.list` is the exact 1,524-instance LTL-realizability selection reported in
the official SYNTCOMP 2026 result artifact, rather than every file in the
release ZIP. The official `v2026` `tlsf.zip` contains 1,586 TLSF files; the
selection is the 1,524 unique `inst` names in `tlsfReal/results.csv` from
Zenodo record `10.5281/zenodo.21451603`. Thus 62 release files that were not in
the official realizability run are not part of this suite.

The release ZIP has SHA-256
`8a7038322ec6c4f7ca7754d3660c3e3b6c79fecab8c41b1304ed0a0589a89843`.
Historically, all 1,524 selected files were converted successfully by
`benchmarking/convert-tlsf-corpus-native.py`, using the
`tlsf-frontend-inspect` helper linked to the same frontend as
`acacia-bonsai -T`. That conversion used no SyFCo or standalone tlsf-tools
executable. `conversion.tsv` records source/formula/partition hashes, I/O
counts, and TLSF semantics/target metadata; `skipped.tsv` is empty apart from
its header.
Those converted pairs are no longer the suite input, and `sources.tsv` was
deleted entirely. The suite now runs from TLSF: `tlsf-sources.tsv`, with the
tab-separated header `instance\ttlsf`, maps all 1,524 official logical names to
flat TLSF filenames. Meson resolves them against the directory produced by
`python3 benchmarking/syntcomp-corpus.py materialize --out DIR`; the build
receives that directory as `-Dacacia_tlsf_corpus_dir=DIR` and passes each source
to the wrapper with `-T`.

Materialize once: any one of `--tlsf-corpus DIR` (G2s/G3),
`ACACIA_TLSF_CORPUS=DIR`, or the build's `-Dacacia_tlsf_corpus_dir=DIR` is
enough for the benchmark gates, in that precedence order. With none set, they
use the repository's `.acacia-tlsf-corpus-path` pointer, provided its directory
still exists and carries the `.acacia-tlsf-corpus` marker. Materialize writes
both records after verification; the marker contains the entry count and
manifest SHA-256. `materialize --no-record` skips updating the pointer.
Meson still requires the build option at configure time to enumerate suite
entries; the gates reuse it without needing a second setting. G2s/G3 use the
candidate binary's build (`BUILD/src/acacia-bonsai`); G2s calibration uses the
baseline build.

The flat 1,586-file corpus is reconstructed from the
`tests/syntcomp-benchmarks` submodule of
[SYNTCOMP/benchmarks](https://github.com/SYNTCOMP/benchmarks), pinned at tag
`v2026`, rather than kept as a local unpacked release archive. Its checkout is
sparse to `tlsf/`, shallow, and blobless: 42 MB rather than 4.9 GB. It is
registered with `update = none`, so plain `git submodule update --init` skips
it; `syntcomp-corpus.py init` is the opt-in path. Upstream does not carry the
flat competition corpus: 807 files come from the checkout directly and 779 are
reconstructed by expanding its parametric templates. Every file is verified
against the SHA-256 manifest at `tests/suites/benchmarks/tlsf-manifest.tsv`.
An independent byte comparison against the release archive found 0 missing, 0
extra, and 0 mismatching files.

The vendored pairs formerly used by the 2025 and 2026 suites were deleted:
1,932 pooled instances, 3,864 files, and 74.6 MiB. The 2025 `sources.tsv`
retains only the 1,171 pooled instances also referenced by `syntcomp21` and
`syntcomp24`; those two suites have no TLSF provenance and remain on vendored
`.ltl` files.

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
stratum, verdict, family, and source campaign. The current TLSF input basis is
materialized and enabled with:

```sh
python3 benchmarking/syntcomp-corpus.py init
python3 benchmarking/syntcomp-corpus.py materialize --out /tmp/syntcomp-tlsf
meson setup build -Dacacia_enable_tlsf_frontend=true \
  -Dacacia_tlsf_corpus_dir=/tmp/syntcomp-tlsf
```

`ltlsynt` is no longer fed pairs produced by Acacia's frontend. With `-T`, the
wrapper converts the TLSF with SyFCo itself and passes an explicit
`--semantics`; see
[Comparison basis: each tool converts the TLSF itself](../../../../benchmarking/LTLSYNT-GAP.md#comparison-basis-each-tool-converts-the-tlsf-itself).
