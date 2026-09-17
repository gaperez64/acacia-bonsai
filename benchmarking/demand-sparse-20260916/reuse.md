# P0 reuse inventory — 16 September 2026

P0 only, inspected at `6410fd3417f5c05f901ed6afc5094947103c157d`. No benchmark, profiling run, solver campaign, preset change, or P1–P5 implementation was performed. Line anchors and `git hash-object` values below describe this working tree after the lifecycle repair; `baseline_git_blob` in the manifest also preserves the original source identity. An absent future file has no hash/line, rather than a fabricated identity.

The existing `freeze-baseline-manifest.py` schema does **not** fit this ledger: it identifies binaries through build directories and does not represent the retained external binary, linked-library identities, arm inventory, historical observations and reuse decisions together. A small read-only collection script produced this manifest using SHA-256, `git hash-object`, `ldd`, and source/TSV validation. Directory hashes below are SHA-256 of sorted `relative-path<TAB>file-sha256<LF>` entries; they are not file hashes.

## Package inventory

| Package | State | Scope / reason |
|---|---|---|
| P0 | modify | Reuse sink/reporters and evidence; repair only missing lifecycle fields/boundaries and consumer assumptions. |
| P1a | modify | Exact checkers already exist; scheduling-only loss hint result/API is absent and left for P1a. |
| P1b | modify | Frozen RowStore still lives per K; lazy retention already exists and stays intact. |
| P2 | new | closure-buchi is absent; adapt existing RowStore/factory/replay later, preserving shared engine/checkers. |
| P3a | modify | Deep-copy survivor compaction is present; move patch deliberately deferred to its own task. |
| P3b / P3c / P3d | deferred | No sampled sparse CPU attribution or required live-generator/duplicate-byte denominator. |
| P4 | deferred | One proposed four-arm screen from existing evidence; no configuration admitted here. |
| P5 | modify | Existing harness and plotter reused; missing columns/adapter validation remain for P5. |

## Source inventory (covers plan §1.1 and §2)

