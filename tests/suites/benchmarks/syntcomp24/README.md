# SYNTCOMP 2024 panel construction

The repository's converted 2024 LTL formula/partition pairs were added in
commit `104cef3d7427121f2d94b7744fc2dc98a331909a`. Their logical names now
resolve through `sources.tsv` into the shared, content-addressed corpus at
`tests/ltl/syntcomp`, which fixes the exact inputs used by current runs. The
historical import did not retain the upstream archive URL, archive checksum,
or conversion command, so this suite is reproducible from checked-in content
but cannot claim archive-level provenance.

`panel.list` is the deterministic panel generated in commit `87e9b391` from a
complete paired reference campaign at repository commit
`5c589692a84480f7267b0efcabde66afc21d5f8e`. The reference is retained outside
the tree under `_bm-logs.gap-plan-20260804/syntcomp24-reference` and compared
`best_decomp_mona` with Spot 2.15.1.dev's default `ltlsynt` at a 17-second cap,
one test at a time, with 8 GiB of memory and no swap.

The common `benchmarking/make-panel.py` rules classify targets as `easy`
(Acacia below 1 second), `border` (Acacia from 1 to below 17 seconds), `gap`
(only `ltlsynt` answers), or `open` (neither answers). The requested quotas
were 40/65/60/15. The 1,195-instance reference pool contained only 59 border
targets, so the resulting panel has 174 instances: 40 easy, all 59 border, 60
gap, and 15 open. Sampling is verdict-proportional and family-round-robin with
seed `20260804`.

`panel.tsv` is the audit trail for every selected logical instance, including
stratum, reference times, verdict, normalized family, and source campaign.
`baseline-panel.csv` is a later measurement and was not an input to selection.
The generating command was equivalent to:

```sh
python3 benchmarking/make-panel.py \
  --reference _bm-logs.gap-plan-20260804/syntcomp24-reference \
  --acacia best_decomp_mona --ltlsynt ltlsynt \
  --source-map tests/suites/benchmarks/syntcomp24/sources.tsv \
  --output tests/suites/benchmarks/syntcomp24/panel \
  --cap 17 --seed 20260804 --easy 40 --border 65 --gap 60 --open 15
```

The remaining `.list` files are historical runtime, regression, translation,
or solver-success slices. `0s-20s.list`, the panel used by the current paper
campaign, expands to the complete sub-20-second historical buckets rather
than to `panel.list`.
