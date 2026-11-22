# SYNTCOMP 2025 panel

`all.list` contains 1,579 inputs from official
`selection-ltl-2025v2/selection-ltl-2025`. Seven strong-next `finding_nemo` inputs
were excluded during historical SyFCo conversion; `skipped.tsv` records them.
Current runs use TLSF: `tlsf-sources.tsv` maps logical names to flat filenames.

[Materialize the corpus](../../../../benchmarking/README.md#corpus-and-panel-selection)
from the `tests/syntcomp-benchmarks` submodule, pinned to upstream
[SYNTCOMP/benchmarks](https://github.com/SYNTCOMP/benchmarks) tag `v2026`:

```sh
python3 benchmarking/syntcomp-corpus.py init
python3 benchmarking/syntcomp-corpus.py materialize --out /tmp/syntcomp-tlsf
meson setup build \
  -Dacacia_tlsf_corpus_dir=/tmp/syntcomp-tlsf
```

The submodule uses `update = none`; ordinary submodule initialization skips it.
The flat corpus has 1,586 files: 807 copied and 779 expanded from templates,
all checked against `tests/suites/benchmarks/tlsf-manifest.tsv`. Gate lookup uses
CLI, environment, build option, then recorded pointer; Meson needs the option
at configure time. G2s/G3 use the candidate build, with G2s calibration using
the baseline build.

## Panel selection

`panel.list` selects 180 inputs: 40 easy (Acacia <1 s), 65 border (1 to <17 s),
60 gap (only ltlsynt answers) and 15 open (neither answers). Answers need harness
`OK` and exactly one standalone decisive verdict. `make-panel.py` apportions each
stratum by verdict and samples normalized families round-robin with seed
`20260804`. Normalization removes generated hash/numeric suffixes and groups
arbiter, ltl2dba/ltl2dpa, robot-grid and collector families.

The reference `gap-plan-20260804-localrest` compared `best_decomp_mona` at
`5c589692a84480f7267b0efcabde66afc21d5f8e` with default ltlsynt from Spot
2.15.1.dev: 17 s, serial, 8 GiB/no swap. `panel.tsv` records strata, both reference
times, verdicts, families and campaign; `panel.list` comments record composition.

## Input basis

The legacy `sources.tsv` keeps 1,171 pairs also needed by the 2021/2024 suites;
current 2025 runs use `-T`. The full retained pool is described in the
[corpus README](../../../ltl/syntcomp/README.md). Switching from SyFCo pairs to
native TLSF preserved all 111 panel solves across four arms, with zero differing
inputs (`syntcomp25-basis-20260828-localrest`). With `-T`, ltlsynt converts TLSF
itself through SyFCo and receives explicit `--semantics`.

Historical import/basis details are archived in docs-20261122. SYNTCOMP accepts
benchmarks under [CC BY](https://www.syntcomp.org/submission/).
