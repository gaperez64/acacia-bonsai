| Hook / corruption / control | Expected telemetry | Runtime assertion |
|---|---|---|
| `env_rank_fault` 1,2,3,4,5,7,9,10,12 | STOPPED/error | `native_failure_census`: exact route/stage/cause, no DECLINE, verified direct fallback |
| `env_rank_fault` 6,8 | DECLINE/applicability | `native_failure_census`: bounded grammar mismatch or completed certificate rejection, verified direct fallback |
| `env_rank_fault` 13 | STOPPED/resource | `native_failure_census`: rank-depth overflow, verified direct fallback |
| `lift_test_fault` 1 | STOPPED/error | `native_failure_census`: game/seed binding, verified direct fallback |
| `lift_test_fault` 2,4 | STOPPED/resource | `native_failure_census`: checker capacity/allowance, verified direct fallback |
| `lift_test_fault` 3 | DECLINE/applicability | `native_failure_census`: CHECK_OK/CERT_FAILED, verified direct fallback |
| `lift_test_fault` 5 | VERIFIED, no failure | `native_failure_census`: recovered game mutation with matching binding, one final check |
| `lift_test_fault` 6,8 | STOPPED/error | `native_failure_census`, `native_corruption_guards`: corrupt R justice metadata or verified U verdict; verified direct fallback with unchanged proof |
| `lift_test_fault` 7 | STOPPED/error | `native_failure_census`, `native_lift_api`: corrupt verified R verdict; legacy DECLINED, no published proof, both proof orders |
| `both_test_seed_fault` 1 | DECLINE/applicability | `native_failure_census`: mixed seed polarity, verified direct fallback |
| `both_test_seed_fault` 2 | STOPPED/error | `native_failure_census`: unknown seed, verified direct fallback |
| `both_test_seed_fault` 3 | STOPPED/resource | `native_failure_census`: U schema capacity, verified direct fallback |
| `both_test_seed_fault` 4 | STOPPED/deadline | `native_failure_census`: expired seed solve, no verdict |
| `both_test_check_fault` 1,2; U observer mutation before checking | STOPPED/error | `native_both_api`: late prepared-game/source binding; no verified result |
| `env_rank_test_apply_cap`, `both_test_env_rss_spike_bytes` | STOPPED/resource | `native_both_api`, `native_real_both_api`: cap/RSS cooperation, retained legacy status |
| Reduction `source-duplicate`, `formula-ap` | STOPPED/error | `attribution-contracts`: direct + all three lifting arms; both prepare APIs and legacy UNSUPPORTED/DECLINED |
| Reduction `monitor-incomplete`, `monitor-nondeterministic`, `monitor-acceptance`, `monitor-empty`, `monitor-sticky` | STOPPED/error | `attribution-contracts`: actual completed Spot monitor checks, all four arms |
| Reduction `transition-ap`, `transition-temporal`, `provenance-boolean` | STOPPED/error | `attribution-contracts`: original Boolean/AP validators, all four arms |
| Reduction `unsupported-class` | DECLINE/applicability | `attribution-contracts`: original structural recognition decline, all four arms |
| Preparation `provenance-duplicate`, `provenance-retry` | STOPPED/error | `attribution-contracts`: all three lifting arms and both versioned/legacy APIs; earlier integrity retained after strict applicability failure |
| Preparation `provenance-recover` | OK, no retained failure | `native_contract_api`: successful strict retry clears cause; exact preparation still rejects |
| `seed-null`; null, constraints/no justice, nonsingleton justice, nonconstant reset | STOPPED/error | `native_contract_validators`: all three seed solvers; `attribution-contracts`: shared seeds followed by verified direct fallback in both combined modes |
| U unnamed latch, missing unique bad predicate, nonsingleton justice, Skolem letter dependency | STOPPED/error | `native_env_contract_validators`: each original guard, immutable cause and legacy DECLINED; both environment/system dependency sets |
| Acacia `source-hash`, `swap-game`, `swapped-game-with-matching-hash`, `swap-certificate`, `wrong-side`, `wrong-method`, `missing-policy-hash`, `policy-hash`, `unexpected-region-policy-hash`, `corrupt-proof` | STOPPED/error | `attribution-contracts`: every hook in all three lifting arms, complete lifecycle, UNKNOWN, no DECLINE |
| Acacia `ACACIA_NATIVE_TEST_CORRUPT_PROOF` | STOPPED/error | `attribution-contracts`: direct proof parser/checker rejection, UNKNOWN |
| Acacia `r-decline` | STOPPED/resource | `attribution`: bounded schema resource exhaustion, verified direct fallback |
| Acacia `region-method` | VERIFIED (region) | `native-gr1-lift-cli` / `native-param-lift-cli`: resource-limited policy alternative recovers via region verification |
| Acacia exceptions `bad-alloc`, `standard`, `unknown` | STOPPED/resource, error, error | `attribution`: complete terminal UNKNOWN lifecycle, no DECLINE |
| Spot provider faults `allocation`, `length`, `capacity`, `bdd_growth` | STOPPED/resource | `spot-lazy-buchi-view`: original provider/BDD checks + serialized lifecycle |
| Spot provider faults `count`, `meaning`, `late_meaning`, `mark`, `ap_inventory`, `null_iterator`; missing provider, foreign/null state | STOPPED/error | `spot-lazy-buchi-view`: original contract checks, UNKNOWN, serialized lifecycle, no partial row |
| Unsupported PSL, arbitrary/incomplete acceptance, raw alternation | DECLINE/applicability | `spot-lazy-buchi-view`: legacy DECLINED + serialized lifecycle |
| Closure `FaultPoint::{branch,guard,interning,publication}` | STOPPED/error (`injected`) | `closure-buchi-provider`: each rollback point; `spot-lazy-buchi-view`: all 11 FailureKind lifecycle conversions |
| Closure cancellation and normalization/branch/guard/state/row/memory budgets | STOPPED/cancelled or resource | `closure-buchi-provider`: genuine fault origin; `spot-lazy-buchi-view`: every cause conversion |
| TAA rank node cap zero, `candidate=only/fallback` | STOPPED/resource | `attribution`: both modes; retained UNKNOWN or verified incumbent fallback |
| `ACACIA_TEST_ATTRIBUTION_PAUSE` at R/U/direct/checking; worker SIGKILL; parent SIGTERM | Parent STOPPED/deadline/signal/interrupted; incomplete child telemetry | `attribution`: selected route retained, never interpreted as applicability |
| `ACACIA_TEST_CHILD_MODES` / `ACACIA_TEST_DELAY_AFTER_REAP` (late, failed, killed, winner) | Parent lifecycle reason; late result rejected | `native-gr1-cli`: deadline/kill/reaping/winner attribution |
| Transport writer stall/delay, 512-byte pipe, full/broken/slow destination | Telemetry loss/incomplete; never a solver decline | `worker-record-transport`, `record-transport`: bounded nonblocking delivery, explicit dropped/incomplete records |
