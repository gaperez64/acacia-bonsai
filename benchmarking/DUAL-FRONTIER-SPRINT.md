# Dual-frontier representation sprint

**Disposition: research-only.** The exact representation, conversion, predecessor and fixed-K
replay stages are implemented. No production solver path or default has been added. The real
capture/campaign manifest is still empty, so the P3/P4 performance gates have not been evaluated
and P5 is not admitted.

## Hypothesis and boundary

The experiment asks whether some expensive rank downsets have a much smaller exact
minimal-exclusion frontier and whether that advantage survives Acacia's input-conditioned
predecessor and a complete fixed-K solve. It does not change the interpretation of forward/OTFUR
losing knowledge, TLSF translation, synthesis, shields, Python export, the critical picker or
bound lifting.

The implementation has one authoritative representation at a time:

- `MaxIncluded`: maximal generators of the represented downset;
- `MinExcluded`: minimal generators of its complement inside the exact safe box.

Every conversion/update produces a complete replacement or a typed resource failure. Temporary
frontiers are never published. Negative membership rejects points outside the box before consulting
the exclusion frontier, and negative action preimages explicitly include counting and Boolean-tail
overflow.

## Frozen provenance

| item | value |
|---|---|
| source baseline | `50384cf69a733199fa17c9c571da1761d8451991` |
| Posets gitlink | `139e14336b7a1f0bc064022e587ea4e1b9a81427` |
| tlsf-tools gitlink | `b42d5ef4a680252e04820ac7f073f5d786a43f7c` |
| SYNTCOMP gitlink | `4105caf1f1e5fd3b76657879bfce8021d130cbde` |
| compiler | GCC 15.3.1, debugoptimized checked build |
| CPU | Intel Core i3-8100T, 4 cores / 4 threads, x86-64 |
| configuration group at start | `otf_sparse_formula`, `best_decomp_rank_bucketed_semantic_mona`, `best_four_arm_bboxtree`, `best_decomp_mona_any` |
| checked generated-config SHA-256 | `4fe9f4753d73d87eccfafc2abefe30ce96994c2dae8c084df5e24d4d59b0dbd8` |
| checked replay SHA-256 | `df6bfa8801dc6a59040933a2ccc9e91cf9a90b2dd955c1f0474e1071dbda0498` |

The checked replay binary is a scratch correctness artifact, not a frozen timing binary. A real
campaign must record its own optimized binary hash in every manifest row.

## Implemented stages

### P1: exact region and predecessor kernels

`src/research/dual_rank_region.*` provides:

- immutable domain identity, K/Boolean bounds and compatibility checks;
- canonical positive/negative antichains and safe-box membership;
- transactional, cooperatively budgeted conversion in both directions;
- exact same-form union/intersection and charged cross-form equality;
- separate work, deadline, live-generator and accounted-workspace outcomes.

`src/research/dual_rank_predecessor.*` provides:

- the existing positive backward-transform replay in the same interface;
- exact threshold preimages with absent-rank handling;
- explicit unsafe/overflow preimages even for an empty exclusion frontier;
- whole-generator threshold intersections and controller-action failure intersections;
- the exact contracting input update, including mathematical zero-action inputs.

The shared CPre and all-input readers now reject unsupported versions, invalid K/splits and ranks,
both invalid transition indices, non-Boolean increments, misordered/missing sections, malformed or
trailing integers, declared-count mismatches and truncated after-regions. Existing schema-2 CPre and
schema-1 all-input files remain the formats consumed.

### P2/P3: conversion and same-event replay

`acacia-dual-frontier-replay` supports:

```text
--task convert --dir DIR
--task cpre --dir DIR
--max-work N --max-workspace-bytes N --max-frontier N --deadline-ms N
```

Conversion reports the complete alternate frontier and a separately budgeted round trip. CPre
reports the positive baseline, entering conversion, resident negative update, return conversion,
cold total, threshold/join counts, accounted peaks and exact comparison with the captured after
region. `--loop N` isolates one event so the reported child high-water RSS is not contaminated by
other cases. A censored conversion is reported by its resource reason, never as an alternate size.

### P4: persistent fixed-K research driver

The same executable supports:

```text
--task solve --dir DIR --k K --mode positive|negative|auto
```

