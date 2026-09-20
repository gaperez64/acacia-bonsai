# Dual-frontier sprint: delta retarget and closing decision

**Disposition: negative research result; no production path.** The exact dual-frontier machinery
remains useful research infrastructure, but the complete-complement census and exact CPre-delta
campaign fail Gate C0. The lazy/subtractive prototype is therefore not admitted, and no solver,
picker, portfolio, polarity, strategy-extraction or outer-`K` behavior is changed.

## Retargeted question

The original sprint asked whether a rank downset should sometimes be stored by its complete
minimal-exclusion frontier. Positive maxima can be operationally better even when a complement is
smaller: `apply_backward()` maps one maximum to one upper corner, and the critical picker already
enumerates maxima. The retarget instead asks whether one contracting update has a small boundary:

```text
F_after = F_before ∩ CPre_i(F_before)
F_after = F_before ∖ ↑B_delta
```

The exact reference construction is:

```text
B_before = Min(P ∖ F_before)
B_after  = Min(P ∖ F_after)
B_delta  = {b in B_after | b is in F_before}
```

Success is published only after reconstructing `Min(P ∖ F_after)` from `B_before ∪ B_delta` and
checking exact equality. A budgeted or timed-out conversion is censored, never interpreted as a
large complement or delta.

## Implementation

`src/research/dual_rank_delta.*` adds a transactional exact-delta kernel. It:

- requires compatible canonical positive and complete-complement regions;
- proves `F_after ⊆ F_before`;
- reports both exact old-generator survival and old-maximum containment;
- filters and minimizes the newly excluded boundary;
- reconstructs the complete after-complement and certifies exact equality before success;
- accounts work, retained storage, peak workspace and typed resource outcomes.

`acacia-dual-frontier-replay --task delta` reports:

```text
loop, K, dimensions, actions,
before_maxima, after_maxima,
before_min_excluded, after_min_excluded, delta_min_excluded,
positive_maxima_still_generators, positive_generator_survival_fraction,
before_maxima_still_contained,
delta_construction_status,
conversion_work_before, conversion_work_after, delta_work, delta_peak_workspace
```

It also records conversion/delta time, exact certification and child peak RSS. `--loop` now selects
the named event before parsing siblings, so a preserved truncated capture cannot contaminate an
independent row.

`benchmarking/dual-frontier-study.py` runs each event in an address-space- and timeout-bounded child,
keeps incomplete and censored rows, reports by family and frontier-size bucket, and evaluates the
predeclared Gate C0 without relaxing it. `benchmarking/freeze-dual-frontier-manifest.py` hashes each
source, capture event, capture binary, replay binary and generated configuration before freezing the
manifest.

Snapshot instrumentation gained `ACACIA_ANTICHAIN_SNAPSHOT_CPRE_MIN_FRONTIER`, allowing the CPre cap
to target large frontiers. An unrelated Spot HOA serialization refusal is now preserved as
`automaton.hoa.skipped`; exact CPre capture continues because replay uses `meta.tsv` and rank actions,
not the HOA. This changes diagnostics only.

## Frozen campaign

| item | value |
|---|---|
| source base | `b9a33b32e50158975bcbd326f481639a316149e7` |
| compiler | GCC 15.3.1, `debugoptimized` correctness/capture build |
| automaton preprocessor | `aut_preprocessors::standard` |
| game path | classic backward; equivariant pre-pass disabled in the capture build |
| capture solver SHA-256 | `f8ec5bb7636ed98a45ffd184b0ea96d4ec6895d49d13d7a15ca3e813ec18f4a2` |
| replay SHA-256 | `35157f1f946300e24f28b7efb9908daea89362e9785bbe1e3b033512539642ca` |
| generated config SHA-256 | `35d123b0bb0ff5a0f465f673e9fd4641ad351a76bb1f7d5d9f661573dcc0fb64` |
| exact event rows | 25 total: 24 complete, 1 preserved truncated |
| large complete rows | 23 across 4 unrelated families |

The large cohort contains `arbiter_on_inpchange`, `prioritized_arbiter`, `round_robin_arbiter` and
`simple_arbiter_with_hints`, spanning 1,031 to 48,264 before-maxima. `06.ltl` is the small control.
Capture runs are not timing evidence; one timed-out solver still left ten fully closed events, which
are independently parseable and hashed. The approximately 508 MiB scratch captures are not checked
in. Their per-event hashes and all raw replay outcomes are frozen in the manifest/results tables.

Every replay child used:

```text
process timeout       15 s
address-space limit   1 GiB
operation work limit  100,000,000
accounted workspace   512 MiB
live-generator limit  200,000
operation deadline    5 s
```

## Phase A: final whole-complement census

Raw rows are in `dual-frontier-conversion-results.tsv`; the outcome summary is
`DUAL-FRONTIER-CENSUS.md`.

| cohort | rows | complete | work-limit censored | preserved incomplete |
|---|---:|---:|---:|---:|
| small control | 1 | 1 | 0 | 0 |
| large frontiers | 23 | 0 | 23 | 0 |
| incomplete capture | 1 | 0 | 0 | 1 |

The control converted exactly (`positive_count=1`, `negative_count=0`, round trip exact). Every
large whole-complement conversion exhausted the fixed 100M-work budget. This is conversion
censorship, not evidence about the unobserved complement size. It is evidence that ordinary
complete-frontier switching is not operationally available under a modest fixed budget.

## Phase B/C: exact delta campaign

Raw rows are in `dual-frontier-results.tsv`; family/bucket summaries and the machine-evaluated gate
are in `DUAL-FRONTIER-DELTA-CAMPAIGN.md`.

| result | rows |
|---|---:|
| exactly certified delta | 0 |
| work-limit censored | 24 |
| preserved incomplete capture | 1 |
| semantic mismatches | 0 |

The small control's before-complement completed in Phase A, but its after-complement did not complete
within the same fixed budget, so it correctly remains a censored delta row. Failed rows do not enter
ratio or survival summaries.

Gate C0 fails:

- 0/20 required complete large events;
- 0/3 required represented families among complete large events;
- no certified large-event delta-size or unchanged-maxima median is available;
- exact row-level semantic mismatches: 0.

The gate is not relaxed. Phase D and the same-schedule materialization comparison are not executed.

## Correctness evidence

The exhaustive tiny-domain test enumerates every pair `F_after ⊆ F_before` over a mixed `K=1`
domain and verifies every point:

```text
contains(F_after, x)
==
contains(F_before, x) && !exists(b in B_delta: b <= x)
```

Named cases cover empty delta, one removed maximum, incomparable replacement maxima, replacement of
a redundant old complement generator, Boolean-tail coordinates, `-1` absence, a non-contracting
input and transactional budget failure. The replay smoke covers exact delta output and isolation
from a truncated sibling.

The focused kernels, parser and smoke tests pass, as does the complete unit suite. The campaign has
zero semantic mismatches; resource-censored rows are excluded from performance summaries.

## Final decision

| Observation | Decision |
|---|---|
| Complete complements are not constructible on any large event under the fixed modest budget | close ordinary dual-frontier switching |
| Exact delta construction depends on two such complements and yields no complete large rows | Gate C0 fails; do not infer delta size |
| Gate C0 fails before a lazy representation is justified | do not build or productionize lazy subtraction |

Retain the exact region, predecessor and delta machinery plus the negative census. Revisit this
representation family only with a genuinely direct exact-delta construction justified by new
multi-family evidence; do not tune automatic orientation or add a dormant production path from the
present data.
