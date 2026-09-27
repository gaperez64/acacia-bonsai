# Sprint decisions — optimization first, lean repository, then UNREAL lifting (2026-09-27)

This is the single active record for the sprint whose plan is `plan.md`. It supersedes the
follow-up handoffs in `../gr1-par2-20260923/`. Agent briefs and review rounds are kept outside Git
(`build_scratch/optimize-20260927/`); only decisions and their evidence pointers are recorded here.

## 2026-09-27 — Kickoff facts

- **Branch.** `sprint/optimize-20260927`, stacked on PR #192
  (`sprint/gr1-par2-20260923` at `26cbb45f`), which stays open and unmerged. The stack is
  #190 → #191 → #192.
- **tlsf-tools.** At `a294419` (main, #41 merged). OxiDD is `be2f69b` plus the patch submitted
  upstream as OxiDD#49, which is still open.
- **E5 (evaluation invocation, not a shipped default).**
  - Arms, in this order: `real:small:backward`, `real:small:forward`,
    `unreal:formula:spot-guarded-sparse`, `unreal:automaton:forward`, `real:gr1:oxidd`.
  - Frozen binary `build_final_f74ad9d1/src/acacia-bonsai`, SHA-256 `65530fb4…c462`.
  - No completed full-corpus E5 result exists. The 1,238 figure is a virtual best of isolated
    60 s legs.
- **S4.** The shipping `docker_default` group, unchanged.
- **Open issues in scope:** #194 (optimise native arms), #195 (UNREAL lifting), #196 (sparse
  backward).
