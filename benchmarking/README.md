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
sorted by PAR-2 score (each OK charged its wall-clock, each TIMEOUT
charged `2 × timeout`). Useful for picking the strongest configurations
after adding or re-running benchmarks:
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
  --csv _bm-logs/best_decomp_mona_diag-targets.csv \
  ltl2dba_E8.ltl ltl2dba_Q6.ltl
```
Direct mode is the default because it preserves `ACACIA_DIAG` progress lines
when the process-group timeout kills the solver.  `--via-wrapper` is available
when the `check-real-correct.sh` wrapper behavior itself is what needs testing.
Use `--progress-every N` to control periodic solve-loop snapshots; `0` disables
loop snapshots.

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
