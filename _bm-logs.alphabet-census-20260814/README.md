# Step A alphabet census (2026-08-14)

This is the fixed-rule census from the alphabet-collapse plan.  It covers the
26 SYNTCOMP 2024 action-construction-bound targets and the 32 SYNTCOMP 2025
fixpoint-bound targets, with one REAL and one I/O-swapped UNREAL-formula child
per target.  Each target had a 120-second process-group budget; the enclosing
detached campaign had `MemoryMax=8G` and `MemorySwapMax=0`.

The diagnostics build was `build_best_decomp_mona_diag` at parent commit
`23d4089c` plus the uncommitted analysis instrumentation.  The census calls
the configured MONA I/O precomputer immediately after translation, emits the
BDD metrics, and exits before automaton preprocessing, action construction, or
fixpoint solving.  This makes the instrumentation analysis-only.

The two cohort definitions used by the runner are preserved as
`_bm-logs.gap-plan-20260804/syntcomp24-gap-phases.tsv` and
`_bm-logs.gap-plan-20260804/syntcomp25-gap-phases.tsv`; only rows classified
as `action-construction-bound` and `fixpoint-bound`, respectively, are used.

There are 101 completed orientation checkpoints and 202 input/output descent
ratios.  Fifteen orientations did not finish translation within 120 seconds:

- 2024: `Automata16S`, `FelixSpecFixed2_fa4d4ce3`,
  `FelixSpecFixed3_b0840146`, `amba_decomposed_lock15`,
  `amba_decomposed_lock16`, `arbiter_with_buffer6`, `full_arbiter_enc8`, and
  `simple_arbiter_with_hints10`;
- 2025: `LedMatrix`, `amba_case_study_unreal_pb_2_pe_`, `arbiter_pb_6_pe_`,
  `arbiter_with_buffer_pb_5_pe_`, `arbiter_with_cancel_pb_6_pe_`,
  `load_balancer_unreal1_pb_6_4_pe_`, and `robot-to-target0`.

The observed joint median `paths/nodes` ratio is 1.132585.  Even replacing
both descents of every missing orientation by positive infinity gives a
worst-case median of only 1.258698.  Both are below the plan's fixed 4.0
threshold, so the recorded decision is `STOP BEFORE STEP B`.

Files:

- `syntcomp24-action-construction.csv` and `syntcomp25-fixpoint.csv`: compact
  per-child diagnostic checkpoints and timeout state;
- `descents.tsv`: the 202 scored input/output ratios;
- `summary.txt`: counts, histogram, robust median bound, and decision;
- `status.txt`: detached campaign terminal status.
