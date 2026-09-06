# At a glimpse: suggested process

In the coming sections of this README, all of the following commands are
explained and justified. We sum up the process we suggest
here for convenience.
```
./self-benchmark.sh -b ab/syntcomp21/crit -t 1
```
Wait for completion of benchmarking of multiple versions of Acacia-Bonsai.
This can take a few hours!

```
mkdir mkplottable

for f in _bm-logs/*.json; do \
    meson-to-mkplot.sh $(basename $f .json) $f > mkplottable/$(basename $f); \
  done

mkplot.py --lloc='upper left' --ymin=1e-2 --ylog -b pdf --save-to plot.pdf mkplottable/*.json
```
Now `plot.pdf` contains a plot of the benchmarking of the
different configurations of Acacia-Bonsai.

## Dependency
Above, we are using `mkplot.py` (https://github.com/alexeyignatiev/mkplot), a
tool to produce cactus plots.

  
# Generating the plots

Once a few JSON files have been produced in _bm-logs/, one can convert the files
to a format that mkplot understands.  To convert one JSON from the test output
to the mkplot format, one can use:
```
meson-to-mkplot.sh 'Title of Plot' testlog.json > mkplottable.json
```

Survival, a.k.a. cactus, plots are then generated using, for instance:
```
mkplot.py --lloc='upper left' --ymin=1e-2 --ylog -b pdf --save-to plot.pdf mkplottable/*.json
```

# Ranking configurations by PAR-2

`rank_bm_logs.py` prints a table of all configurations in `_bm-logs/`
sorted by PAR-2 score. It reports timeout, UNKNOWN/resource-limit, and error
separately; every non-answer is charged `2 × timeout`. Useful for picking the
strongest configurations after adding or re-running benchmarks:
```
benchmarking/rank_bm_logs.py              # reads ../_bm-logs by default
benchmarking/rank_bm_logs.py path/to/logs # or a custom directory
```
The per-instance timeout cap defaults to the largest `duration` observed
across the TIMEOUT entries in the logs; override it with
`--timeout SECONDS` if you want a specific value.

When benchmark runs are split with Meson's `--slice=N/M`, aggregate the complete
slice sets first:
```
benchmarking/aggregate_bm_slices.py _bm-logs --out _bm-logs/aggregated
benchmarking/rank_bm_logs.py _bm-logs/aggregated --timeout 17
```
Incomplete slice sets are skipped, so interrupted smoke runs do not pollute the
ranking table.

If the directory also contains older one-shot runs, require the current slice
count explicitly:
```
benchmarking/aggregate_bm_slices.py _bm-logs --out _bm-logs/aggregated --min-slices 4
```

For Acacia-vs-ltlsynt diagnostics, `loss-set.py` extracts the `ltlsynt_only`
and `acacia_slow` instances, while `ltlsynt_ablation_report.py` annotates those
instances with the ltlsynt ablations that still solve them:
```
python3 benchmarking/loss-set.py --logs _bm-logs/aggregated \
  --acacia best_decomp_mona --ltlsynt ltlsynt \
  --csv _bm-logs/best_decomp_mona-vs-ltlsynt-loss-set.csv

benchmarking/ltlsynt_ablation_report.py --logs _bm-logs/aggregated \
  --acacia best_decomp_mona \
  --csv _bm-logs/best_decomp_mona-ltlsynt-ablation-report.csv
```

For focused Acacia phase diagnostics, build a preset with diagnostics enabled
and run selected LTL instances directly through the diagnostics binary:
```
python3 benchmarking/run_diag_targets.py \
  --build build_best_decomp_mona_diag \
  --timeout 25 \
  --systemd-scope --memory-max 8G --memory-swap-max 0 \
  --csv _bm-logs/best_decomp_mona_diag-targets.csv \
  ltl2dba_E8.ltl ltl2dba_Q6.ltl
```
Direct mode is the default because it preserves `ACACIA_DIAG` progress lines
when the timeout kills the solver. Use `--systemd-scope` for experiments so
the solver and all forked children run in a named memory-limited cgroup;
`--via-wrapper` is available when the `check-real-correct.sh` wrapper behavior
itself is what needs testing. Use `--progress-every N` to control periodic
solve-loop snapshots; `0` disables loop snapshots.

The shared bounded-runner path uses `KillMode=control-group` and tears down both
the named user scope and the launcher's process group on every return path. It
accepts a printed Acacia verdict only when the documented process exit code
agrees, and classifies timeouts or resource limits before parsing solver output.

The cap and direct-simulation preprocessing census is deliberately excluded
from ordinary diagnostics because it can be expensive. Add
`--preprocessing-census-only` to measure those reductions and stop before the
game solver; its CSV fields otherwise remain zero.

`summarize-diag-phases.py` first separates translation, action construction,
and fixed-point stalls.  Diagnostics builds also split fixed-point time into
input picking, backward action application, and downset work; the summary
labels a target `letter-loop-bound`, `downset-bound`, or `mixed` (within 20%).

The semantic-action and M2 sprint -- the pre-decoding action quotient that landed, and the
representation measurements that closed the compressed-downset branch -- is in
[SEMANTIC-ACTIONS-AND-M2-SPRINT.md](SEMANTIC-ACTIONS-AND-M2-SPRINT.md), with its census in
[semantic-action-census.tsv](semantic-action-census.tsv).

The completed zero-tail versus bare-vector ablation — five-by-20-second
LTO/no-LTO runs, profile, and disassembly comparison — is in
[STATE-VECTOR-TAIL-STUDY.md](STATE-VECTOR-TAIL-STUDY.md). The TLSF
normalization/HOA replay outcome is in
[TLSF-NORMALIZATION-STUDY.md](TLSF-NORMALIZATION-STUDY.md).
The checksum-verified final current-versus-Acacia-1.x cactus plots are in
[plots/final-v1-current-20260825](plots/final-v1-current-20260825/README.md).

# Deterministic stratified panels

The current flow for the TLSF-backed `syntcomp25` and `syntcomp26` suites
reconstructs the SYNTCOMP TLSF corpus from the `tests/syntcomp-benchmarks`
submodule. `syntcomp-corpus.py materialize` verifies and writes a flat corpus,
and `-Dacacia_tlsf_corpus_dir` makes that directory available to Meson. Each
suite's `tlsf-sources.tsv` maps its logical `.ltl` instance names to the TLSF
sources passed to the configured backend.

Materialize once: any one of `--tlsf-corpus DIR` (G2s/G3),
`ACACIA_TLSF_CORPUS=DIR`, or the build's `-Dacacia_tlsf_corpus_dir=DIR` is
enough for the gates, in that precedence order. Without an override, they use
the repository's `.acacia-tlsf-corpus-path` pointer written by materialize,
provided the directory still exists and contains its `.acacia-tlsf-corpus`
marker. The marker records the entry count and manifest SHA-256;
`materialize --no-record` skips updating the pointer. A setting that names a
directory which is not there is skipped rather than used, so a stale
`acacia_tlsf_corpus_dir` left in an old build cannot mask a live
`ACACIA_TLSF_CORPUS`; when nothing resolves, the gate says which mechanisms it
consulted and why each one failed. Meson still needs the
build option at configure time to enumerate these suites; the gates reuse it
without a second setting. G2s/G3 look in the candidate binary's build directory
(`BUILD/src/acacia-bonsai`); G2s calibration uses the baseline build.

For example:
```
python3 benchmarking/syntcomp-corpus.py materialize \
  --out /tmp/syntcomp-tlsf

meson setup build \
  -Dacacia_tlsf_corpus_dir=/tmp/syntcomp-tlsf
```

The retained flow applies to the `syntcomp21` and `syntcomp24` suites.
`convert-tlsf-corpus-native.py` produced their still-vendored `.ltl`/`.part`
pairs through the linked `tlsf-frontend-inspect` implementation, and
`syntcomp-pool.py` imported those pairs into the shared content-addressed
`tests/ltl/syntcomp` corpus with year-specific `sources.tsv` maps. Their
upstream TLSF provenance was not retained, so those suites cannot be
reconstructed from the TLSF submodule and remain vendored. Both tools remain
the supported conversion and import path when the original TLSF corpus and
selection are available; the converter records conversion metadata without
invoking SyFCo or a standalone tlsf-tools binary.

For example, given the original `syntcomp24` inputs:
```
python3 benchmarking/convert-tlsf-corpus-native.py \
  /path/to/syntcomp24-tlsf /tmp/syntcomp24-stage \
  --native-inspect build/tests/tlsf-frontend-inspect \
  --selection /path/to/syntcomp24.tlsf.list \
  --list-output /tmp/syntcomp24.list

python3 benchmarking/syntcomp-pool.py \
  --pool tests/ltl/syntcomp --maps-root tests/suites/benchmarks \
  --suite syntcomp24=/tmp/syntcomp24-stage
```

`make-panel.py` builds a family-balanced easy/border/gap/open panel from paired
Acacia and `ltlsynt` Meson JSON logs.  Repeat `--reference` with the newest
campaign first: reference coverage is unioned, and the first campaign that
contains an instance supplies its timings and `source_campaign` provenance.
This lets a later full-corpus sweep repair or extend an older partial reference
without the panel monotonically shrinking to the campaigns' intersection.

For example:
```
python3 benchmarking/make-panel.py \
  --reference _bm-logs/full-current \
  --reference _bm-logs/older-supplement \
  --source-map tests/suites/benchmarks/syntcomp24/sources.tsv \
  --output tests/suites/benchmarks/syntcomp24/panel \
  --cap 17 --easy 40 --border 65 --gap 60 --open 15
```

# Local tuning protocol

Use the local Meson LTL benchmark suites as the default optimization loop.  A
typical first pass is:
```
./self-benchmark.sh \
  -b ab/syntcomp21/crit \
  -c ltlsynt,best_decomp_mona,best_mona,base_iosprecom_mona,best_decomp_kdtree_mona,best_decomp_sharingtrie_mona,best_decomp_sharingtree_mona,best_decomp_simpsharingtree_mona,best_decomp_skiplist_mona,best_decomp_cst_mona \
  -t 1.7 -f
benchmarking/rank_bm_logs.py _bm-logs > _bm-logs/ranking.txt
```

For the cgrouped overnight campaign, the default Acacia set comes from the
`local_tuning_default` preset group.  To run only the downset data-structure
sweep against already-collected tool logs, use:
```
ACACIA_CONFIG_GROUP=posets_downset_sweep TOOL_CONFIGS= \
  ./scripts/overnight-benchmark-session.sh
```

Use the measured top four Acacia configurations from that ranking for the next
comparison round.  Do not use a TLSF walk as the default evidence for an
optimization unless the local Meson suites do not contain the family being
diagnosed.

The focused `ab/symmetry-2025` suite vendors a 24-instance panel from the 2025
LTL selection: ten expected symmetry cases, four indexed protocol controls,
and ten general controls.  Run the top-four on/off comparison with a 17-second
per-instance limit as follows:
```
./self-benchmark.sh \
  -b ab/symmetry-2025 \
  -c best_decomp_mona,best_decomp_mona_noequivariant,best_decomp_rank_bucketed_mona,best_decomp_rank_bucketed_mona_noequivariant,best_decomp_bboxtree_mona,best_decomp_bboxtree_mona_noequivariant,best_decomp_filtered_vector_mona,best_decomp_filtered_vector_mona_noequivariant \
  -t 1.7 -f
```
The source selection and category labels are recorded in
`benchmarking/symmetry-2025-sample.tsv`; the default-on decision campaign is
summarized below. Across all four configuration pairs, enabling the exact
equivariant path preserved every answer, gained two answers, reduced common
solved time by 39.81% (97.870 s to 58.909 s), and improved PAR-2 from
1117.870 s to 1032.447 s.

| configuration | solved off/on | common time off/on (s) | gain | PAR-2 off/on (s) |
|---|---:|---:|---:|---:|
| `best_decomp_mona` | 17/17 | 37.760/16.358 | 56.68% | 275.760/254.358 |
| `best_decomp_rank_bucketed_mona` | 17/17 | 11.875/8.993 | 24.26% | 249.875/246.993 |
| `best_decomp_bboxtree_mona` | 16/17 | 22.253/19.075 | 14.28% | 294.253/273.911 |
| `best_decomp_filtered_vector_mona` | 16/17 | 25.982/14.482 | 44.26% | 297.982/257.184 |

The August 2026 admission-gate campaign lowered the shipping
`acacia_equivariant_min_blocks` threshold from 4 to 2.  It passed G1 at 40/40,
preserved all SYNTCOMP26 panel answers, and converted
`arbiter_with_buffer_pb_5_pe_.ltl` from a timeout to REALIZABLE in 7.011 s.

| panel | solved, blocks 4/2 | PAR-2, blocks 4/2 (s) |
|---|---:|---:|
| SYNTCOMP25 | 106/107 | 2780.724/2746.223 |
| SYNTCOMP26 | 134/134 | 1702.186/1706.153 |

The four `best_decomp_rank_bucketed_mona_eq_*` experiment presets pin all three admission values,
so the one-at-a-time comparisons remain reproducible after the shipping default changed.
The combined minimum-block plus safety-core-witness head also passed the SYNTCOMP21 critical
screen at 91/94; common solved time moved from 114.711 s with the four-block preset to 103.355 s
with the two-block preset, with the same three timeouts.

See [LTLSYNT-GAP.md](LTLSYNT-GAP.md) for the current comparison with `ltlsynt`, the residual gap
analysis, and the durable record of optimization ideas rejected by the gates.

`self-benchmark.sh` also exposes ltlsynt ablation pseudo-configs.  They run the
same local `ltlsynt/...` Meson suites as `ltlsynt`, but set `LTLSYNT_OPTS`:
```
./self-benchmark.sh \
  -b ab/syntcomp21/crit \
  -c ltlsynt,ltlsynt_no_bypass,ltlsynt_no_obligation,ltlsynt_no_decompose,ltlsynt_no_specials \
  -t 1.7 -f
```

Use these only to classify why ltlsynt wins a local loss-set instance:
decomposition, bypass/direct-strategy checks, obligation synthesis, or the
general backend.

Benchmark configurations are the same compile-time presets described in the
top-level README.  `self-benchmark.sh -c NAME -R` asks
`scripts/acacia-config.py` for the selected preset, configures Meson with the
corresponding `-Dacacia_*` options, records the normalized preset in
`build_NAME/.acacia-config.json`, and rebuilds when that metadata no longer
matches.  Add new benchmark variants as presets in `config/acacia-presets.json`
instead of passing ad hoc macro flags, so benchmark logs and build directories
remain reproducible.

# Acacia–ltlsynt gap diagnosis

The August 2026 shipping-matched census originally audited 267 rows. A corrected empty-partition
wrapper reclassified six of them, leaving 153 `ltlsynt_only` rows plus 108 rows where both tools
solve but Acacia is more than 2× slower and takes more than 0.3 s. See
[LTLSYNT-GAP.md](LTLSYNT-GAP.md) for the 261-row residual census, the six-row audit trail, and
exact telemetry.

| set | M1 letter-loop | M2 downset | M3 translation-stall | M4 one-sided-race | mixed | residual total |
|---|---:|---:|---:|---:|---:|---:|
| corrected census rows | 122 | 68 | 36 | 9 | 26 | 261 |
| `ltlsynt_only` | 57 | 39 | 36 | 9 | 12 | 153 |

The measured outcomes are deliberately mechanism-specific:

- Every `ltlsynt` feature to which the ablation attributed a win is present in Acacia; the
  syntactic bypass captured 17/17 predicted instances.
- 86/153 residual losses (56%) are instances `ltlsynt` answers in under 0.2 s. The largest
  concentration is the parameterized arbiter/lift/AMBA block, but the census distinguishes its
  letter-loop, downset, and translation modes.
- The equivariant minimum-block threshold moved from 4 to 2, admitting verified two- and
  three-block layouts. It gained one SYNTCOMP25 panel answer; the other recognition thresholds
  gained none and remain unchanged.
- The `simple_arbiter_unreal2` M3 anchor was Spot's 64-acceptance-set exception, not a translator
  timeout or `push_aps` limit. A sound safety-core witness now solves the measured 25/50/60/75
  family in 0.009–0.020 s.
- Twelve rows formerly labeled M3 had already built a 105–775-state automaton. Two `LedMatrix`
  rows are now M2 and the other ten are M1, leaving 36 M3 losses. Only four rows in the original
  48-row M3 cohort show an architectural parity-translation advantage.
- Correct empty-side partitions raise the reported campaign coverage from 104/180 to 106/180 on
  SYNTCOMP25 and from 133/180 to 134/180 on SYNTCOMP26; all 187 empty-side corpus instances solve
  through the repaired wrapper.
- A semantic whole-letter quotient cut action applications by 14.16–15.59× on the three M1 spike
  targets. After correcting the baseline and corpus lookup, it passed G1 at 40/40 (PAR-2
  101.867 → 87.880 s), gained two SYNTCOMP25 G3 answers, and preserved the SYNTCOMP26 panel.
  G2s then exposed a 680.604% cycle regression on `round_robin_arbiter4` with three candidate
  timeouts, so the quotient was rejected and removed. M4 had only 9 losses and did not meet the
  plan's 15-instance implementation threshold.

Upstream-facing Spot reproducers are prepared in [SPOT-ANOMALIES.md](SPOT-ANOMALIES.md).

# Gates

- **G0, correctness:** `meson test -C build --suite=unit` and
  `meson test -C subprojects/posets/build`; both must report `Fail: 0`.
- **G1, frozen verdicts:** `benchmarking/regression-gate.sh build`; all 40
  sentinels must pass and the script must print `GATE PASS`.
- **G2, Posets proxy (advisory):** `benchmarking/posets-microbench.sh`.
- **G2s, solver-profile proxy:** `benchmarking/solver-profile-gate.sh BASELINE-BIN CANDIDATE-BIN`. The SYNTCOMP25
  `mixed` target is `evasion0.ltl`; `g-unreal-116.ltl` was retired (census: M1 letter-loop).
- **G3, landing bar:** run `benchmarking/landing-campaign.sh` with paired
  binaries, suite lists, a 17-second timeout, and an output directory. It
  invokes `benchmarking/landing-bar.py`; every suite must print `GATE PASS`.
  The syntcomp25 and syntcomp26 panels are reconstructed from the TLSF
  submodule and have no `.ltl` pair for 77 of 180 and 180 of 180 of their rows,
  so both G2s and G3 need a materialized corpus. Materialize once: any one of
  `--tlsf-corpus DIR`, `ACACIA_TLSF_CORPUS=DIR`, or the candidate build's
  `-Dacacia_tlsf_corpus_dir=DIR` suffices, in that precedence order; otherwise
  the gates use the recorded corpus path. G1 uses the same lookup without a
  `--tlsf-corpus` flag.
- **G4, corpus correctness:**
  `meson test -C build --num-processes 1 --suite=ab/realizable --suite=ab/unrealizable`;
  timeouts are allowed, but `Fail: 0` and no false-positive/negative marker are
  required.
- **G5, native TLSF parity:** run `benchmarking/tlsf-verdict-parity.py` and
  `benchmarking/check-tlsf-conversion.py` against the selected TLSF corpus.

Frozen G1 baselines were measured with the shipping preset (see the top-level README) through its
pinned twin `best_decomp_rank_bucketed_mona_eq_min_blocks_2`, which resolves to an identical option
set now that `acacia_equivariant_min_blocks` defaults to 2, and re-validated on the final tree; the
producing binary's SHA-256 is
`6467869a4411233ec148f7136fe6a6595a43205cc2bbd412f8d8beacb55ec2e9`.
`syntcomp24/Morning_f2774e0b.ltl` is frozen at 14.542 s, above the 13.6 s threshold that admits a
51 s cap remeasurement. Full gate results are in [LTLSYNT-GAP.md](LTLSYNT-GAP.md).

# Native TLSF parity

The native route was rechecked against tlsf-tools `338fdd3`. SyFCo is a
compatibility reference, not the semantic oracle: the native frontend follows
TLSF's Strict weak-until rules, semantics/target adaptation, and enum
valuation-list, wildcard, and REQUIRE/ASSERT validity rules where they differ.

| check | cohort | result |
|---|---:|---|
| solver verdicts | 1,579 | GATE PASS; 0 opposite verdicts, 0 errors, 2 native-only and 1 converted-only answers; 3 native and 2 converted resource limits |
| regenerated SyFCo pairs | 50 | 50/50 `.ltl`/`.part` pairs matched |
| native formula semantics | 50 | 48 matches; 1 deliberate enum-validity divergence; 1 normalization exceeded 600 s |
| native I/O lists | 50 | 49/49 classifiable comparisons matched; the normalization timeout was unclassified |

The deliberate formula divergence is
`amba_case_study_pb_2_pe_.tlsf`; the native route places enum validity according
to TLSF rather than SyFCo. The unclassified normalization is
`full_arbiter_unreal1_pb_3_16_pe_.tlsf`; its regenerated SyFCo pair still
matched, but `ltlfilt` did not finish canonicalizing it within 600 seconds.
Literal formula bytes matched 0/49 because the independent printers choose
different parentheses and derived-operator spellings.

# Measurement protocol

Performance gates use a 17-second per-instance cap and run sequentially. Put
each campaign in a user systemd scope with `MemoryMax=8G` and
`MemorySwapMax=0`; the campaign tools use process groups for individual solver
runs inside that outer scope. PAR-2 charges twice the cap for every timeout,
UNKNOWN/resource-limit result, or error, while reporting those categories
separately. If a lost baseline answer took more than 80% of the cap,
`landing-bar.py` automatically re-measures both binaries at three times the
cap before deciding the gate.

Read PAR-2-only changes against the measured same-configuration noise floor. Three baseline runs
spanned 2778.691–2799.825 s on SYNTCOMP25 (21.134 s) and 1702.186–1714.260 s on SYNTCOMP26
(12.074 s). A change inside that spread is not performance evidence by itself; coverage changes
and per-instance losses remain gate evidence.
