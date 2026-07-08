# Acacia-Bonsai Optimization Follow-up Sprint

Current branch: `spot-fastpath-no-tlsf-tools`

Source plan: `/home/gperez/Downloads/acacia-bonsai-optimization-followup-plan.md`

## Rules

- Use local Meson LTL suites first. TLSF walks are not part of the default tuning loop.
- Use cgrouped benchmark runs for memory control.
- Do not run multiple compilation jobs at once.
- Use `meson test --no-rebuild` for benchmark timing runs.
- Compare against measured top Acacia configurations plus local `ltlsynt`, not only one favorite config.

## Implemented So Far

- Added benchmark metadata sidecars under `_bm-logs/*.meta`.
- Added local pseudo-config support for `ltlsynt` ablations.
- Added slice-specific benchmark markers so full-slice reruns do not get skipped by old marker files.
- Added stale JSON cleanup before timing runs.
- Added diagnostics infrastructure behind `ACACIA_ENABLE_DIAGNOSTICS`.
- Added diagnostic timers/counters for simplification, translation, automaton size, preprocessing, fast-path classification, solve time, loop counts, and max antichain size.
- Added finer Spot NBA fast-path classes: deterministic, GFG, non-GFG, GFG disabled, GFG budget declined, unsupported.
- Merged remaining `better-preprocessing` work except the `Small` translator default:
  - Docker uses `tlsf-tools` instead of `syfco`.
  - Docker release platform is linux/amd64 only.
  - `tlsf-tools` release is pinned to v1.3.
  - Docker installs `mealy2moore`.
  - TLSF wrapper always asks `tlsf2ltl` for `--overwrite-target Mealy`.
  - Synthesis wrapper converts synthesized AAG with `mealy2moore` only when the original TLSF target is Moore.
  - Explicit `spot::sbacc()` normalization is applied after translation and after `push_aps`.

## Completed Local Benchmark Runs

Suite: `ab/syntcomp24/0s-1s`

Timeout factor: `1.7`

Run mode: cgrouped, `meson test --no-rebuild --benchmark --slice=1/1`

| Config | Rows | Timeouts | Failures | Sum Duration |
| --- | ---: | ---: | ---: | ---: |
| `ltlsynt` | 994 | 82 | 0 | 1666.51s |
| `best_decomp_mona` | 994 | 0 | 0 | 9.35s invalid |
| `best_mona` | 994 | 121 | 0 | 2443.63s |
| `base_iosprecom_mona` | 994 | 253 | 0 | 4780.72s |
| `best_decomp_kdtree_mona` | 994 | 254 | 0 | 5027.97s |

Interrupted run:

- `best_decomp_sharingtrie_mona` was stopped before completion, around the FelixSpec/finding-nemo region.

The `base_iosprecom_mona`, `best_decomp_kdtree_mona`, and
`best_decomp_sharingtrie_mona` entries above came from pre-metadata
`--slice=1/1` artifacts. Treat them as historical context until the current
four-slice solver-cgroup reruns complete.

## `best_decomp_mona` Investigation

The `best_decomp_mona` result is invalid. It did not run Acacia.

Root cause:

- `_bm-logs/best_decomp_mona-slice-1-of-1.json` has 994 entries whose stderr says `build_best_decomp_mona/src/acacia-bonsai` does not exist.
- The test stdout contains `FAILED: PASS.` because `tests/check-real-correct.sh.in` did not set `ret` for Acacia infrastructure errors on unlabeled benchmarks.
- `self-benchmark.sh` trusted the stale `build_best_decomp_mona/compiled` marker before verifying that the executable existed.

Fixes applied:

- The test wrapper now checks that `ltlsynt` and Acacia executables exist before running them.
- Acacia return codes greater than 2 now set a failing return value even when the benchmark is unlabeled.
- The benchmark driver now validates that a non-tool config has an executable before honoring `compiled` or `benchmarked` markers.
- External tool benchmark host-build selection now ignores Acacia builds with stale `compiled` markers and missing executables.
- The benchmark driver treats any `FAILED:` line in the Meson log as a failed benchmark run, catching older generated wrappers too.

