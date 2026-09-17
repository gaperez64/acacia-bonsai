# Reuse ledger — symbolic rows / selective sparse-real sprint

Per package, against the pinned production baseline `7e6e932a8c44a3e263ddde9ebd3c6e60ea1e0aab`
and the research recovery points `c1f60fffebba7a49c7ff1c8642118a35ddbd3556` (provider
core) / `64b961f049a42cbc51504897e29137b63078298c` (worker/replay integration).

## R0 — recovery

| Item | Status | Reason |
| --- | --- | --- |
| `src/solver/closure_buchi_provider.{hh,cc}`, `tests/closure_buchi_provider_test.cc`, provider `meson.build` entries | **reuse** | Cherry-picked whole from `c1f60fff` (`git apply --3way`); this commit is self-contained, no P0/P1a/P3a dependency at all |
| `benchmarking/demand-sparse-20260916/closure-buchi-provider.md` | **reuse (relocated)** | Moved to `benchmarking/symbolic-rows-20260917/closure-buchi-provider.md`; provider design doc, not sprint-specific evidence |
| `src/research/spot_provider_replay.cc`, its tests, `benchmarking/spot-provider-replay.py`, `game_backend.hh`, `arg_parser.hh`, `config/derived_gates.hh`, build registration | **reuse** | `git apply --3way` of the integration-only diff (`64b961f0` vs its direct parent `f4c95a9f`) applied cleanly onto pinned master for every file except the three below |
| `src/solver/solver_invoker.cc`, `src/solver/spot_lazy_worker.hh` | **incompatible (hand-ported)** | The 3-way merge's recorded base already included P1a's `LossCheckPolicy`/`solve_for_schedule`/`SchedulingAction` restructuring, which clean master never had; ported the true delta (provider dispatch, `capture_boundary`, `make_closure_store`) by hand onto master's actual (simpler) call signatures and K-loop, dropping every loss-check-policy reference |
| `tests/check-spot-worker.py` | **incompatible (hand-ported)** | Same cause, plus P0's `record_version`/`worker_id`/history-JSONL fields baked into the recorded base; narrowed the closure-provider assertions to fields the ported code actually emits, verified against a real build (see the R0 commit) |
| `4a04e882` (P0 lifecycle records), `52d4c8e5` (P3a move compaction), `d14cf955` (P1a scheduling hints) | **excluded** | Per plan §2.2; none of their code, options, presets or tests exist on this branch |

## A1 — Boolean-guard folding

New work; nothing to reuse from a prior branch (no such mechanism existed
before this sprint). Reused the existing `RowBudget::guard`/`hit` checked
BDD boundary, the existing `literals` cache storage (generalized rather than
duplicated), and the existing `closure_buchi_provider_test.cc` fixtures/
helpers (`make`, `value`, `signature`, `Hooks`, `Alphabet`/`accepts`).

## A2 — first-row diagnosis

| Item | Status | Reason |
| --- | --- | --- |
| P2 target list (22 IDs) | **reuse** | `_bm-logs.20260916-demand-sparse/targets/p2.list`, hash-verified identical to the previous sprint's frozen list |
| Transformed worker formulas/inputs/outputs/partition | **reuse** | Read from the previous sprint's captured worker-record JSON (`diag-p2mech/wrec/p2mech-solo-unreal-closure/`), not reconstructed from filenames |
| `run-syntcomp26-coverage.py`, `paired-admission.py`, `cactus-report.py` | **not yet used** | A2 needed only per-job row status, not a coverage/timing campaign; deferred to B/C |
| Replay tool's "stop after a verdict" behavior | **missing a first-row-only mode** | Added `--stop-after-first-row` (§5.1 explicitly permits this); also had to fix the parent process's partial/censored-row fallback, which only recognized `stage=="attempt_complete"` as clean and printed a spurious duplicate row for the new terminal stage |
| Replaying a formula near/above ~128 KiB | **missing** | The kernel's per-argument length limit made `--formula <huge string>` fail for `follow0.ltl`, `ltl2dba_C2_unreal_pb_100_pe_.ltl`'s first component (240 KiB / 134 KiB); added `--formula-file` |

No second runner, event system, PAR-2 calculator, chart generator, formula
parser or TLSF lowering pass was created. `run-a2-first-row.py` is a thin
driver over the existing replay binary's CLI and TSV output; it does not
duplicate any of the tools listed above.
