# SYNTCOMP 2025 panel construction

`all.list` is the repository's converted copy of the official 2025 TLSF
selection (`selection-ltl-2025v2/selection-ltl-2025`). The historical SyFCo
conversion produced 1,579 `.ltl`/`.part` pairs. Seven `finding_nemo`
specifications using strong next were excluded; their names and
the conversion diagnostic are preserved in `skipped.tsv`.  The official names
in `all.list` resolve through `sources.tsv` into the shared content-addressed
corpus at `tests/ltl/syntcomp`; the corpus conversion provenance is recorded in
the final section below.

`panel.list` is a deterministic 180-instance subset of those 1,579 pairs. Its
reference data is the full, paired, serialized campaign retained under
`_bm-logs.gap-plan-20260804/syntcomp25-reference`, run from repository commit
`5c589692a84480f7267b0efcabde66afc21d5f8e`. It compared the
`best_decomp_mona` Acacia configuration with the default `ltlsynt` from Spot
2.15.1.dev at a 17-second cap, one test job at a time, with an 8 GiB memory
limit and no swap. The four slices cover all 1,579 converted instances.

The shared `benchmarking/make-panel.py` algorithm classified an instance as:

- `easy` when Acacia answered in less than 1 second;
- `border` when Acacia answered in at least 1 but less than 17 seconds;
- `gap` when Acacia did not answer before 17 seconds but `ltlsynt` did; or
- `open` when neither tool answered before 17 seconds.

An answer requires an `OK` harness result and exactly one standalone
`REALIZABLE` or `UNREALIZABLE` verdict. The selected quotas were 40 easy, 65
border, 60 gap, and 15 open. Within each stratum, the quota is apportioned by
verdict, then sampled round-robin across normalized benchmark families. Family
normalization removes generated eight-hex-digit suffixes and numeric parameter
suffixes, with explicit grouping for common arbiter, `ltl2dba`, `ltl2dpa`,
robot-grid, and collector families. Deterministic shuffling uses seed
`20260804`.

`panel.tsv` is the audit trail: it records every selected instance's stratum,
two reference times, verdict, normalized family, and source campaign. The
header comments in `panel.list` record the complete pool and selected
composition.

The generating command was equivalent to:

```sh
python3 benchmarking/make-panel.py \
  --reference _bm-logs.gap-plan-20260804/syntcomp25-reference \
  --acacia best_decomp_mona --ltlsynt ltlsynt \
  --source-map tests/suites/benchmarks/syntcomp25/sources.tsv \
  --output tests/suites/benchmarks/syntcomp25/panel \
  --cap 17 --seed 20260804 --easy 40 --border 65 --gap 60 --open 15
```

## Corpus conversion provenance

The original flat staging corpus was generated with:
`python3 benchmarking/convert-tlsf-corpus.py selection-ltl-2025v2 /tmp/syntcomp25-stage --list-output tests/suites/benchmarks/syntcomp25/all.list`.
It can then be imported without duplicating pairs already used by another
year:

```sh
python3 benchmarking/syntcomp-pool.py \
  --pool tests/ltl/syntcomp --maps-root tests/suites/benchmarks \
  --suite syntcomp25=/tmp/syntcomp25-stage
```

SYNTCOMP accepts benchmark submissions under the Creative Commons Attribution
(CC BY) licence; see https://www.syntcomp.org/submission/.
