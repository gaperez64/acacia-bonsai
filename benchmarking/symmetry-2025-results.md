# Symmetry 2025 A/B campaign

This campaign was run on 2026-08-03 at source revision `2ffef8bb`, using the
24-instance `symmetry-2025` panel from the 2025 LTL selection.  It compares the
four `docker_default` presets with `acacia_enable_equivariant_solver` as the
only changed build option.

## Protocol

- release, optimization level 3, LTO, and the native compiler profile;
- GCC 15.2.1, Spot 2.15.1, and SyFCo 1.2.1.2;
- one solver at a time, with adjacent on/off pairs and counterbalanced order;
- 17-second timeout per invocation;
- one systemd scope per invocation, with 8 GiB RAM and zero swap;
- 192 invocations: 24 instances x 4 presets x 2 modes.

## Results

`solved` and `common time` are reported as off/on.  Common time sums only
instances solved in both modes.  PAR-2 assigns 34 seconds to an unsolved run.

| preset | solved | common time (s) | common-time gain | PAR-2 off/on |
|---|---:|---:|---:|---:|
| `best_decomp_mona` | 17/17 | 37.760/16.358 | 56.68% | 275.760/254.358 |
| `best_decomp_rank_bucketed_mona` | 17/17 | 11.875/8.993 | 24.26% | 249.875/246.993 |
| `best_decomp_bboxtree_mona` | 16/17 | 22.253/19.075 | 14.28% | 294.253/273.911 |
| `best_decomp_filtered_vector_mona` | 16/17 | 25.982/14.482 | 44.26% | 297.982/257.184 |
| **all pairs** | **66/68** | **97.870/58.909** | **39.81%** | **1117.870/1032.447** |

There were no verdict mismatches, no instances solved only with symmetry off,
and two instances solved only with symmetry on:

- `amba_decomposed_arbiter_pb_6_pe_` with filtered-vector downsets;
- `arbiter_on_inpchange_pb_4_pe_` with bboxtree downsets.

The ten expected-symmetry instances reduced common-solved time by 69.77%.
The four indexed controls and ten general controls changed by +2.93% and
+3.37%, respectively.  No common-solved pair had both a slowdown above 10%
and an absolute slowdown above 50 ms.  The largest observed slowdowns were
0.301 seconds (4.16%) and 0.189 seconds (2.63%) on the same indexed control;
these are below that materiality threshold and did not change solve status.

This focused, stratified panel is decision evidence rather than a corpus-wide
performance estimate.  It supports retaining the equivariant solver as the
default: the intended families improve materially, the controls remain
neutral, and this sample contains no correctness or solve-set regression.
