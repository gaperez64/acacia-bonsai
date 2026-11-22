# SYNTCOMP 2024 panel

Converted LTL pairs arrived in `104cef3d7427121f2d94b7744fc2dc98a331909a`;
`sources.tsv` resolves them into the shared [corpus](../../../ltl/syntcomp/README.md).
The import retained no upstream archive URL/checksum or conversion command.

`panel.list` has 174 inputs: 40 easy (Acacia <1 s), 59 border (1 to <17 s), 60 gap
(only ltlsynt answers) and 15 open (neither answers). The requested border quota
was 65, but the 1,195-input reference had only 59. Selection is verdict-proportional,
then family-round-robin with seed `20260804`; `panel.tsv` records every selection.

The reference `gap-plan-20260804-localrest` compared `best_decomp_mona` at
`5c589692a84480f7267b0efcabde66afc21d5f8e` with Spot 2.15.1.dev's default
ltlsynt, serially at 17 s, 8 GiB/no swap. [Restore it](../../../../benchmarking/README.md#evidence-and-retention)
under `restore/gap-plan`, then recreate the panel:

```sh
python3 benchmarking/make-panel.py \
  --reference restore/gap-plan/_bm-logs.gap-plan-20260804/syntcomp24-reference \
  --acacia best_decomp_mona --ltlsynt ltlsynt \
  --source-map tests/suites/benchmarks/syntcomp24/sources.tsv \
  --output tests/suites/benchmarks/syntcomp24/panel \
  --cap 17 --seed 20260804 --easy 40 --border 65 --gap 60 --open 15
```

Other lists are historical slices. `0s-20s.list` expands the complete historical
sub-20-second buckets, independently of `panel.list`.