The current `best_decomp_mona` JSON/log/marker artifacts must not be used for ranking. Rebuild/recompile that config and rerun it under the cgrouped benchmark protocol before comparing against `ltlsynt`.

Follow-up:

- `best_decomp_mona` was rebuilt from a clean Meson-option configuration, with matching `.acacia-config.json` metadata.
- Two full `--slice=1/1` reruns made real progress but received SIGTERM around test 755/994, so the full-run log is still not a valid complete result.
- Continue this config in smaller Meson slices and aggregate the resulting JSON files instead of relying on one long runner process.
- Slices `1/4` and `2/4` completed with real executions. Detached `3/4` reruns under an outer 8G systemd cgroup were killed by the OOM controller around test 189/248.
- The benchmark harness now passes `--num-processes "$BENCHMARK_TEST_JOBS"` to Meson and defaults `BENCHMARK_TEST_JOBS=1`, so sliced runs do not multiply memory pressure across Meson tests.
- Meson-level cgrouping is still too coarse for memory-heavy instances: when one solver execution exhausts the cgroup, the OOM controller can kill the whole benchmark harness. `BENCHMARK_CGROUP_SCOPE=solver` is now the default, routing each solver invocation through its own `systemd-run --scope` while keeping `BENCHMARK_CGROUP_SCOPE=meson` available for the old behavior.
- The benchmark harness now defaults `BENCHMARK_COMPILE_JOBS=1` as well as `BENCHMARK_TEST_JOBS=1`, so rebuilding optimized presets does not accidentally start a high-parallelism compile/link job.
- `best_decomp_mona` has now completed all four `--slice=N/4` shards under `BENCHMARK_CGROUP_SCOPE=solver`, `BENCHMARK_TEST_JOBS=1`, 8G memory, no swap:
  - rows: 994
  - OK: 824
  - timeouts: 170
  - failures: 0
  - resource-limit UNKNOWNs: 0
  - summed Meson durations: 3311.36s

## Overnight/Resume Benchmark Status

`overnight-20260706` ran as one sequential `systemd-run --user` service.

- `best_mona` completed all four solver-cgroup shards:
  - rows: 994
  - OK: 817
  - timeouts: 177
  - failures: 0
  - summed Meson durations: 3429.84s
- The ltlsynt ablations completed all four shards:
  - `ltlsynt_no_decompose`: 906 OK, 88 timeouts, 1796.21s summed duration
  - `ltlsynt_no_bypass`: 892 OK, 102 timeouts, 2041.37s summed duration
  - `ltlsynt_no_obligation`: 918 OK, 76 timeouts, 1593.82s summed duration
  - `ltlsynt_no_specials`: 890 OK, 104 timeouts, 2084.01s summed duration
- The first attempt to rebuild the remaining Acacia top configs failed because
  the detached service lacked `PKG_CONFIG_PATH=/usr/local/lib/pkgconfig`, so
  Meson could not find Spot. `scripts/overnight-benchmark-session.sh` now adds
  that path automatically when present.
- `resume-topconfigs-20260707` is the current sequential resume service for:
  - `base_iosprecom_mona`
  - `best_decomp_kdtree_mona`
  - `best_decomp_sharingtrie_mona`
- Current `base_iosprecom_mona` four-slice rerun completed:
  - rows: 994
  - OK: 810
  - timeouts: 184
  - failures: 0
  - summed Meson durations: 3564.23s
- Current `best_decomp_kdtree_mona` four-slice rerun completed:
  - rows: 994
  - OK: 816
  - timeouts: 178
  - failures: 0
  - summed Meson durations: 3429.50s
- Current `best_decomp_sharingtrie_mona` four-slice rerun completed:
  - rows: 994
  - OK: 825
  - timeouts: 169
  - failures: 0
  - summed Meson durations: 3524.41s

Current ranking from complete current/safe aggregates
(`_bm-logs/aggregated-20260707-current`, PAR-2 cap 17s):

