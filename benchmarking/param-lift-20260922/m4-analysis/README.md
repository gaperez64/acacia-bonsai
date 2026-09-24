# M4 analysis drivers

The measurements behind `../m4-structure.md`. They read seed artefacts produced by

```
python3.13 scripts/gr1_monitor_game.py <tlsf> --semantics exact \
    --output   $GR1_WORK/<family>_<n>.aag \
    --provenance-out build_scratch/m4align/<family>_<n>.prov.json
tlsfsolve --policy $GR1_WORK/<family>_<n>.policy.aag \
          --certificate $GR1_WORK/<family>_<n>.cert.aag \
          $GR1_WORK/<family>_<n>.aag
```

with `PYTHONPATH=/usr/local/lib64/python3.13/site-packages` for Spot/BuDDy, and `GR1_WORK`
pointing at the seed directory (default `build_scratch/gr1-seeds`).

| driver | question | output |
|---|---|---|
| `census_align.py` | does the per-index monitor template set stabilize in `n`, and is the role count constant? | `../m4-alignment.tsv` |
| `census2.py` | same, per axis, for the two-parameter families | `../m4-alignment-2param.tsv` |
| `ksep.py` | smallest `k` with `inv` (or `KSEP_WHICH=move_0`) equal to the conjunction of its `k`-subset projections | `../m4-invariant-separability.tsv`, `../m4-move-separability.tsv` |
| `polsep.py` | does each exported policy guard read only its own client? | printed table (§2b) |
| `invschema.py` | shared AIG/BDD plumbing, plus the discarded adequacy experiment | — |

Two traps these encode, both hit while writing them:

- `bdd_satcount` counts over BuDDy's whole declared variable count. At 256 variables a few-state
  difference vanishes into double precision and every ratio reads 1.000. Use `bdd_satcountset` over
  the state-variable cube. Equality verdicts are BDD identity and were never affected.
- `bdd_setvarnum` cannot decrease, so the variable count is set once for all seeds, not per seed.

Run them cgroup-wrapped (`systemd-run --user --scope -p MemoryMax=6G -p MemorySwapMax=0`) and one
at a time.
