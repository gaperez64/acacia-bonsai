# Shared SYNTCOMP LTL corpus

This directory stores the 1,234 byte-distinct `.ltl`/`.part` pairs referenced
by the SYNTCOMP 2021 and 2024 suites and the compatibility 2025 source map.
Each
`sources.tsv` under `tests/suites/benchmarks/syntcompYY/` maps logical names
to this pool. The primary 2025 and 2026 suites use TLSF through
`tlsf-sources.tsv`; the retired 2026 LTL source map is absent.

A pair is named by

```
SHA256("acacia-syntcomp-pair-v1\0" || SHA256(ltl) || SHA256(part))
```

Use raw 32-byte component digests. Both files determine identity: the same
formula can have different I/O partitions, and official names can refer to
different conversions across years.

`index.tsv` records pair/component hashes. Import a legacy LTL suite from a
staged flat directory with:

```sh
python3 benchmarking/syntcomp-pool.py \
  --pool tests/ltl/syntcomp \
  --maps-root tests/suites/benchmarks \
  --suite syntcomp-legacy-new=/path/to/staged/legacy-new
```

Validate the committed index and all surviving mappings without writing:

```sh
python3 benchmarking/syntcomp-pool.py \
  --pool tests/ltl/syntcomp \
  --maps-root tests/suites/benchmarks --check
```

The importer never rewrites an existing hash to different bytes and rejects
missing partners, hash mismatches, dangling mappings, and unreferenced pool
pairs.