All modes use the complete recorded input order and require a whole unchanged sweep for `win_k`.
They start from the whole safe box and stop early only if the recorded initial rank leaves the
candidate region. Negative mode never calls the maxima-enumerating critical picker. Auto probes at
committed boundaries with cooldown, growth checkpoints, a cumulative probe-work cap and at most
eight conversions. Its deliberately simple structural estimate requires either a predicted 2x
service gain or 4x storage gain without predicted slowdown, plus repayment of conversion work.
Failed probes leave the region unchanged. A failed native auto update may retry through the other
orientation within the original per-update work/deadline allowance.

The auto constants are experimental and logged through probe/update/conversion counters. They are
not a trained policy and do not route on benchmark names, future regions or oracle verdicts.

`benchmarking/dual-frontier-study.py` runs manifest rows in timeout- and address-space-bounded child
processes, preserves incomplete captures and failures, and emits raw TSV plus a Markdown outcome
summary. It does not build Acacia or hide additional solver workers.

## Correctness evidence run locally

The focused checked targets compiled and passed:

| check | result |
|---|---:|
| exhaustive downsets on the mixed `[-1,1] x [-1,0]` box, both forms, conversions and algebra | passed |
| explicit membership, empty/full/bottom, zero dimension, K=127 and incompatible domains | passed |
| independent-forbidden-pairs expansion and forced transactional frontier abort | passed |
| 300 deterministic random mixed-domain action/input differentials against `apply_forward` | passed |
| K=1 accepting overflow, absent state, zero actions and one transition-free action | passed |
| complete-sweep quantifier regression and positive/negative final membership | passed |
| predecessor-reversal `2^5` expansion and transactional work abort | passed |
| strict parser valid/malformed/truncated/count/index/increment/rank cases | passed |
| synthetic `convert`, `cpre`, and positive/negative/auto `solve` replay smoke | passed |
| complete checked unit suite | 54/54 passed |
| pinned Posets standalone suite | 18/18 passed |
| focused Clang 21 ASan/UBSan kernels, parser and replay smoke | passed |

Commands:

```sh
PKG_CONFIG_PATH=/usr/local/lib/pkgconfig meson setup build-dual-checked \
  --buildtype=debugoptimized -Dbuild_research_tools=true \
  -Dacacia_enable_diagnostics=true
meson compile -C build-dual-checked \
  dual-rank-region-test dual-rank-predecessor-test dual-replay-parser-test \
  acacia-dual-frontier-replay
build-dual-checked/tests/dual-rank-region-test
build-dual-checked/tests/dual-rank-predecessor-test
build-dual-checked/tests/dual-replay-parser-test
tests/check-dual-frontier-replay.sh \
  build-dual-checked/src/acacia-dual-frontier-replay
meson test -C build-dual-checked --no-rebuild --suite unit \
  --print-errorlogs --num-processes 1
```

The first build attempt exhausted the environment's `/tmp` quota. Re-running serially with compiler
temporaries under the workspace completed the whole checked build, after which all 54 unit tests
passed. A focused Clang 21 `address,undefined` sanitizer build also passed the three new test
executables and replay smoke. LeakSanitizer was disabled because it cannot operate under the
execution environment's ptrace supervision; ASan and UBSan remained enabled with abort-on-error.
The parent-pinned Posets commit was also built in an isolated plain-debug directory and passed all
18 standalone tests.

## Campaign status

No real snapshot cohort or optimized timing campaign has been run in this implementation session.
Accordingly:

| stage | measured rows | gate result |
|---|---:|---|
| static conversion census | 0 | unrun |
| captured same-event CPre | 0 | unrun |
| persistent fixed-K solves | 0 | unrun |
| production campaign | 0 | not admitted |

`dual-frontier-manifest.tsv` and `dual-frontier-results.tsv` intentionally contain headers only.
They are not evidence of compression, speedup, memory savings or coverage. Populate the manifest
from complete captured events, preserve `.skipped` cases, and replace the results file with the
bounded runner's raw output.

## Admission decision and deferred integration

The experiment remains research-only until real P3/P4 rows show zero mismatches and the predeclared
multi-family operation/end-to-end benefit. Production integration is therefore deferred in full:

- the critical picker still consumes positive maxima;
- raising K invalidates the dual domain and must start a fresh fixed-K region;
- synthesis, strategy extraction, shields and Python winning-region export need positive interfaces;
- forward/OTFUR losing antichains remain proof knowledge, not exact complements of candidate regions;
- worker polarity and original-LTL verdict contracts are unchanged;
- no compile-time option, preset, portfolio arm or backend dispatch was added.

If the real gates fail, retain these exact tools, fixtures and negative measurements and do not add a
dormant production path. If they pass, production integration belongs in a separate change with the
repository's G0-G5 gates and matched frozen binaries.
