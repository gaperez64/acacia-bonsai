# SYNTCOMP 2025 LTL corpus

These 1,579 instances are the historically SyFCo-convertible specifications
selected for the 2025 synthesis competition; seven strong-next inputs are
listed in `skipped.tsv`.  Their official logical names are mapped by
`sources.tsv` to exact pairs in the shared `tests/ltl/syntcomp` pool.

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