| Rank | Config | Solved | Timeouts | OK Time | PAR-2 |
| ---: | --- | ---: | ---: | ---: | ---: |
| 1 | `ltlsynt_no_obligation` | 918/994 | 76 | 298.1s | 2882.1s |
| 2 | `ltlsynt` | 912/994 | 82 | 268.9s | 3056.9s |
| 3 | `ltlsynt_no_decompose` | 906/994 | 88 | 295.6s | 3287.6s |
| 4 | `ltlsynt_no_bypass` | 892/994 | 102 | 301.4s | 3769.4s |
| 5 | `ltlsynt_no_specials` | 890/994 | 104 | 309.9s | 3845.9s |
| 6 | `best_decomp_mona` | 824/994 | 170 | 411.8s | 6191.8s |
| 7 | `best_decomp_sharingtrie_mona` | 825/994 | 169 | 636.8s | 6382.8s |
| 8 | `best_mona` | 817/994 | 177 | 410.7s | 6428.7s |
| 9 | `best_decomp_kdtree_mona` | 816/994 | 178 | 391.7s | 6443.7s |
| 10 | `base_iosprecom_mona` | 810/994 | 184 | 425.9s | 6681.9s |

Loss/slow-set artifacts generated:

- `_bm-logs/best_decomp_mona-vs-ltlsynt-loss-set.csv`
- `_bm-logs/best_mona-vs-ltlsynt-loss-set.csv`
- `_bm-logs/current-best_decomp_mona-vs-ltlsynt-loss-set.csv`
- `_bm-logs/current-best_decomp_sharingtrie_mona-vs-ltlsynt-loss-set.csv`
- `_bm-logs/best_decomp_mona-ltlsynt-ablation-report.csv`
- `_bm-logs/best_mona-ltlsynt-ablation-report.csv`
- `_bm-logs/current-best_decomp_mona-ltlsynt-ablation-report.csv`
- `_bm-logs/current-best_decomp_sharingtrie_mona-ltlsynt-ablation-report.csv`

For `best_decomp_mona` vs `ltlsynt`:

- `ltlsynt_only`: 114 instances
- `acacia_only`: 26 instances
- `acacia_slow`: 44 instances
- Most `ltlsynt_only` and `acacia_slow` rows remain solved by all feature
  ablations, so they are not explained by one removed ltlsynt shortcut.
- High-signal diagnostic targets:
  - loss set, ltlsynt solves instantly and all ablations still solve:
    `ltl2dba_E8.ltl`, `ltl2dba_Q6.ltl`, `ltl2dba_Q7.ltl`,
    `ltl2dba_Q8.ltl`, `ltl2dba_E10.ltl`, `ltl2dba_C216.ltl`,
    `ltl2dba_beta10.ltl`
  - slow set, largest absolute Acacia-vs-ltlsynt gaps:
    `ltl2dba_beta6.ltl`, `07.ltl`, `06.ltl`,
    `amba_decomposed_lock14.ltl`, `robot_grid7_7.ltl`, `mux16.ltl`,
    `robot_grid6_6.ltl`, `detector_unreal15.ltl`
  - Acacia-only wins worth preserving:
    `generalized_buffer3.ltl`, `collector_v37.ltl`, `collector_v211.ltl`,
    `LightsTotal_9cbf2546.ltl`, `LightsTotal_06e9cad4.ltl`,
    `ltl2dba_R6.ltl`

Focused diagnostics artifact:

- `_bm-logs/best_decomp_mona_diag-targets.csv`

Diagnostic takeaways for `best_decomp_mona_diag`:

- `ltl2dba_E8.ltl`, `ltl2dba_Q6.ltl`, `ltl2dba_Q8.ltl`, and
  `ltl2dba_beta10.ltl` hit the 25s wrapper timeout without an `ACACIA_DIAG`
  line, so the current end-of-scope diagnostics are insufficient for
  classifying these complete losses.
- `07.ltl` and `06.ltl` spend about 6.7s in translation and 7.6-9.3s in the
  fixpoint solve on a 13609-state/155315-edge automaton.
- `ltl2dba_beta6.ltl` spends most time in fixpoint solving: 17.5s solve time,
  489 loops, max antichain size 896.
- `amba_decomposed_lock14.ltl` is also solve-bound: 10.4s solve time after a
  tiny 5-state automaton.
- `detector_unreal15.ltl` is translation-heavy: 5.4s translation, 1.5s solve.
- `mux16.ltl` goes through the Spot deterministic fast path, with the winning
  branch spending about 7.3s in the fast solver.
