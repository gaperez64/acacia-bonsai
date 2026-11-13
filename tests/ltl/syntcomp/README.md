# Shared SYNTCOMP LTL corpus

This directory stores the 1,234 byte-distinct `.ltl`/`.part` pairs referenced
by the SYNTCOMP 2021 and 2024 suites and the compatibility 2025 source map.
The 2,468 payload files total 5,690,457 bytes (about 5.43 MiB). Each
`sources.tsv` under `tests/suites/benchmarks/syntcompYY/` maps logical names
to this pool. The primary 2025 and 2026 suites use TLSF through
`tlsf-sources.tsv`; the retired 2026 LTL source map is absent.

A pair is named by

```
SHA256("acacia-syntcomp-pair-v1\0" || SHA256(ltl) || SHA256(part))
```

using the raw 32-byte component digests.  Hashing both files is important:
two conversions can have the same formula but different input/output
partitions.  Full hashes also allow the same official name to map to different
conversion output in different years.

`index.tsv` records the pair and component hashes. To import a new legacy
LTL suite, substitute its name and staged flat corpus path below. This is
separate from materializing the primary TLSF suites.

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
