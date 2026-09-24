# Brief P5-acacia — decision-only REAL route without global Skolemization

Read plan.md §2, §8 (all), §5.4; tlsf-tools docs/gr1-region-method.md (in
/home/gperez/GIT-repos/tlsf-tools at 9212e2b) which defines `tlsfcertcheck --method region`
(REGION_VERIFIED; never UNREALIZABLE; move_* untrusted and not compiled). Frozen build with the
method: /home/gperez/GIT-repos/tlsf-tools/build-P5-9212e2b (read-only). The P2c orchestration is
committed just before this task. NEVER stage or commit by any path (including /usr/bin/git).
Goal: for REAL decision requests (not synthesis), the generalizer emits the target certificate
(region, goals, rank layers) WITHOUT constructing/Skolemizing/exporting a global policy, and the
target is checked with `--method region`; a REGION_VERIFIED result is a REALIZABLE answer for the
actual requested target. Keep the policy route intact for synthesis requests and as a selectable
alternative (a --real-check {policy,region} option; default stays `policy` until I measure;
evidence records the method and result string). The region route must:
- never produce UNREAL; UNREAL stays with exact environment certificates only;
- require the tool configuration's checker to support --method region (probe detects it; if the
  configured checker lacks it, the region option is a configuration error, not a silent fallback);
- keep the certificate artifact complete and hash-bound (P2c bundle identity) — only the policy
  artifact is omitted; the checker receives the game and certificate it validates;
- skip exactly the policy-construction/Skolemization/export stages (diagnostics show them absent).
Tests: on the generalizer suite instances both routes give REALIZABLE with verified certificates;
region route never invokes Skolemization (diagnostic count 0); mutated certificate (drop a rank
layer; corrupt region) → not REALIZABLE; UNREAL instances unaffected; probe/config error path.
Machine rules: one job; scratch build_scratch/p5a/; MACHINE MAY BE RUNNING TIMED MEASUREMENTS —
before running test_generalize_gr1.py or heavy work, wait until build_scratch/seq5/progress.txt is
absent or has a line starting "DONE" (poll every 60 s). Full suites + ruff. VERDICT at the end.