- Fast Acacia-only examples such as `generalized_buffer3.ltl`,
  `collector_v37.ltl`, `ltl2dba_R6.ltl`, and `Alarm_70523fbe.ltl` are short
  fixed-point runs; preserve these when changing preprocessing or solver gates.

## Remaining Sprint Work

Completed in this pass:

1. Resolved the `best_decomp_mona` timing anomaly: the original 9.35s
   result was invalid due to a stale missing binary and a wrapper bug.
2. Rebuilt/reran the top Acacia baselines under solver-scoped cgroups.
3. Completed the measured local LTL ranking for the current trusted configs.
4. Ran the `ltlsynt` ablations on the same suite:
   - `ltlsynt_no_decompose`
   - `ltlsynt_no_bypass`
   - `ltlsynt_no_obligation`
   - `ltlsynt_no_specials`
5. Produced loss/slow sets against `ltlsynt` for current best Acacia configs.
6. Ran focused diagnostics on selected loss/slow/win instances.
7. Ran validation:
   - `meson test -C build_best_decomp_mona --no-rebuild --suite unit --num-processes 1 -t 4`
   - `zsh -n self-benchmark.sh tests/check-real-correct.sh.in`
   - `bash -n scripts/overnight-benchmark-session.sh`
   - `python3 -m py_compile` on the benchmark analysis scripts

Additional completed items:

8. Added explicit configuration booleans and presets for the symmetry gates:
   - `best_decomp_mona_equivariant`
   - `best_decomp_mona_symmetric`
   Defaults remain off.
9. Completed the remaining variant comparison under the same 4-slice,
   8G/no-swap, solver-scoped cgroup protocol:

   | Rank | Config | Solved | Timeouts | OK Time | PAR-2 |
   | ---: | --- | ---: | ---: | ---: | ---: |
   | 6 | `best_decomp_mona` | 824/994 | 170 | 411.8s | 6191.8s |
   | 7 | `best_decomp_mona_equivariant` | 823/994 | 171 | 406.6s | 6220.6s |
   | 8 | `best_decomp_mona_spotfast_det_and_gfg` | 824/994 | 170 | 472.5s | 6252.5s |
   | 12 | `best_decomp_mona_elevator` | 813/994 | 181 | 430.6s | 6584.6s |

   Decision: keep `best_decomp_mona` as the best Acacia default.  Exact
   equivariant loses only `ltl2dba_beta6.ltl` relative to the baseline and is
   probably timeout-threshold sensitive, but it is not a net win.  GFG solves
   the same count but adds about 60.7s OK time.  Elevator is clearly worse on
   this suite, with 14 baseline-only losses and only 3 variant-only wins.
10. Added timeout-resilient diagnostics:
    - phase checkpoints: `after-rsimp`, `after-decomposition`,
      `after-translation`, `after-input-push`, `after-spot-fast`,
      `after-preprocessing`, `before-solve`
    - periodic solve-loop checkpoints controlled by
      `ACACIA_DIAG_PROGRESS_EVERY` (default 128, `0` disables)
    - atomic single-syscall diagnostic-line emission to avoid interleaving
      when real/unreal children write concurrently
11. Updated `benchmarking/run_diag_targets.py` to run the diagnostics binary
    directly by default, keeping wrapper mode available with `--via-wrapper`.
    Direct mode preserves diagnostics under process timeout.
12. Verified timeout diagnostics with:
    - `_bm-logs/best_decomp_mona_diag-progress-targets.csv`
    - four timeout-killed targets produced 69 parseable progress rows in total

Final validation in this pass:

- `python3 scripts/acacia-config.py validate`
- `python3 -m py_compile` on benchmark/config helper scripts
- `bash -n scripts/overnight-benchmark-session.sh`
- `zsh -n self-benchmark.sh tests/check-real-correct.sh.in`
- `meson compile -C build_best_decomp_mona_diag -j 1` under 12G/no-swap
- `meson compile -C build_best_decomp_mona -j 1` under 12G/no-swap
- `meson test -C build_best_decomp_mona --no-rebuild --suite unit --num-processes 1 -t 4`

Still open:

1. Commit/push once the desired commit boundary is confirmed.
