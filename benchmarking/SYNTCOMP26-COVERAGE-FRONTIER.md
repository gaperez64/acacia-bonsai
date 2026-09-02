# SYNTCOMP 2026 coverage frontier

Where Acacia-Bonsai stops on the full official 2026 LTL-realizability set, which families it
stops on, and what the solver was doing when it stopped.

## 1. Frozen revisions and binaries

| item | value |
|---|---|
| Acacia | `2178290b` (master; PRs #122 and #123 merged) |
| corpus | `syntcomp-benchmarks` `4105caf1`, materialized and **verified: 1,586 TLSF files** |
| official set | 1,524 instances, `tests/suites/benchmarks/syntcomp26/all.list` |
| B — backward baseline | `best_decomp_rank_bucketed_semantic_mona`, binary `fbe85394…` |
| S — backward + local certificates | `best_decomp_rank_bucketed_semantic_mona_local`, binary `05c81e9b…` |
| diagnostics build (A6) | `..._semantic_mona_diag`, binary `aef21524…`, probe **off** |
| protocol | one transient user-systemd scope per invocation, `MemoryMax=8G`, `MemorySwapMax=0`, sequential, staged caps 1/5/17/60 s |

B and S differ **only** in `acacia_local_certificate`. The arms were verified at the binary
level with `nm` rather than from `meson configure`, because turning the probe on by default
makes `meson configure` report `true` for build directories whose binaries contain no probe.

## 2. Coverage over all 1,524 instances

Cumulative instances decided, by cap:

| arm | 1 s | 5 s | 17 s | 60 s | unsolved |
|---|---:|---:|---:|---:|---:|
| B backward | 936 | 1010 | 1056 | **1093** | 431 |
| S + local certificates | 940 | 1017 | 1065 | **1100** | 424 |
| P survey union | 942 | 1018 | 1065 | **1100** | 424 |

`P` is a **survey union**, not a runtime portfolio: no executable computes it.

**The local-certificate probe is worth 7 instances on the full set** — the first measurement
of its value beyond the two 180-instance panels it was tuned against. All seven are
realizable and all are decided far inside their cap:

| instance | cap | time |
|---|---|---:|
| `box-real` | 1 s | 0.051 s |
| `robot_grid_pb_5_1_pe_` | 1 s | 0.066 s |
| `finding_nemo_pb_1_pe_` | 1 s | 0.134 s |
| `lift_pb_3_pe_` | 1 s | 0.153 s |
| `g-real-real` | 5 s | 2.782 s |
| `finding_nemo_pb_2_pe_` | 5 s | 3.789 s |
| `lift_unary_enc_pb_3_pe_` | 17 s | 4.530 s |

B's 431 unsolved are 412 timeouts, **17 memory exhaustions at 8 GiB**, one OOM kill, and one
UNKNOWN.

## 3. Three wrong corpus annotations

The campaign checks every decisive verdict against the `//STATUS` annotation and stops on a
conflict. Three fired, and **all three were the annotation, not the solver** — each confirmed
by `ltlsynt` 2.15.1.dev independently:

| instance | annotated | Acacia | `ltlsynt` | evidence |
|---|---|---|---|---|
| `lilydemo04_modified` | realizable | UNREALIZABLE | UNREALIZABLE | the file's own comment says the modification exists "to make the spec unrealizable" |
| `lilydemo15` | unrealizable | REALIZABLE | REALIZABLE | two independent solvers agree |
| `lilydemo16` | unrealizable | REALIZABLE | REALIZABLE | two independent solvers agree |

A scan of all 1,586 files found no further case. Fifteen `chomp` instances look like the same
pattern and are not: their comment says the spec is unrealizable only for `N=M=1`, and every
`chomp` instance in the set has `N,M ≥ 2`.

Conflicts are collected rather than fatal, and the runner exits **3** so a conflicted campaign
cannot be mistaken for a clean one. The check is deferred, never waived: a conflict adjudicated
*against* Acacia would be a correctness failure and would stop the sprint.

## 4. Family frontiers

Parameters come from the TLSF release manifest's `param:<template>:<name>=<value>` origins, not
from filenames. Of 1,524 instances, **734 have exact parameter origins** (454 one-parameter,
280 two-parameter) and 790 are direct files with no parameters at all — marked
`parameter_confidence=none`, since only their *family grouping* is a guess.

| classification | families |
|---|---:|
| all solved | 215 |
| **clean cutoff** | **34** |
| singleton hard | 50 |
| direct cluster | 12 |
| mixed direct | 10 |
| all unsolved | 4 |
| **total** | **325** |

57 families are formally orderable (identical parameter names in identical order, all numeric).
Multi-parameter families use the **componentwise** order, never a lexicographic total order, so
a "cutoff" is claimed only where one exists. **77 boundary pairs**: 73 with a genuine covering
solved neighbour, 4 with none.

Representative cutoffs: `amba_case_study` n=2→3, `abcg_arbiter` n=3→4, `arbiter` n=5→6,
`amba_decomposed_arbiter` n=6→7.

## 5. The frozen top-20 targets

Selected from 40 preselected candidates using the committed weight tables, then ranked with
mechanism data from the diagnostics campaign. **Target selection did not consult `ltlsynt` at
any point**; the set is frozen before any external annotation.

19 families, 2 memory-quota rows, none missing diagnostics, scores 19–49.

| # | score | instance | aut states | rank coords | actions |
|---:|---:|---|---:|---:|---:|
| 1 | 49 | `arbiter_on_inpchange_pb_5_pe_` | 53 | 16 | 736 |
| 2 | 46 | `chomp_pb_4_2_pe_` | 75 | 22 | 30,070 |
| 3 | 46 | `full_arbiter_unreal1_pb_3_8_pe_` | 40 | 7 | 14,120 |
| 4 | 46 | `full_arbiter_unreal1_pb_4_6_pe_` | 52 | 9 | 25,968 |
| 5 | 46 | `full_arbiter_unreal2_pb_5_pe_` | 43 | 21 | 249,408 |
| 6 | 46 | `prioritized_arbiter_pb_7_pe_` | 47 | 43 | **30,100,344** |
| 7 | 46 | `rw_arbiter_pb_4_pe_` | 39 | 17 | 17,664 |
| 8 | 44 | `amba_case_study_pb_3_pe_` | 104 | 20 | 253,248 |
| 9 | 44 | `load_balancer_unreal1_pb_5_6_pe_` | 210 | 17 | 50,110 |
| 10 | 42 | `lift_gr1+_pb_3_pe_` | 270 | 136 | **87,281,993** |
| 11 | 42 | `lift_gr1_pb_3_pe_` | 225 | 91 | **115,278,358** |
| 12 | 41 | `robot_grid_pb_3_3_pe_` | **10,514** | 82 | 721 |
| 13 | 41 | `round_robin_arbiter_unreal1_pb_3_9_pe_` | 755 | 99 | 1,288 |
| 14 | 40 | `arbiter_with_buffer_pb_6_pe_` | 1,377 | 19 | 188,416 |
| 15 | 40 | `arbiter_with_cancel_pb_6_pe_` | 1,120 | 13 | 5,952 |
| 16 | 40 | `collector_v1_pb_11_pe_` | 15,372 | **15,371** | 90 |
| 17 | 40 | `collector_v3_pb_9_pe_` | 2,835 | 10 | 0 |
| 18 | 40 | `simple_arbiter_with_hints_pb_8_pe_` | 1,607 | 9 | 22,272 |
| 19 | 20 | `robot-to-target-charging3` (memory) | 99 | 20 | 1,558,304 |
| 20 | 19 | `Morning2_06e9cad4` (memory) | 198 | 183 | 0 |

Every row carries its solved boundary neighbour in
`benchmarking/syntcomp26-frontier-targets.tsv`.

## 6. What the mechanisms say

**Correction.** An earlier version of this section read `actions_seen` as the size of an
instance's semantic action table and concluded that a forward reachable solver was an
implausible answer for the "action explosion" families. That was wrong twice over.

`observe_action()` is called inside `cpre_inplace`'s loop over the action list, once per
action **per CPre call**, so `actions_seen` is cumulative backward work across every
fixed-point iteration — not a table size. A table of 115 million entries would be gigabytes;
what the number actually says is that the backward fixed point ground through an enormous
amount of work. That is a *symptom* of the backward search being large, which is evidence a
reachable forward search might help, not evidence against it.

The measurement settled it: `lift_gr1_pb_3_pe_` (115,278,358 `actions_seen`) and
`lift_gr1+_pb_3_pe_` (87,281,993) are both solved by the forward backend in 0.42 s and
2.42 s, having been unsolved by B and S at 60 s.

What the diagnostics do separate reliably:

- **Automaton size.** `robot_grid_pb_3_3` reaches 10,514 states with 721 actions per pass.
- **Rank dimension.** `collector_v1_pb_11` carries 15,371 numeric rank coordinates against 90
  actions per pass — the opposite shape.
- **Backward work.** `prioritized_arbiter_pb_7`, `lift_gr1_pb_3` and `lift_gr1+_pb_3` spend
  30.1M, 115.3M and 87.3M cumulative action applications. This measures how far the backward
  fixed point got, not how big the instance is.
- **Measured on the wrong workers.** `arbiter_on_inpchange_pb_5` appears small by every
  measure — 53 automaton states, 16 rank coordinates, 736 actions — and unsolved at 60 s. A
  later trace shows why that reading is wrong: those columns describe classic workers that
  finish in 47 ms reporting `unknown`, while the instance actually runs on the **equivariant
  solver**, reaching `equivariant-after-closure` with 5 clients, 10 blocks and 6 orbits. The
  equivariant fixed point emits no snapshots, so its cost is uninstrumented. Nothing here was
  measured about the computation that actually runs. See
  `frontiers/param-tlsf-arbiters_zoo-parametric-arbiter_on_inpchange.tlsf.md`.

The honest summary is that the diagnostics distinguish *shapes* of backward difficulty well,
and that predicting from them which shapes a forward solver can handle was premature. The
frozen set contains all of them, which is what allowed the prediction to be tested and
falsified rather than merely asserted.

## 7. The memory cohort, and why it needed a quota

All 22 memory-bounded instances are direct TLSF files with **no parameters**, so they cannot
earn the weights that dominate the ranking — minimal point of an exact parametric family, an
ordered solved neighbour, a family with ≥3 observed points — and cap near 14 against 40. They
were not penalised; the score could not see them. Left alone, the target set would have
contained none of the one failure mode a forward reachable solver is most likely to move: the
backward antichain exhausting 8 GiB rather than 60 s.

Quota slots are therefore reserved and filled round-robin across **distinct** families. Filling
by score alone put all four in `Morning`, which would have characterised one benchmark
generator rather than the failure mode.

## 8. Open questions per family

- Why does `arbiter_on_inpchange` stop at n=5 when every size measure stays small?
- Is `collector_v1`'s 15,371-coordinate rank vector genuinely necessary, or an artefact of the
  encoding?
- For the action-explosion families, is the semantic quotient already tight, or do 30–115
  million actions collapse further under a state-dependent minimisation (Stage F3)?
- Do the memory-bounded families have small reachable strategies despite an unrepresentable
  permissive region?

## 9. Reproduction

```sh
python3 benchmarking/syntcomp-corpus.py init
python3 benchmarking/syntcomp-corpus.py materialize --out tlsf-corpus
python3 benchmarking/family_metadata.py
python3 benchmarking/run-syntcomp26-coverage.py --bin <B> --solver-label B \
    --list tests/suites/benchmarks/syntcomp26/all.list \
    --tlsf-map tests/suites/benchmarks/syntcomp26/tlsf-sources.tsv \
    --tlsf-corpus tlsf-corpus --caps 1,5,17,60 --conflict-policy collect \
    --output benchmarking/_coverage26/B-runs.tsv
python3 benchmarking/adjudicate-status-conflicts.py --conflicts … --tlsf-corpus tlsf-corpus
python3 benchmarking/build-coverage-frontier.py --runs B=… --runs S=…
python3 benchmarking/select-frontier-targets.py
python3 benchmarking/run_diag_targets.py --build <diag> --tlsf-map … --tlsf-corpus … --csv …
python3 benchmarking/freeze-frontier-targets.py
```

Every table above is regenerable by these committed scripts.
