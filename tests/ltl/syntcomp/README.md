# Shared SYNTCOMP LTL corpus

This directory stores every byte-distinct `.ltl`/`.part` pair used by the
tracked SYNTCOMP 2021, 2024, 2025, and 2026 suites exactly once.  The official
year-specific names and selections remain under
`tests/suites/benchmarks/syntcompYY/`; each `sources.tsv` maps those logical
names to files in this pool.

A pair is named by

```
SHA256("acacia-syntcomp-pair-v1\0" || SHA256(ltl) || SHA256(part))
```

using the raw 32-byte component digests.  Hashing both files is important:
two conversions can have the same formula but different input/output
partitions.  Full hashes also allow the same official name to map to different
conversion output in different years, as happens between the historical
SyFCo-derived suites and the 2026 linked-frontend conversion.

`index.tsv` records the pair and component hashes.  Import a staged flat
corpus, rebuild the index, and validate all mappings with:

```sh
python3 benchmarking/syntcomp-pool.py \
  --pool tests/ltl/syntcomp \
  --maps-root tests/suites/benchmarks \
  --suite syntcomp26=/path/to/staged/syntcomp26

python3 benchmarking/syntcomp-pool.py \
  --pool tests/ltl/syntcomp \
  --maps-root tests/suites/benchmarks --check
```

The importer never rewrites an existing hash to different bytes and rejects
missing partners, hash mismatches, dangling mappings, and unreferenced pool
pairs.
