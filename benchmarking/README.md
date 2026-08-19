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

The cap and direct-simulation preprocessing census is deliberately excluded
from ordinary diagnostics because it can be expensive. Add
`--preprocessing-census-only` to measure those reductions and stop before the
game solver; its CSV fields otherwise remain zero.

`summarize-diag-phases.py` first separates translation, action construction,
and fixed-point stalls.  Diagnostics builds also split fixed-point time into
input picking, backward action application, and downset work; the summary
labels a target `letter-loop-bound`, `downset-bound`, or `mixed` (within 20%).

# Deterministic stratified panels

`convert-tlsf-corpus-native.py` materializes a flat staging directory of
`.ltl`/`.part` pairs through the
`tlsf-frontend-inspect` helper, which links the same frontend implementation as
`acacia-bonsai -T`. It does not invoke SyFCo or any standalone tlsf-tools
binary. Use `--selection` to reproduce a competition selection and
`--list-output` to emit its Meson suite manifest; `conversion.tsv` records the
source, formula, and partition hashes plus TLSF semantics/target metadata.
`syntcomp-pool.py` then imports exact pairs into the shared content-addressed
`tests/ltl/syntcomp` corpus and writes the year-specific `sources.tsv` map.

For example:
```
python3 benchmarking/convert-tlsf-corpus-native.py \
  selection-ltl-2026 /tmp/syntcomp26-stage \
  --native-inspect build/tests/tlsf-frontend-inspect \
  --selection syntcomp26.tlsf.list \
  --list-output tests/suites/benchmarks/syntcomp26/all.list

python3 benchmarking/syntcomp-pool.py \
  --pool tests/ltl/syntcomp --maps-root tests/suites/benchmarks \
  --suite syntcomp26=/tmp/syntcomp26-stage
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

See [NEGATIVE-RESULTS.md](NEGATIVE-RESULTS.md) for the durable record of
optimization ideas rejected by the gates.

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

# Gates

- **G0, correctness:** `meson test -C build --suite=unit` and
  `meson test -C subprojects/posets/build`; both must report `Fail: 0`.
- **G1, frozen verdicts:** `benchmarking/regression-gate.sh build`; all 40
  sentinels must pass and the script must print `GATE PASS`.
- **G2, Posets proxy (advisory):** `benchmarking/posets-microbench.sh`.
- **G2s, solver-profile proxy:** `benchmarking/solver-profile-gate.sh build`.
- **G3, landing bar:** run `benchmarking/landing-campaign.sh` with paired
  binaries, suite lists, a 17-second timeout, and an output directory. It
  invokes `benchmarking/landing-bar.py`; every suite must print `GATE PASS`.
- **G4, corpus correctness:**
  `meson test -C build --num-processes 1 --suite=ab/realizable --suite=ab/unrealizable`;
  timeouts are allowed, but `Fail: 0` and no false-positive/negative marker are
  required.
- **G5, native TLSF parity:** run `benchmarking/tlsf-verdict-parity.py` and
  `benchmarking/check-tlsf-conversion.py` against the selected TLSF corpus.

# Native TLSF parity

The native route was rechecked against tlsf-tools `338fdd3`. SyFCo is a
compatibility reference, not the semantic oracle: the native frontend follows
TLSF's Strict weak-until rules, semantics/target adaptation, and enum
valuation-list, wildcard, and REQUIRE/ASSERT validity rules where they differ.

| check | cohort | result |
|---|---:|---|
| solver verdicts | 1,579 | GATE PASS; 0 opposite verdicts, 0 errors, 1 native-only and 3 converted-only answers |
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