| Packages | State | Symbol / file:line | git hash-object | Reason |
|---|---|---|---|---|
| P0 | modify | [src/solver/spot_worker_record.hh:33](../../src/solver/spot_worker_record.hh#L33): `class Record {` | `b24ab9139744c7dfd1e534d3af7d737b0ca4bfd0` | Extend the existing atomic snapshot/history sink with attempt resets, end evidence and fallback segments; no new sink. |
| P0 | modify | [src/solver/diagnostics.hh:1013](../../src/solver/diagnostics.hh#L1013): `inline bool finish` | `816dba2edeaf5391b83853c20783346a92849c99` | Publish observed worker result through the existing finish boundary; retain monotonic timers and scoped diagnostics. |
| P0, P2 | modify | [src/solver/solver_invoker.cc:432](../../src/solver/solver_invoker.cc#L432): `acacia::spot_records::Record capture` | `5e172276288e4d449d65d57ebf9854f3f4818f57` | Mark transformed worker, preprocessing and Boolean discovery; P2 can select its factory here later. |
| P0, P1a, P1b | modify | [src/solver/solve_game_impl.hh:203](../../src/solver/solve_game_impl.hh#L203): `for (long long k = kmin` | `99bcfa216cc5713d16f118a424fd7c338f29943a` | P0 marks attempts only; frozen sparse RowStore remains inside this K loop for P1b. |
| P0, P1a, P2 | modify | [src/solver/spot_lazy_worker.hh:88](../../src/solver/spot_lazy_worker.hh#L88): `game::RowStore store` | `398547b5380cc1fbae1acbb6644fafb0bb5f7bdf` | P0 repairs attempt reporting; immutable RowStore retention already exists outside K. |
| P1a, P2 | already present | [src/solver/spot_lazy_game.hh:1032](../../src/solver/spot_lazy_game.hh#L1032): `class Search {` | `b4f93020aa5f565f771783746874bb8abf7546b5` | Shared exact sparse engine and independent winning/losing checkers remain authoritative. |
| P2 | modify | [src/solver/spot_lazy_game.hh:121](../../src/solver/spot_lazy_game.hh#L121): `class RowStore {` | `b4f93020aa5f565f771783746874bb8abf7546b5` | Current constructors accept FrozenAcacia or LazyBuchiView, not a generic twa; later add one generic path. |
| P2 | already present | [src/solver/spot_lazy_game.hh:298](../../src/solver/spot_lazy_game.hh#L298): `class Reader {` | `b4f93020aa5f565f771783746874bb8abf7546b5` | Reuse verifier reconstruction and current sparse Reader. |
| P2 | already present | [src/solver/spot_lazy_game.hh:360](../../src/solver/spot_lazy_game.hh#L360): `class Oracle {` | `b4f93020aa5f565f771783746874bb8abf7546b5` | Reuse guarded predicates and current limits. |
| P2 | already present | [src/solver/spot_lazy_game.hh:250](../../src/solver/spot_lazy_game.hh#L250): `class CertificateSource final` | `b4f93020aa5f565f771783746874bb8abf7546b5` | Reuse independent logical row reconstruction with retained provider ownership. |
| P3a | modify | [src/solver/spot_lazy_game.hh:669](../../src/solver/spot_lazy_game.hh#L669): `generators_[write] = generators_[read]` | `b4f93020aa5f565f771783746874bb8abf7546b5` | Deep-copy compaction remains; the move change is a separate package. |
| P3b | deferred | [src/solver/spot_lazy_game.hh:1278](../../src/solver/spot_lazy_game.hh#L1278): `++nodes_checked_` | `b4f93020aa5f565f771783746874bb8abf7546b5` | Broad node scan exists; sampled CPU gate is unevaluated. |
| P3c | deferred | [src/solver/spot_lazy_game.hh:624](../../src/solver/spot_lazy_game.hh#L624): `class LossSet {` | `b4f93020aa5f565f771783746874bb8abf7546b5` | No sampled >=10% CPU plus >=128-live-generator query evidence; retain scalar exact filtering. |
| P3d | deferred | [src/solver/sparse_forward_rank.hh:107](../../src/solver/sparse_forward_rank.hh#L107): `std::vector<Entry> entries_` | `6977c71469ddf410656a9f985da1a8b5e1d36448` | Owning vectors remain; allocation/copy and duplicate-byte gates are unevaluated. |
| P1b, P2, P3a, P3b, P3c, P3d | already present | [src/solver/sparse_forward_rank.hh:17](../../src/solver/sparse_forward_rank.hh#L17): `class SparseForwardRank` | `6977c71469ddf410656a9f985da1a8b5e1d36448` | Reuse exact sparse support/hash/mass/order; absent coordinates remain -1. |
| P1b, P2 | already present | [src/solver/spot_rows.hh:32](../../src/solver/spot_rows.hh#L32): `struct GenericTransitionBuchi` | `ed440f94088fb127bbae82fea63edb6425ea309f` | Underlying SpotRows already has generic transition-Buchi support, stable IDs and transactional rows. |
| P0 | modify | [src/solver/forward_k_bounded_safety_aut.hh:53](../../src/solver/forward_k_bounded_safety_aut.hh#L53): `std::optional<std::pair<VECTOR_ELT_T, SetOfStates>> solve` | `102b64b1af6e5e65ea8a5ed1a5c275ce8036cc7b` | Mark shared action construction, K search, verification and attempt outcome. |
| P0 | modify | [src/solver/k_bounded_safety_aut.hh:150](../../src/solver/k_bounded_safety_aut.hh#L150): `std::optional<std::pair<VECTOR_ELT_T, SetOfStates>> solve` | `aee9871c2e638d09046c5c18dcb5b034ae7dbdbc` | Mark backward action/search/K boundaries without calling fixed-point evidence an independent certificate check. |
| P0 | modify | [src/solver/spot_guarded_forward_safety.hh:246](../../src/solver/spot_guarded_forward_safety.hh#L246): `SolveResult solve` | `0dc656ee3fb54cc2dbd4a25015aab28eef934c73` | Flush dense guarded verification entry before the checker can be interrupted. |
| P1a | modify | [src/solver/k_schedule.hh:11](../../src/solver/k_schedule.hh#L11): `struct loss_evidence` | `3fe1840038ca6f72d16df9c87ca51ab7fbc26e14` | have_certificate is separate from LOSE_K; future scheduling hints must leave it false. |
| P2 | modify | [src/solver/game_backend.hh:19](../../src/solver/game_backend.hh#L19): `enum class automaton_provider` | `5b95bd8a32919ab75f7218afea21c4bebeb1e80d` | Typed provider parser exists; closure-buchi is absent. |
| P2 | modify | [src/portfolio_arm.hh:138](../../src/portfolio_arm.hh#L138): `if (third_colon != std::string::npos)` | `883c47782854e85c8214938ea93242708fa4ee73` | Fourth provider field already parses; actual path is src/portfolio_arm.hh, not src/solver/. |
| P2 | modify | [src/arg_parser.hh:689](../../src/arg_parser.hh#L689): `if (arm.provider != acacia::automaton_provider::frozen_graph)` | `cb6d71df052e073d2eb2ce4e811bfd487995d0e2` | Provider compile gate and backend/polarity validation already exist at src/arg_parser.hh. |
| P2 | modify | [src/research/spot_provider_replay.cc:255](../../src/research/spot_provider_replay.cc#L255): `void usage` | `3afb66674cf6d0b7deed34ca355da1a87809fca0` | Reuse C4/C5 eager/lazy, transformed formula/partition, limits and repeated-K loop; selector/export missing. |
| P3b, P3c, P3d | deferred | [src/research/antichain_replay.cc:73](../../src/research/antichain_replay.cc#L73): `void usage` | `bb0beb95bf86ca09b4a4d337396bbd42fa2917a3` | Dense snapshot replay exists; no sparse trace mode; extend only after a gate fires. |
| P2 | already present | [tests/spot_provider_replay_test.cc:1542](../../tests/spot_provider_replay_test.cc#L1542): `int main` | `cf36e4a3bf329acbd8d8d09c0ad73caf06a263e5` | Reuse exact/cross-engine and certificate mutation tests, compiled in multiple policy variants. |
| P3a, P3b, P3c, P3d | already present | [tests/sparse_forward_rank_test.cc:179](../../tests/sparse_forward_rank_test.cc#L179): `int main` | `694538eecf2f936dba677eea403e6908eacdc453` | Reuse normalization, growth-stable hashing, mass and exact sparse/dense order checks. |
| P2 | already present | [tests/spot_lazy_buchi_view_test.cc:117](../../tests/spot_lazy_buchi_view_test.cc#L117): `void language_checks` | `0c69790c1d1241afbb805827ffbc9def843046e7` | Reuse complete materialization and Spot are_equivalent language cross-checks. |
| P2 | already present | [benchmarking/spot-provider-replay.py:65](../../benchmarking/spot-provider-replay.py#L65): `def equivalence` | `d180f5b3f50ca62a3f2ec838b9e18a00437d9620` | Reuse C4/C5 identity-aware fixed-K comparison; it is not a translator language proof. |
| P0 | modify | [benchmarking/summarize-worker-phases.py:164](../../benchmarking/summarize-worker-phases.py#L164): `def load_workers` | `6c0e76a7723d296314f91cef5e823137ab8e4f48` | Remove fixed cohort/single sparse arm assumptions; preserve censoring and recover only complete history prefixes. |
| P0 | modify | [tests/diagnostics_attempt_test.cc:21](../../tests/diagnostics_attempt_test.cc#L21): `void worker_records` | `2feb9f94b875efbcd0a92630e90c344dcebb9ae8` | Extend existing diagnostic fixture with two attempts, fallback, exceptions, real parent kill and nested accounting. |
| P0 | modify | [tests/pytest/test_summarize_diag_phases.py:157](../../tests/pytest/test_summarize_diag_phases.py#L157): `def load_worker_module` | `a0bfa36eb0bc46d0324d2b88834a203d93b3386b` | Extend phase fixtures for legacy, cancellation, truncation and arbitrary cohorts/arm counts. |
| P0 | modify | [tests/check-spot-worker.py:28](../../tests/check-spot-worker.py#L28): `cases =` | `3f470098f15c1b7729d613b1d005d70a950a11f2` | Extend native TLSF live worker fixture with lifecycle/history and inclusive row-time assertions. |
| P0, P5 | already present | [benchmarking/run-syntcomp26-coverage.py:51](../../benchmarking/run-syntcomp26-coverage.py#L51): `OUTPUT_COLUMNS` | `0178463fdce6781969b9f7e6eaf0dbc604fcdb40` | Reuse resume, durable results, full-invocation limits, CPU quota/cpuset checks; no runner changes. |
| P0, P5 | already present | [benchmarking/benchlib.py:512](../../benchmarking/benchlib.py#L512): `def run_systemd_scope` | `818846658d32dae3c45e7fff9bb1e3dc7510d4c2` | Reuse common bounded execution/normalization/PAR2; no execution of campaigns in P0. |
| P5 | modify | [benchmarking/cactus-report.py:183](../../benchmarking/cactus-report.py#L183): `def render_markdown` | `1189a10e239b7d4f890d074507b084b1da9b1746` | Existing PAR2 total/equal-set checks/PNG/PDF; later add missing summary fields, untouched in P0. |
| P0 | already present | [benchmarking/freeze-baseline-manifest.py:156](../../benchmarking/freeze-baseline-manifest.py#L156): `def build_metadata` | `2ce5165dd281e9907335597ac33481da0b52ce39` | Useful hash/build collection patterns; build-directory schema does not describe the retained external binary and campaign ledger. |
| P2, P4, P5 | already present | [config/acacia-options.json:1](../../config/acacia-options.json#L1): `{` | `c2fec7da1859d1b8d08a7faa91da080a31050403` | Registry/frontends already agree; do not add a build variant or change shipping presets in P0. |
| P2, P4, P5 | already present | [config/acacia-presets.json:1](../../config/acacia-presets.json#L1): `{` | `ef243c9ad67e8533cbe93c73edd63d49aab52821` | Registry/frontends already agree; do not add a build variant or change shipping presets in P0. |
| P2, P4, P5 | already present | [meson.options:1](../../meson.options#L1): `option('build_tests'` | `70a96b2be6884c24f6cd543f5ed0b3ca3dc61eed` | Registry/frontends already agree; do not add a build variant or change shipping presets in P0. |
| P2, P4, P5 | already present | [meson.build:1](../../meson.build#L1): `project (` | `5b23bf975155b380eec7207bd8da10472f212f8e` | Registry/frontends already agree; do not add a build variant or change shipping presets in P0. |
| P2, P4, P5 | already present | [src/config/acacia_build_config.hh.in:1](../../src/config/acacia_build_config.hh.in#L1): `#pragma once` | `cdc151eaa049602fe34ad1080c669224da58d8d2` | Registry/frontends already agree; do not add a build variant or change shipping presets in P0. |
| P0, P4, P5 | already present | [tests/suites/benchmarks/syntcomp26/all.list:4](../../tests/suites/benchmarks/syntcomp26/all.list#L4): `01.ltl` | `2eb83157ac81706c48b778418d7082cd5cf335bc` | Existing logical corpus list; validated unchanged, do not rebuild the universe. |
| P0, P4, P5 | already present | [tests/suites/benchmarks/syntcomp26/tlsf-sources.tsv:1](../../tests/suites/benchmarks/syntcomp26/tlsf-sources.tsv#L1): `instance` | `591e29bd527f0c6b472a9de27c8af0e71e013490` | Existing native TLSF source map; every selected ID maps exactly once. |
| P0, P4, P5 | already present | [benchmarking/coverage-frontier-20260912/diagnostic-cohort.tsv:1](../../benchmarking/coverage-frontier-20260912/diagnostic-cohort.tsv#L1): `instance` | `953bb43f6c6ebb571024b3caa3ea1f8b3d64fada` | Existing frozen cohort and tiers; no reselection. |
| P0, P4, P5 | already present | [benchmarking/coverage-frontier-20260912/p1-pairs.md:1](../../benchmarking/coverage-frontier-20260912/p1-pairs.md#L1): `# P1` | `1855dd1f4a0cbf3bb19caee431e6131f0d3b8de8` | Existing selector/pair evidence; reuse verdict sets under their own identity. |
| P0, P4, P5 | already present | [benchmarking/portfolio-evidence-20260910/PROVENANCE:1](../../benchmarking/portfolio-evidence-20260910/PROVENANCE#L1): `question:` | `01851dbb4760b4015b56a897e72f30b2b8b9941f` | Existing isolated-arm/race identities, including the CPU-quota distinction. |
| P0, P4, P5 | already present | [subprojects/std-simd.wrap:3](../../subprojects/std-simd.wrap#L3): `revision =` | `be54171e2fbc0f8b3e38829e7af05af3eba267ff` | Reuse the existing pinned SIMD abstraction only if a later profile gate fires. |
| P4 | deferred | [benchmarking/COVERAGE-DIRECTED-FORWARD-SPRINT.md:65](../../benchmarking/COVERAGE-DIRECTED-FORWARD-SPRINT.md#L65): `## D. Family-level frontiers` | `18392832fa449d05d65c86e4b0b7671ab8d015ac` | Use named five-arm gains and losses to screen one four-arm replacement later. |
| P2 | new | `src/solver/closure_buchi_provider.hh`: `closure-buchi (absent)` | `absent` | New closure provider is reserved for P2; absent files have no blob hash or line. |
| P2 | new | `src/solver/closure_buchi_provider.cc`: `closure-buchi (absent)` | `absent` | New closure provider is reserved for P2; absent files have no blob hash or line. |
| P2 | new | `tests/closure_buchi_provider_test.cc`: `closure-buchi (absent)` | `absent` | New closure provider is reserved for P2; absent files have no blob hash or line. |
| P0 | modify | [src/solver/spot_nba_fastpath.hh:442](../../src/solver/spot_nba_fastpath.hh#L442): `inline fast_path_result try_spot_nba_fast_path` | `bd3ba3d911b0de2a91ffa6932f1e4df7ca55ea97` | Record direct Spot solving and GFG precondition checks as unbounded attempts with the effective backend. |
| P0 | modify | [src/solver/equivariant_k_bounded_safety_aut.hh:545](../../src/solver/equivariant_k_bounded_safety_aut.hh#L545): `result<SetOfStates> solve_orbit_sweep` | `696e963a73aff7302afeb22e4129772140fee276` | Record both exact equivariant K loops, construction, search, outcomes and declines. |
| P0 | modify | [tests/spot_nba_fastpath_test.cc:119](../../tests/spot_nba_fastpath_test.cc#L119): `bool fast_path_records` | `672dbbe6eb5ee9c0e71dc355c18cb13160aaa51e` | Check deterministic/GFG loss records and distinguish GFG precondition verification from solving. |
| P0 | modify | [tests/equivariant_pre_test.cc:517](../../tests/equivariant_pre_test.cc#L517): `bool check_record_lifecycle` | `92c2594f163fd290012cc6828f17efd361b83a4e` | Check accepted closure/sweep wins, repeated-K losses and ordered lifecycle boundaries. |
| P0 | modify | [src/solver/solver_invoker.hh:44](../../src/solver/solver_invoker.hh#L44): `inline bool finish_empty_translation` | `667878eb7737cdac4092b0ecf2db5faee1d8128c` | Keep the zero-state worker return testable: unbounded language evidence, with real/unreal polarity unchanged. |

## Artifact inventory

| State | Artifact | SHA-256 | Reason |
|---|---|---|---|
| reuse | `/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-closing/bin/candidate-f7919b83` | `0f4f48b6b751be91d9e0575105d4678d47dbccb8c998c01d71f9597abe22fb99` | Frozen 0555 binary; matches retained freeze manifest and every D1 binary_sha256; source-equivalent to 6410fd34. |
| reuse | `/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-closing/bin/freeze-manifest.tsv` | `d4dfb006736e491552c1471692fae92d9a6536f9f28f9c46fea6a946d8b0bc2f` | Original binary/source identity. |
| reuse | `/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-closing/candidate-options.txt` | `094502d7eda29643553b4d98972a13e7218a08cfd2f42f1d029a53d93f34c0a2` | Resolved option set for this frozen candidate, including native release profile and four arms. |
| reuse | `/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-closing/d1-candidate.tsv` | `29cf79c82674a1e76686fef1b539985764736e06070449d9e0d7c1207b3199dc` | 1524 primary uniform-17-second observations; retain raw exit/status and repetition identity. |
| reuse | `/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-closing/d1-candidate-summary.tsv` | `c88f31fd3453d704f5477b450f38821f635e0b78e96aac99857dce9f148121b4` | Derived D1 summary; one cap only, so no staged-cap timing substitution. |
| reuse | `/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-closing/three-way/ltlsynt.csv` | `3edbe5a2ad649f68add59896329eecdbedde775bf92ff732e4b0185af18af5aa` | 1524 primary external observations from same closing epoch; preserve conversion failures. |
| reuse | `/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-closing/three-way/d6-provenance.tsv` | `3b18ad91f5d283995c2cf4ae9a7d84c13d120a7dd7e71ac7427d9c418456c963` | External hash/version/kernel and epoch; literal backslash-t separators in retained file. |
| reuse | `/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-closing/closing.sh` | `4fc670e82f49311ec5f15d359e39b93f7ec60fdd8dd252bf059b097bc7b725c9` | Retained Acacia orchestration entry point; documentation only, never executed in P0. |
| reuse | `/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-closing/ltlsynt.sh` | `77834d6b62019ca99d6e6d71570644d53cbd4de8cd578d1a6bf3c9ebdf30a784` | Retained D6/conversion/cactus entry point; conversion has limitations reserved for P5. |
| reuse | `/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-closing/closing.log` | `434b3fa90b2a765e6215ef919d99c0b4c89016f37ba20c3949acb36b20437db3` | Retained campaign timestamps/commands and completion status. |
| reuse | `/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-closing/close/SPRINT-CLOSE.md` | `99e4a6409abe58565fd9630044b3acfe17e8211b34ad103e6788ba8200e42d5a` | Historical closure, kernel and failure attribution; no new timing claim. |
| reuse | `/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-closing/d3-pairs.tsv` | `d34369d728238d0427196bab0c958dd41bed92f6f64e9ec41694e4a1db9fe973` | Two fixed measured pairs; cannot prove a four-arm replacement. |
| reuse | `/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-closing/d3-pairs-summary.tsv` | `bc1ccf1fbf0fe016e1d55c996594cba28718e1fc4a789c5eecf39705fdd09e9e` | Pair verdict sets and primary outcomes. |
| reuse | `/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-closing/d3-pairs-metadata.json` | `b699da119490c527b8dd946c8d49be4c79fb0bea4489b3431137922d2d5c3672` | Exact pair campaign identity and error policy. |
| reuse | `/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-closing/d5-five-arm.tsv` | `c524edb4afff1377dce99c4bcfe118f8b5924b4b428505eca5fb4551cf3bfebd` | Measured five-arm no-quota race, not a four-arm replacement. |
| reuse | `/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-closing/d5-five-arm-summary.tsv` | `816548b687c0939050236319fe7aefa8274c9301122dd9bd6ac4bebde199a252` | Primary five-arm gains/losses, subject to retained repetitions. |
| reuse | `/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-closing/reps` (tree, 108 files) | `dd2c34d30e1422a2a4ded6ea826f7c1e9a3c4e621f3bf7f3c1bf6c1edbb663a4` | Retained confirmation repetitions; never take per-instance fastest run. |
| reuse | `/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-closing/close/wrec` (tree, 1405 files) | `8c3e3eccdddfd9440c8965e32474b578c5e7acf656fac01492c93376a39d1aad` | 1405 legacy final snapshots; stages/lower-bound reached K, not cumulative completed-attempt cost. |
| reuse | `/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-closing/close/d7-unsolved.tsv` | `7bca9addf6cfe62cd99a65c6ffbfb61f6570a359b6ab11500fef07676aa45407` | 350 historical misses revisited; retain denominator and parent outcome. |
| reuse | `/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-closing/close/d7-unsolved-summary.tsv` | `74b19ea61841c1a12f32f21a5314ecf7a0b97c34cf8f490e36cbbb839271434b` | Derived D7 miss evidence; no full-corpus replacement. |
| reuse | `/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-closing/g2s` (tree, 122 files) | `efb9eaede3986179f8046dd49b63cab05f8f6084b9f6458416582b3ae29db7aa` | Perf stat hardware totals exist; no sampled sparse-search attribution. |
| regenerate missing | `/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-closing/close/d7-phases.tsv` | `missing` | No d7-phases file found; raw wrec and SPRINT-CLOSE.md already provide existing observations. No new census in P0. |
| reuse | `benchmarking/run-syntcomp26-coverage.py` | `9f11372fcb4214aec672fcbb467b3cb9177d13352f66dad3651f43622c047fab` | Existing helper identity in this checkout; do not assume historical helper hashes without checking the historical source. |
| reuse | `benchmarking/run-subset.py` | `a0bf4ce74e33c9c966a5d6c0fcfda0f19d2ab79333c99869b786a8e938cea125` | Existing helper identity in this checkout; do not assume historical helper hashes without checking the historical source. |
| reuse | `benchmarking/benchlib.py` | `3989eefa83e9e2c51e79732a36946addbe9e4f0eab2180dca958bf7e2dfcd1cb` | Existing helper identity in this checkout; do not assume historical helper hashes without checking the historical source. |
| reuse | `benchmarking/cactus-report.py` | `571179127e908743a1ff59afd5321d596e7bc626a80c751e2b4dec7b1e0df4dc` | Existing helper identity in this checkout; do not assume historical helper hashes without checking the historical source. |
| reuse | `benchmarking/coverage-frontier-20260912/diagnostic-cohort.list` | `3d26a5ae64576862e036dd1065a9e47a16735b659dfe7c1bcfe9df0271a13bc5` | Frozen 46-instance target list, reused unchanged. |
| reuse | `benchmarking/coverage-frontier-20260912/diagnostic-cohort.tsv` | `60f960c95f9cafb6208e8d41ba8c9c2a88de2746bdc6e1140c5b62e217ef45c9` | Frozen tiers/selection metadata. |
| reuse | `benchmarking/coverage-frontier-20260912/failure-phases.md` | `891d2294ad62f91f9cc68d54726bf365892427429a8b3f85e4f139213ea71deb` | Legacy stage/censoring caveats; last-attempt distributions only. |
| reuse | `benchmarking/coverage-frontier-20260912/failure-phases.tsv` | `85f011d7bd1c43ef07361ec06867422ba5c03e442169db7858d79e5148966d71` | Per-worker stage/current K and stale last-completed attempt counters; not cumulative loss time. |
| reuse | `benchmarking/coverage-frontier-20260912/a2b-profile.md` | `d5e8861cf45261e3e14c450dcab655c29c7a0f2512dd6fe3aa16a28988551188` | 13-worker count composition, explicitly no CPU-time attribution. |
| reuse | `benchmarking/coverage-frontier-20260912/t1-translation.md` | `ef3bd13d248265c172ac7cf667644a3034eea37985c984b7c1174b7e09047a90` | Negative translation/TAA/AST studies; reuse conclusions, do not repeat. |
| reuse | `benchmarking/coverage-frontier-20260912/t1-translation` (tree, 1 files) | `dee64ee1aff778c714e8c9e22ffc1dc5c62f990ad210640e59b7e0d3bdf85cdd` | Retained translation attribution tables; historical per-construction outcomes. |
| reuse | `benchmarking/coverage-frontier-20260912/s2-ablation` (tree, 3 files) | `c430de485f2dc022551efdd5ff76d2de0f8dbf9aa7e0e859e34c3bf7cd3a3608` | Existing symbolic losing-input ablation; mechanism/outcome evidence, no new measurement. |
| reuse | `benchmarking/coverage-frontier-20260912/p1-pairs.md` | `be445003140c239f8d54009fec891df903a1068ea5e3e43a0dd4178756ce68b6` | Historical 180-panel fixed-pair/oracle sets, not replacement admission. |
| reuse | `benchmarking/coverage-frontier-20260912/p1-pairs` (tree, 2 files) | `4dc0bdad1f03944a959ec97a735e63d381715420978f510a908111d5072c94ea` | Committed pair metadata and summary. |
| reuse | `benchmarking/p4-forward-expansion-profile.tsv` | `a9bf2cb5bd30853441a1a2ea63daead9579a9717814b598eb6014ea0e9af4462` | Structural counts or historical arm/race evidence; regime-specific, not sampled sparse CPU. |
| reuse | `benchmarking/portfolio-evidence-20260910` (tree, 48 files) | `d4b1f16ebf7e844f00f456aede97e75d68d261a8a34fd70fcf84f4922e76130f` | Structural counts or historical arm/race evidence; regime-specific, not sampled sparse CPU. |
| reuse | `benchmarking/COVERAGE-DIRECTED-FORWARD-SPRINT.md` | `9cb81ae2a9cbae0199fe340b58251b4c7f1cd3fa5609633e1972c39367ee361d` | Structural counts or historical arm/race evidence; regime-specific, not sampled sparse CPU. |
| reuse | `benchmarking/ADAPTIVE-PORTFOLIO-OTFUR-SPRINT.md` | `679bde05cc25df4479710e31dd70892b71fa06b706b6cfc7c9a0e056d18c43ae` | Structural counts or historical arm/race evidence; regime-specific, not sampled sparse CPU. |
| incompatible | `benchmarking/plots/spot-otf-threeway-20260909` (tree, 5 files) | `9c098af20ea89bfcb6adc10501f75ca2af2163d66f8d9a1b85d14d68922c9da4` | Older three-way context only: different epoch/build/caps/series; not the new sprint reference dataset. |
| incompatible | `benchmarking/plots/three-way-20260829` (tree, 7 files) | `48ad8f70e9d039b83450566b0b71e1be63e2afe81f92cf6182edf3a685b092d7` | Older three-way context only: different epoch/build/caps/series; not the new sprint reference dataset. |
| incompatible | `benchmarking/plots/three-way-full-20260905` (tree, 8 files) | `27f07b69e0d4e803e30eb8c8e5d58b1dc0375b622b209c9f3d9b31a18fa1ee11` | Older three-way context only: different epoch/build/caps/series; not the new sprint reference dataset. |
| incompatible | `benchmarking/plots/three-way-probe-20260831` (tree, 8 files) | `c32f7424f907383598d1a549465f8e31299eb67ea571f39ff84b4926152d845f` | Older three-way context only: different epoch/build/caps/series; not the new sprint reference dataset. |
| reuse | `/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260912-s0-phases/wrec` (tree, 184 files) | `30167eb0593610fda8584a2c072783671a28d341cf08b3b0f12a7200bc5d0a20` | Legacy frozen sparse repeated-K evidence; no attempt histories or cumulative verification totals. |
| reuse | `/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-a2b-profile/wrec` (tree, 184 files) | `e02e111acf52843fb4f853dc27744d99b3ebfd63d12318a43531335e434714a0` | Last-verified-attempt counters only; no history.jsonl, no cumulative verification timing. |
| reuse | `tests/suites/benchmarks/syntcomp26/all.list` | `0226e5cd2cadc033544f0901bd900dd31dd7add813ef216558826468e0b7b5c7` | Identity checked; 1524 unique logical IDs each mapped exactly once and every mapped TLSF exists. |
| reuse | `tests/suites/benchmarks/syntcomp26/tlsf-sources.tsv` | `9fbc4007b18065296d592f14bd9ef0e9e95a4893a4d4477e0153013fa531f92f` | Identity checked; 1524 unique logical IDs each mapped exactly once and every mapped TLSF exists. |
| reuse | `/home/gperez/GIT-repos/acacia-bonsai/tlsf-corpus/.acacia-tlsf-corpus` | `4b69bad194323325bc424b94419d911b1127b5a0af79cd149a12a3754218c807` | Materialized marker describes 1586 entries; logical corpus is the 1524-ID selection. |

## Frozen identities and reuse boundary

Baseline binary: `/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-closing/bin/candidate-f7919b83`, mode `0o555`, SHA-256 `0f4f48b6b751be91d9e0575105d4678d47dbccb8c998c01d71f9597abe22fb99`. This is the old closing **candidate**, now the sprint baseline; do not substitute the old `baseline-1557235d`. Its hash matches `bin/freeze-manifest.tsv` and all 1524 D1 raw rows. The requested command produced empty stdout:

```sh
git diff --stat f7919b83 6410fd34 -- src subprojects meson.build meson.options config
# stdout: empty
```

Resolved option set: [/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-closing/candidate-options.txt:1](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-closing/candidate-options.txt#L1). Preset `otf_sparse_formula`; release `-Ofast -march=native`, optimization 3, LTO, assertions disabled; defaults K=2..99 by 3, Spot fast=det, candidate-only. Runtime flags in D1 are empty; exact arms:

```text
real:small:backward
real:small:forward
unreal:formula:spot-guarded-sparse
unreal:automaton:forward
```

Current linked libraries (all resolved paths/hashes are in the manifest; the relevant two are):

- `libspot.so.0` → `/usr/local/lib/libspot.so.0.0.0`, SHA-256 `0d6dbbb6c985bd3001d1df5b6b562e5b403a00ec0d6bb3275fc0617b3d5faf32`.
- `libbddx.so.0` → `/usr/local/lib/libbddx.so.0.0.0`, SHA-256 `a991f2049c44e3f3cf9102b7d40d9efc2c5bb2b8a3a1af7d120f7c23c9caf44d`.

Host: `7.2.5-200.fc44.x86_64`; `11th Gen Intel(R) Core(TM) i7-11850H @ 2.50GHz`; `ltlsynt --version`: `/usr/local/sbin/ltlsynt (spot) 2.15.1.dev`; `PKG_CONFIG_PATH=/usr/local/lib/pkgconfig pkg-config --modversion libspot`: `2.15.1.dev`; `g++ (GCC) 16.2.1 20260819 (Red Hat 16.2.1-2)`.

External binary `/usr/local/sbin/ltlsynt`, SHA-256 `ea761a1c0594278bd4b525369977677520c8b9d3dcc2f5a45dab91d961663900`, matches retained D6 provenance. D1 and D6 each contain exactly the same 1,524 logical IDs. D1 result counts: `{'REALIZABLE': 533, 'TIMEOUT': 347, 'UNREALIZABLE': 641, 'UNKNOWN': 1, 'MEMOUT': 2}`; D6: `{'REALIZABLE': 573, 'TIMEOUT': 255, 'UNREALIZABLE': 684, 'SYFCO-FAIL': 7, 'UNKNOWN': 2, 'ERROR': 3}`. These are observations, not fresh runs. D1 raw fields verify 17 s / 8G / swap 0 / no CPU quota / no CPU pinning on every row. D6's script and provenance establish the same requested regime and kernel. Epoch: D1 2026-09-15T23:35:38.211648Z–2026-09-16T01:28:07.698370Z, D6 2026-09-16 06:32:16–07:56:50 UTC.

These references are reusable **historical observations**. New candidate timing reuse still needs the plan’s matched controls and identity checks. Matching current kernel/version/sonames cannot retrospectively prove historical linked-library bytes; those hashes were not independently frozen by closing. Historical hardware identity is in `benchmarking/portfolio-evidence-20260910/PROVENANCE:37`. Current helper hashes and `git show f7919b83:...` helper hashes are both recorded and match for all four helpers; a future changed helper must not be treated as the old execution identity.

Corpus validation: 1524 IDs, 0 duplicates; each selected ID has exactly one map row, and every selected source exists. Marker `/home/gperez/GIT-repos/acacia-bonsai/tlsf-corpus/.acacia-tlsf-corpus` reports 1,586 materialized entries and manifest SHA-256 `6f2e459918335f5af41f435d4755bf76352fc11a7d07693fdf5d3786d0bdb615`. The marker's full universe is distinct from the 1,524-ID selection. The manifest also hashes sorted logical-ID/TLSF-content-hash pairs. Cohort: 46 unique IDs, unchanged.

## Retained threeway-experiment commands

There is no tracked executable named `threeway-experiment`. The retained entry point is `closing.sh` plus `ltlsynt.sh`; complete original scripts (including the exact inline conversion heredoc) are embedded in `manifest.json → harness.threeway_entry_points[].commands` and hashed above. They are provenance, not instructions executed during P0. The exact retained commands and variable definitions relevant to the headline workflow follow:

```sh
COMMON=(--list tests/suites/benchmarks/syntcomp26/all.list
        --tlsf-map tests/suites/benchmarks/syntcomp26/tlsf-sources.tsv
        --tlsf-corpus "$MAIN/tlsf-corpus" --memory-max 8G --memory-swap-max 0)
FIVE="real:small:backward,real:small:forward,unreal:formula:spot-guarded-sparse,unreal:automaton:forward,real:small:spot-guarded-sparse"
run_step d1-candidate python3 benchmarking/run-syntcomp26-coverage.py --bin "$CAND" \
    --solver-label "d1-candidate-$candidate_rev" "${COMMON[@]}" --caps 17 --conflict-policy collect \
    --collect-rusage --preset otf_sparse_formula --acacia-sha "$candidate_rev" --output "$G/d1-candidate.tsv" --resume
run_step d2-baseline python3 benchmarking/run-syntcomp26-coverage.py --bin "$BASE" \
    --solver-label d2-baseline-1557235d "${COMMON[@]}" --caps 17 --conflict-policy collect \
    --collect-rusage --preset otf_sparse_formula --acacia-sha 1557235d --output "$G/d2-baseline.tsv" --resume
run_step d5-five-arm python3 benchmarking/run-syntcomp26-coverage.py --bin "$CAND" \
    --solver-label "d5-five-arm-$candidate_rev" "${COMMON[@]}" --caps 17 --conflict-policy collect \
    --collect-rusage --flags "--arms $FIVE" --preset otf_sparse_formula --acacia-sha "$candidate_rev" \
    --output "$G/d5-five-arm.tsv" --resume
```

`MAIN=/home/gperez/GIT-repos/acacia-bonsai`; `G=$MAIN/_bm-logs.20260915-closing`; `candidate_rev=f7919b83`; `CAND=$G/bin/candidate-f7919b83`; `BASE=$G/bin/baseline-1557235d`. `run_step` is the retained sequential wrapper, with logs and completion markers. D6 and conversion/plot commands, verbatim:

```sh
LT=$(command -v ltlsynt)
{ echo "ltlsynt_bin	$LT"; echo "ltlsynt_sha256	$(sha256sum "$LT" | cut -d' ' -f1)"
  echo "ltlsynt_version	$(ltlsynt --version 2>&1 | head -1)"; echo "syfco_version	$(syfco --version 2>&1 | head -1)"
  echo "kernel	$(uname -r)"; echo "started_utc	$(date -u +%FT%TZ)"; } > "$T/d6-provenance.tsv"
grep -vE '^\s*(#|$)' tests/suites/benchmarks/syntcomp26/all.list > "$T/all-instances.list"
say "instances: $(wc -l < "$T/all-instances.list")"
if [ ! -f "$K/d6-ltlsynt.done" ]; then
    sleep 120; say "START d6-ltlsynt"
    python3 benchmarking/run-subset.py --tool ltlsynt --bin "$LT" --list "$T/all-instances.list" \
        --tlsf-map tests/suites/benchmarks/syntcomp26/tlsf-sources.tsv --tlsf-corpus "$MAIN/tlsf-corpus" \
        --syfco-cache "$T/syfco-cache" --systemd-scope --memory-max 8G --memory-swap-max 0 --timeout 17 \
        --csv "$T/ltlsynt.csv" > "$K/d6-ltlsynt.log" 2>&1
    rc=$?; echo $rc > "$K/d6-ltlsynt.done"; say "END d6-ltlsynt rc=$rc"
fi
echo "finished_utc	$(date -u +%FT%TZ)" >> "$T/d6-provenance.tsv"
python3 - "$K" "$T" <<'PY' >> "$LOG" 2>&1
import csv, sys, pathlib
K, T = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
for src, dst in (('d2-baseline', 'acacia-1557235d'), ('d1-candidate', 'acacia-new')):
    rows = list(csv.DictReader(open(K / f'{src}-summary.tsv'), delimiter='\t'))
    with open(T / f'{dst}.csv', 'w', newline='') as h:
        w = csv.writer(h); w.writerow(['instance', 'result', 'seconds', 'exit'])
        for r in rows:
            solved = r['still_unsolved_at_max_cap'] == 'false'
            res = r['decisive_result'] if solved else (r['failure_kind_at_max_cap'] or 'UNSOLVED')
            secs = r['decisive_seconds'] if solved else r.get('max_cap_s', '17')
            w.writerow([r['instance'], res, secs, 0])
    print(f'converted {src} -> {dst}.csv ({len(rows)} rows)')
PY
python3 benchmarking/cactus-report.py --csv "ltlsynt=$T/ltlsynt.csv" --csv "Acacia before sprint (1557235d)=$T/acacia-1557235d.csv" \
    --csv "Acacia new=$T/acacia-new.csv" --title "SYNTCOMP26 (1,524): ltlsynt vs Acacia before and after the sprint" \
    --timeout 17 --out-prefix "$T/three-way" --markdown "$T/three-way.md" >> "$LOG" 2>&1 || say "cactus-report failed (data intact)"
say "LTLSYNT DONE"; touch "$K/LTLSYNT_DONE"
```

P5 must keep raw exit codes and normalize MEMOUT/CRASH without turning them into answers: the retained inline conversion currently writes exit=0 for every row, does not validate raw exit/verdict agreement, and does not explicitly map those statuses. Neither it nor `cactus-report.py` was changed here. Older tracked three-way folders contain protocol/provenance, not a reusable named wrapper; the sole explicit command fragment found is `cactus-report.py --virtual-best` in `spot-otf-threeway-20260909/README.md:24`. Their older epochs/series cannot populate this new headline comparison.

## Decision inputs

### P1a — scheduling-only loss hints

Frozen sparse dispatch: [src/solver/solve_game_impl.hh:216](../../src/solver/solve_game_impl.hh#L216) creates `Search`, then calls `search.solve()`. Lazy worker: [src/solver/spot_lazy_worker.hh:109](../../src/solver/spot_lazy_worker.hh#L109) invokes the same exact engine. Its fixed-K losing checker call is [src/solver/spot_lazy_game.hh:1177](../../src/solver/spot_lazy_game.hh#L1177); the dense guarded checker is [src/solver/spot_guarded_forward_safety.hh:287](../../src/solver/spot_guarded_forward_safety.hh#L287). None was removed or made optional in P0.

`spot_lazy_game::SolveResult::status` carries `forward_result_status::lose_k` ([src/solver/spot_lazy_game.hh:812](../../src/solver/spot_lazy_game.hh#L812)); **there is no single result type carrying both LOSE_K and have_certificate**. `k_schedule::loss_evidence::have_certificate` is the separate scheduling payload ([src/solver/k_schedule.hh:11](../../src/solver/k_schedule.hh#L11)). Both wrappers currently pass `true` only after their exact solver returns LOSE_K ([src/solver/solve_game_impl.hh:273](../../src/solver/solve_game_impl.hh#L273), [src/solver/spot_lazy_worker.hh:131](../../src/solver/spot_lazy_worker.hh#L131)). Future hint results must be type-separated and must never set that Boolean or claim verified loss.

Which cohort instances spend most **cumulative** time verifying loss, and how much: **not recorded**. S0/A2b/D7 have final snapshots, no milestone histories; a K=next/search record may retain the previous verified attempt's timings. [benchmarking/coverage-frontier-20260912/a2b-profile.md:30](../../benchmarking/coverage-frontier-20260912/a2b-profile.md#L30) and [benchmarking/coverage-frontier-20260912/a2b-profile.md:61](../../benchmarking/coverage-frontier-20260912/a2b-profile.md#L61) explicitly forbid interpreting the old counter distribution as cumulative cost. A2b's 13 measured last verified attempts are losses with 100% of verifier BDD operations in proof-bad reconstruction; that is neither cumulative milliseconds nor sampled CPU. The new opt-in fields permit future measurement; P0 made none.

### P1b — immutable frozen rows

Confirmed: the `spot_guarded_sparse` `RowStore` is constructed inside the K loop ([src/solver/solve_game_impl.hh:202](../../src/solver/solve_game_impl.hh#L202); [src/solver/solve_game_impl.hh:221](../../src/solver/solve_game_impl.hh#L221)). `spot_lazy_worker` constructs `game::RowStore store` before its K loop ([src/solver/spot_lazy_worker.hh:88](../../src/solver/spot_lazy_worker.hh#L88), [src/solver/spot_lazy_worker.hh:100](../../src/solver/spot_lazy_worker.hh#L100)); this retention is already implemented. P0 does not move either store or cache K-dependent proof data.

Identity/context: provider/graph ownership and revision, canonical state numbering, row limits/failure state, increment mode and acceptance convention, Boolean threshold and metadata ([src/solver/spot_rows.hh:28](../../src/solver/spot_rows.hh#L28), [src/solver/spot_rows.hh:91](../../src/solver/spot_rows.hh#L91), [src/solver/spot_rows.hh:107](../../src/solver/spot_rows.hh#L107)); AP BDD dictionary, partition and order supplied separately to Reader/Oracle/Search ([src/solver/solve_game_impl.hh:196](../../src/solver/solve_game_impl.hh#L196), [src/solver/spot_lazy_game.hh:1037](../../src/solver/spot_lazy_game.hh#L1037)). Raw rows do not depend on K, while safe caps, threshold/preimage caches, ranks, losses and proofs do. Boolean-state discovery can renumber the graph ([src/solver/solver_invoker.cc:651](../../src/solver/solver_invoker.cc#L651)); freeze only afterwards. Never reuse across a different transformed formula/polarity, fallback provider, dictionary or alphabet. Retained BDD/provider ownership must outlive certificates.

Existing A2b frozen sparse records establish repeated K **starts** for the following 13 cohort instances (Kmin=2, shipped linear +3 schedule). They do not establish the number of completed attempts or cumulative cost. S0 confirms the same set; D7 excludes solved instances and is not a replacement denominator.

| Cohort instance | Last recorded K | Stage | Evidence |
|---|---:|---|---|
| Alarm_a5f99bc6.ltl | 5 | search | [/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-a2b-profile/wrec/a2b-master-c4e0eb80/17/Alarm_a5f99bc6.ltl/102208-1.json:1](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-a2b-profile/wrec/a2b-master-c4e0eb80/17/Alarm_a5f99bc6.ltl/102208-1.json#L1) |
| F-G-contradiction-110.ltl | 17 | search | [/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-a2b-profile/wrec/a2b-master-c4e0eb80/17/F-G-contradiction-110.ltl/102285-1.json:1](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-a2b-profile/wrec/a2b-master-c4e0eb80/17/F-G-contradiction-110.ltl/102285-1.json#L1) |
| GF-G-contradiction7.ltl | 11 | search | [/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-a2b-profile/wrec/a2b-master-c4e0eb80/17/GF-G-contradiction7.ltl/101761-1.json:1](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-a2b-profile/wrec/a2b-master-c4e0eb80/17/GF-G-contradiction7.ltl/101761-1.json#L1) |
| g-unreal-113.ltl | 20 | search | [/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-a2b-profile/wrec/a2b-master-c4e0eb80/17/g-unreal-113.ltl/101832-1.json:1](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-a2b-profile/wrec/a2b-master-c4e0eb80/17/g-unreal-113.ltl/101832-1.json#L1) |
| g-unreal-116.ltl | 26 | search | [/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-a2b-profile/wrec/a2b-master-c4e0eb80/17/g-unreal-116.ltl/102634-1.json:1](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-a2b-profile/wrec/a2b-master-c4e0eb80/17/g-unreal-116.ltl/102634-1.json#L1) |
| heim-double-x-real.ltl | 20 | search | [/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-a2b-profile/wrec/a2b-master-c4e0eb80/17/heim-double-x-real.ltl/101455-1.json:1](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-a2b-profile/wrec/a2b-master-c4e0eb80/17/heim-double-x-real.ltl/101455-1.json#L1) |
| infinite-race-u12.ltl | 11 | search | [/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-a2b-profile/wrec/a2b-master-c4e0eb80/17/infinite-race-u12.ltl/102694-1.json:1](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-a2b-profile/wrec/a2b-master-c4e0eb80/17/infinite-race-u12.ltl/102694-1.json#L1) |
| ordered-visits-choice-real.ltl | 99 | verified-attempt | [/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-a2b-profile/wrec/a2b-master-c4e0eb80/17/ordered-visits-choice-real.ltl/101539-1.json:1](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-a2b-profile/wrec/a2b-master-c4e0eb80/17/ordered-visits-choice-real.ltl/101539-1.json#L1) |
| robot-to-target-charging10.ltl | 14 | search | [/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-a2b-profile/wrec/a2b-master-c4e0eb80/17/robot-to-target-charging10.ltl/102815-1.json:1](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-a2b-profile/wrec/a2b-master-c4e0eb80/17/robot-to-target-charging10.ltl/102815-1.json#L1) |
| robot-to-target-charging7.ltl | 14 | search | [/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-a2b-profile/wrec/a2b-master-c4e0eb80/17/robot-to-target-charging7.ltl/101942-1.json:1](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-a2b-profile/wrec/a2b-master-c4e0eb80/17/robot-to-target-charging7.ltl/101942-1.json#L1) |
| taxi-service-real.ltl | 5 | search | [/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-a2b-profile/wrec/a2b-master-c4e0eb80/17/taxi-service-real.ltl/102009-1.json:1](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-a2b-profile/wrec/a2b-master-c4e0eb80/17/taxi-service-real.ltl/102009-1.json#L1) |
| thermostat-F-real.ltl | 20 | search | [/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-a2b-profile/wrec/a2b-master-c4e0eb80/17/thermostat-F-real.ltl/101647-1.json:1](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-a2b-profile/wrec/a2b-master-c4e0eb80/17/thermostat-F-real.ltl/101647-1.json#L1) |
| workstation_resupply_pb_3_pe_.ltl | 29 | search | [/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-a2b-profile/wrec/a2b-master-c4e0eb80/17/workstation_resupply_pb_3_pe_.ltl/102056-1.json:1](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-a2b-profile/wrec/a2b-master-c4e0eb80/17/workstation_resupply_pb_3_pe_.ltl/102056-1.json#L1) |

### P2 — provider and replay boundaries

RowStore has exactly two constructors: `RowStore(shared_ptr<LazyBuchiView>, ...)` and `RowStore(FrozenAcacia, ...)` ([src/solver/spot_lazy_game.hh:135](../../src/solver/spot_lazy_game.hh#L135), [src/solver/spot_lazy_game.hh:143](../../src/solver/spot_lazy_game.hh#L143)). It does **not** accept an arbitrary transition-Büchi twa. `SpotRows` underneath already accepts `GenericTransitionBuchi` ([src/solver/spot_rows.hh:107](../../src/solver/spot_rows.hh#L107)). `RowStore::snapshot` assumes TAA cursor state in its non-frozen path, with `dynamic_cast<const lazy::detail::CursorState*>` and a required non-null result ([src/solver/spot_lazy_game.hh:204](../../src/solver/spot_lazy_game.hh#L204)); P2 must separate this telemetry path for the new generic provider.

Fourth-field handling is already typed: provider enum/parser [src/solver/game_backend.hh:19](../../src/solver/game_backend.hh#L19); optional fourth colon-field parse [src/portfolio_arm.hh:138](../../src/portfolio_arm.hh#L138); compile validation [src/arg_parser.hh:280](../../src/arg_parser.hh#L280); final defaults/compatibility validation [src/arg_parser.hh:689](../../src/arg_parser.hh#L689). Names currently are frozen-graph/spot-lazy/spot-eager. Non-frozen providers require the lazy compile gate, formula-unreal (not automaton-unreal), and **spot-guarded**, not spot-guarded-sparse. P2 needs a targeted extension; do not merely add an unchecked string.

Transformed worker `spot::formula` is ready after X-shifting and polarity negation, at [src/solver/solver_invoker.cc:432](../../src/solver/solver_invoker.cc#L432). AP vectors and registered `all_inputs`/`all_outputs` are members initialized at [src/solver/solver_invoker.cc:331](../../src/solver/solver_invoker.cc#L331). Existing provider dispatch precedes translation at [src/solver/solver_invoker.cc:467](../../src/solver/solver_invoker.cc#L467); eager translation and graph-wide preprocessing happen later. Reuse the native TLSF lowering and this exact transformed formula boundary.

`src/research/spot_provider_replay.cc` supports **eager C4 and lazy C5**, transformed formula plus lexical AP partition, process isolation/limits, reporter normalization, and **repeated K** (`--kmax`, `--kinc`, [src/research/spot_provider_replay.cc:255](../../src/research/spot_provider_replay.cc#L255); loop [src/research/spot_provider_replay.cc:531](../../src/research/spot_provider_replay.cc#L531)). It currently hardcodes `ltl_to_taa`: **no provider-selection CLI and no complete-graph export CLI**. C4 already enumerates/freezes the cache; extend it for export rather than enumerating twice. `antichain_replay.cc` is a dense snapshot replay with `--dir/--cap`, not a sparse trace replay ([src/research/antichain_replay.cc:73](../../src/research/antichain_replay.cc#L73)).

Reuse language checks and completed-graph materialization in [tests/spot_lazy_buchi_view_test.cc:117](../../tests/spot_lazy_buchi_view_test.cc#L117), including `spot::are_equivalent`, cursor/acceptance edge cases, and tiny explicit fixed-K games. Reuse sparse exact-engine/mutation tests in [tests/spot_provider_replay_test.cc:1542](../../tests/spot_provider_replay_test.cc#L1542). The Python comparison [benchmarking/spot-provider-replay.py:65](../../benchmarking/spot-provider-replay.py#L65) plus [tests/pytest/test_spot_provider_replay.py:94](../../tests/pytest/test_spot_provider_replay.py#L94) checks exact construction/options and independent certificates; it is a fixed-K comparison, not a language-equivalence substitute. There is no existing closure-provider language orchestration to claim as already complete.

### P3 — storage profile gates

`LossSet::insert` still performs the guarded deep copy `generators_[write] = generators_[read]` ([src/solver/spot_lazy_game.hh:669](../../src/solver/spot_lazy_game.hh#L669)); Rank owns a vector ([src/solver/sparse_forward_rank.hh:107](../../src/solver/sparse_forward_rank.hh#L107)). P3a can change that distinct-slot assignment independently and test proof-ID alignment and moved-from lifetime; P0 deliberately leaves it intact.

| Gate | Existing trustworthy evidence | Sampled sparse-search CPU percentage / decision |
|---|---|---|
| P3b broad node scan ≥10% | A2b: tombstones median 97.8% (49.8–99.8%), prefilter rejects 1.8%, exact compares 0.5% of checked nodes, n=13. | Not recorded; **gate unevaluated**. Counts are not CPU shares. |
| P3c LossSet ≥10%, material queries with ≥128 live generators | Existing scan/antichain peaks lack CPU attribution and live-size-at-query distribution. | Not recorded; **gate unevaluated**. Peak alone cannot satisfy the query-distribution condition. |
| P3d payload allocation/copy ≥15%, or duplicate ratio ≥2 and ≥25% retained storage | Snapshot rank-byte totals lack duplicate-content and allocator denominators. | Not recorded; **gate unevaluated**. |

Evidence: [benchmarking/coverage-frontier-20260912/a2b-profile.md:44](../../benchmarking/coverage-frontier-20260912/a2b-profile.md#L44); [benchmarking/p4-forward-expansion-profile.tsv:1](../../benchmarking/p4-forward-expansion-profile.tsv#L1) has legacy forward node/action counts, not sparse CPU samples. Retained `g2s/*.perf` contains `perf stat` totals (cycles, instructions, LLC misses, branch misses), e.g. [/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-closing/g2s/candidate-syntcomp25-lift_pb_3_pe_-r1.perf:1](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-closing/g2s/candidate-syntcomp25-lift_pb_3_pe_-r1.perf#L1); there is no stack-sampled perf.data in S0/A2b/closing or the demand-sparse directory. Those totals cannot assign CPU to any of the three gates.

Freeze these five existing search-reaching targets for a later bounded profiling task: `Alarm_a5f99bc6.ltl`, `F-G-contradiction-110.ltl`, `robot-to-target-charging10.ltl`, `infinite-race-u12.ltl`, `workstation_resupply_pb_3_pe_.ltl`. Their record:1 evidence is in the P1b table. The same five samples can decide each CPU gate; no separate campaign per storage layout. Exact minimal sampling commands below are **proposed, not executed**, and produce diagnostic samples, not headline timings. Run within the existing future profiling task’s whole-invocation memory/swap controls; this bare command is not itself an 8G cgroup runner.

```sh
BASE=/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-closing/bin/candidate-f7919b83
CORPUS=/home/gperez/GIT-repos/acacia-bonsai/tlsf-corpus
OUT=/tmp/demand-sparse-perf
mkdir -p "$OUT"
for id in Alarm_a5f99bc6 F-G-contradiction-110 robot-to-target-charging10 infinite-race-u12 workstation_resupply_pb_3_pe_; do
  perf record -e cycles:u -F 199 --call-graph dwarf,16384 -o "$OUT/$id.data" -- \
    timeout --signal=TERM --kill-after=1s 17s "$BASE" \
      --arms unreal:formula:spot-guarded-sparse --candidate-mode only -T "$CORPUS/$id.tlsf"
  perf report --stdio --children --percent-limit 0 -i "$OUT/$id.data" > "$OUT/$id.report"
  perf script -i "$OUT/$id.data" > "$OUT/$id.stacks"
done
```

Use samples inside sparse Search exploration, excluding verifier call stacks, as the denominator; report row/BDD time as part of inclusive search. Attribute non-overlapping self cost to (b) enqueue-loss broad node traversal, (c) LossSet filtering/exact comparison, and (d) rank-payload allocator/copy stacks, with uncertainty/sample counts. BDD/translation costs must not be mislabeled as scans. If (c) crosses 10%, collect one bounded existing-replay trace extension recording live generator size at each query to test ≥128; sampling alone does not establish that condition. If (d) crosses 15%, its CPU gate suffices; otherwise a separate retained-byte/normalized-duplicate measurement would be needed, not inferred from capacities. Unsupported perf facilities leave gates unevaluated.

### P4 — one evidence-selected four-arm screen

The best-supported **candidate for screening**, not an admitted replacement, swaps `real:small:forward` for `real:small:spot-guarded-sparse` and retains `real:small:backward`, `unreal:formula:spot-guarded-sparse`, `unreal:automaton:forward`. This minimizes the isolated-arm displacement among the five relevant behaviors: the union loses one panel instance on removing forward-real, versus two panel plus one hard-set instance on removing backward-real. No existing report measures this exact four-arm replacement.

Evidence for new capability: five named gains in [benchmarking/COVERAGE-DIRECTED-FORWARD-SPRINT.md:67](../../benchmarking/COVERAGE-DIRECTED-FORWARD-SPRINT.md#L67) — `SPIPureNext`, `heim-double-x-real`, `ordered-visits-choice-real`, `robot_grid_pb_5_5_pe_`, `thermostat-F-real`, each solved in every retained five-arm repetition, roughly 0.02–9 s. Hard-set isolated results agree ([benchmarking/ADAPTIVE-PORTFOLIO-OTFUR-SPRINT.md:200](../../benchmarking/ADAPTIVE-PORTFOLIO-OTFUR-SPRINT.md#L200)). The displaced forward-real arm uniquely contributes `workstation_resupply_pb_3_pe_` among the five selected arm behaviors on the panel; it has no unique hard-set solve. Retaining backward-real protects `amba_decomposed_arbiter_pb_5_pe_`, `collector_v2_pb_12_pe_`, and hard-set `Alarm_a5f99bc6`. These are set differences over existing summary rows, not a new run.

| Displacement/gain evidence | File:line |
|---|---|
| `workstation_resupply_pb_3_pe_.ltl` | [benchmarking/portfolio-evidence-20260910/panel-census/real-small-forward-summary.tsv:165](../../benchmarking/portfolio-evidence-20260910/panel-census/real-small-forward-summary.tsv#L165) |
| `amba_decomposed_arbiter_pb_5_pe_.ltl` | [benchmarking/portfolio-evidence-20260910/panel-census/real-small-backward-summary.tsv:55](../../benchmarking/portfolio-evidence-20260910/panel-census/real-small-backward-summary.tsv#L55) |
| `collector_v2_pb_12_pe_.ltl` | [benchmarking/portfolio-evidence-20260910/panel-census/real-small-backward-summary.tsv:65](../../benchmarking/portfolio-evidence-20260910/panel-census/real-small-backward-summary.tsv#L65) |
| `Alarm_a5f99bc6.ltl` | [benchmarking/portfolio-evidence-20260910/hard-set/real-small-backward-summary.tsv:5](../../benchmarking/portfolio-evidence-20260910/hard-set/real-small-backward-summary.tsv#L5) |
| `SPIPureNext.ltl` | [benchmarking/portfolio-evidence-20260910/hard-set/real-small-spot-guarded-sparse-summary.tsv:41](../../benchmarking/portfolio-evidence-20260910/hard-set/real-small-spot-guarded-sparse-summary.tsv#L41) |

Displaced-arm sentinels matter: the closing five-arm race robustly **lost** `workstation_resupply_pb_3_pe_` (0/5 versus four-arm 4/5), with partial `sort50` loss (3/5 versus 5/5), at [benchmarking/COVERAGE-DIRECTED-FORWARD-SPRINT.md:70](../../benchmarking/COVERAGE-DIRECTED-FORWARD-SPRINT.md#L70). P4 must preserve them, not trade them for the five gains. `p1-pairs.md` [benchmarking/coverage-frontier-20260912/p1-pairs.md:24](../../benchmarking/coverage-frontier-20260912/p1-pairs.md#L24) has only a two-solve, ~0.2-s mean PAR2 spread on 180 and no separable fixed-pair winner; closing D3 has rf-ugf=1146 and pair oracle=1163 versus four-arm=1174, five-arm=1175 ([benchmarking/COVERAGE-DIRECTED-FORWARD-SPRINT.md:73](../../benchmarking/COVERAGE-DIRECTED-FORWARD-SPRINT.md#L73)). Keep both unreal arms. The older 400%-quota race curve and newer no-quota closing runs are distinct identities, not pooled timing cells.

### P5 — missing report fields

At [benchmarking/cactus-report.py:197](../../benchmarking/cactus-report.py#L197), the Markdown summary already has series, solved, N (`of`), PAR-2 total, total solved time, TIMEOUT, RESOURCE_LIMIT, UNKNOWN, ERROR and SYFCO-FAIL. Plan §9.5 still needs **REAL, UNREAL, PAR-2 mean, raw dataset hash**, and explicit **memory-failure and error/crash subtype counts** when raw sidecars distinguish them. Existing RESOURCE_LIMIT is broader than proven memory failure; ERROR does not split crashes. TIMEOUT and UNKNOWN are already present. Equal-ID checks and PNG/PDF plotting already exist; do not rebuild them. The adapter’s status/exit preservation limitations are recorded above. No plotter or conversion edit in P0.

## P0 lifecycle repair and evidence trust

- Reused unchanged: atomic snapshot rename, opt-in JSONL milestone history, harness invocation directories, PID identity, monotonic steady clocks, reporter sink ownership, lazy cross-K RowStore, exact search and checkers. The replay’s separate K-loop reset schema was already implemented and is not replaced.
- Added only missing worker-record state: schema version 2, invocation directory + PID/sequence worker ID, monotonic attempt and segment IDs, current K/search_started, attempt-end outcome/evidence, observed return/exception, and additive cumulative fields. Current-attempt keys are erased at the next start; cumulative keys survive. Fallback resets provider-specific metrics even if the declined factory never started K. Requested arm identity remains available separately from effective segment identity.
- Translation/factory, preprocessing, Boolean discovery, action construction, search and verification have explicit entry snapshots, including both equivariant K loops and the Spot fast path. Direct Spot and empty-language decisions have no K field. Spot uses an effective `spot-fast-det`/`spot-fast-gfg` backend; a decline resumes preprocessing in a new segment. GFG precondition verification is labeled separately from game solving; no certificate check is fabricated for deterministic Spot or equivariant fixed points. Parent SIGKILL/timeout cannot run the destructor and leaves worker_end=unobserved; no parent outcome is fabricated as a worker return. Exceptions are recorded where observable. Legacy fixed-point and fixed-K proof evidence is named distinctly from independently verified guarded certificates.
- Search destruction publishes final counters before attempt-end on the lazy path. `search_ms`/`verification_ms` and new loss-check calls/time are attempt values; `cumulative_*` sums observed ended-attempt contributions, with separate started/ended/completed counts. Missing fields remain absent. A partial killed attempt is not silently added as completed work. Existing `row_generation_ms`/wrapper counters retain their provider/store lifetime; `attempt_row_generation_ms`/`attempt_rows_generated` are explicit deltas. Rows are an inclusive subcomponent of search/verification, never added again to wall time.
- Diagnostics off: new lifecycle calls guard the inactive record before creating strings/maps or serializing; empty Record containers allocate no nodes. Reporter string_view arguments avoid new key allocations when its sink is absent. No per-node disk writes were added: snapshots remain phase/attempt boundaries and final destruction.
- Consumer: cohort TSV is optional; arbitrary zero/multiple sparse arms work, PID/sequence separates same-arm transformed jobs, legacy absent-search markers are conservative, and an invalid trailing snapshot can recover only complete history-prefix lines. Corrupt interior history still fails closed. Campaign timeout and decisive-portfolio cancellation are not normal returns. New worker-end fields are exported without removing old columns.
- Existing fixtures extended: diagnostics-attempt exercises two K attempts with no stale counters, fallback, exception, actual parent SIGKILL, and inclusive row accounting; native TLSF worker pairs inspect real histories and cumulative sums, plus fast-path enabled/declined cases and decomposition; the worker zero-state return helper is exercised for both polarities; equivariant fixtures cover closure/sweep wins and three losing K attempts; Spot fixtures cover deterministic/GFG losses and verification boundaries; Python phase tests cover legacy labels, cancellation/exception, truncated trailing records, non-trailing corruption and arbitrary arm counts.


One follow-up return-route audit: [syntactic bypass/forced-output contradiction](../../src/solver/solver_invoker.cc#L752), [degenerate I/O and no-input synthesis](../../src/solver/solver_invoker.cc#L720), and [realizability simplification](../../src/solver/solver_invoker.cc#L1006) run before the captured `run_one_ltl` record exists; simplification itself returns no solver verdict. They cannot leave a captured search mislabeled. [Decomposition](../../src/solver/solver_invoker.cc#L862), [safety-core witnesses](../../src/solver/solver_invoker.cc#L842), and monolithic dispatch invoke that runner for each solved subproblem. [Empty translation](../../src/solver/solver_invoker.hh#L43) now closes an unbounded `empty-language` attempt without claiming search. Input-push limits, census-only exits and empty-after-preprocessing return inconclusive and correctly retain the phase they reached. [Backward/local-certificate](../../src/solver/k_bounded_safety_aut.hh#L217), [forward](../../src/solver/forward_k_bounded_safety_aut.hh#L97), [frozen guarded/sparse](../../src/solver/solve_game_impl.hh#L202), and [lazy/eager provider](../../src/solver/spot_lazy_worker.hh#L101) decisive returns follow search entry and attempt completion. Forward, guarded and lazy fallback branches select fresh segments before conventional solving; the equivariant decline does the same at [solve_game_impl.hh:188](../../src/solver/solve_game_impl.hh#L188). The [elevator SCC helper](../../src/aut_preprocessors/elevator.hh#L162) is an auxiliary preprocessing check; its direct Spot helper call leaves worker phases untouched, covered by the Spot fixture. No other decisive captured pre-search return was found.

Read-only consumer verification: the repaired summarizer processed all 1,405 existing D7 records successfully, without a cohort argument, writing only `/tmp/p0-d7-phases.tsv` and `/tmp/p0-d7-phases.md`. These are derived verification scratch files, not new observations; the retained close directory was not changed.

Historical trust ledger: S0/failure-phases fields support last observed explicit boundary, reached K and labeled snapshot counts, not completed stage durations or cumulative K cost. A2b supports scan and verifier BDD composition for 13 measured last-verified attempts, not CPU attribution. T1/s2-ablation/p1-pairs support their own declared configurations, verdict sets and mechanisms; do not transplant their wall times to the new epoch. D7 supports parent failure status and explicit sparse progress; legacy backward/forward preprocessing labels cannot rule out search. These limits leave P1a cumulative target ranking and P3 CPU gates unmeasured, while P2’s implementation boundaries are fully resolved.

## Verification and local build identity

`meson setup build` created the requested debug build in this worktree with `PKG_CONFIG_PATH=/usr/local/lib/pkgconfig`; all compilations used `-j 8`. Local build options then enabled TLSF, forward safety and Python bindings so the touched worker paths and available Python tests could run. No preset/registry or frozen binary was changed. Unit result: **48 passed, 0 failed**, including diagnostics, game-backend, native TLSF worker-boundary and sparse provider replay suites. Configuration validation succeeds with the existing documented TLSF-default divergence. `git diff --check` is clean.

The worktree's populated dependency directories are source snapshots without submodule `.git` metadata. The manifest uses superproject gitlink pins, not an accidental superproject `rev-parse` result. A content check against the pinned objects in the main checkout matched all 297 tracked posets files and 567/568 TLSF files; the sole difference is `scripts/version-archive.txt`, whose Git archive placeholders are expanded to `describe=v1.5.0-7-gb42d5ef`, `commit=b42d5ef`. The dependency pins remain those in §0.1.

The exact broad Ruff command reports **135 pre-existing findings**, all inside the untouched `tests/syntcomp-benchmarks` submodule; excluding only that vendored directory passes. Full pytest collection with `PYTHONPATH=build/src/python:benchmarking` is blocked by missing Python-3.14 `spot`; the available installed Spot Python extension targets 3.13 and cannot serve this interpreter. The remaining suite passes **642 tests, 1 skipped** (excluding the two binding modules); a separate run of the binding module's tests that do not require Spot passes **11 tests, 2 deselected**. No dependency was installed or mismatched Python extension loaded to conceal this limitation.

## Executed in this sprint

This append updates the P0-only inventory above; the original inventory and manifest remain historical. Newly executed elsewhere in the sprint: the single [P0 cohort/worker/profile diagnostic](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/p0-diag) ([driver](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/scripts/p0-diag.sh)); five-pair [P3a](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/screens/p3a) and [P1a](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/screens/p1a) screens ([drivers](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/scripts/seq-screens1.sh)); the **51 s diagnostic only** [GF-G follow-up](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/diag-gfg7); one [P2 exploratory portfolio round](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/screens/p2-explore); and [P2 mechanism plus A/B/C/real-arm diagnostics](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/diag-p2mech) ([mechanism driver](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/scripts/diag-p2mech.sh), [eager driver](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/scripts/diag-p2eager.sh)). Tests and mutation reviews are recorded under [codex](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/codex), summarized in [decisions.md](decisions.md). P4 is **PENDING — screen running**, specified by [seq-p4.sh](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/scripts/seq-p4.sh).

Reused: the frozen corpus/map, historical closing/isolated-arm evidence and controls already inventoried above, the existing coverage runner, worker reporter, provider replay and shared PAR-2/cactus tooling. These documentation reports read saved observations only; no solver, build, test, profiling or benchmark was run for this task. Small source hashes and paths are in [evidence-sha256.tsv](evidence-sha256.tsv); bulky raw logs were not copied.

Invalidated inference, not deleted evidence: the P1a flag screen used `p1a-d14cf955`, which **contained rejected P3a**; it is not a completed retention check on the P3a-free candidate. P3a fails admission; its 51 s answers never count as 17 s coverage. P2's one-round gains are exploratory and include a loss; its first-row blocker leaves code/tests on the research branch. The §5.9 diagnostic also inherited P3a rather than meeting the originally requested P3a-off condition. Neither diagnostic solo-arm wins nor historical five-arm unions establish the actual P4 replacement. Final combined-candidate checks and §9.6 closing artifacts remain pending.
