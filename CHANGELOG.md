# Changelog

## Unreleased

- `--aiger-mode` selects Spot's AIGER encoder (default `isop`); unsafe `+dc`
  modes are rejected pending #466. `benchmarking/synthesis-size.py` accepts
  per-label `--extra-args` (#280).

- Existing formula workers try one-sided TLSF restrictions: input families share
  their first member (accept only UNREALIZABLE), or scalar outputs become false
  (accept only REALIZABLE). tlsf-tools `Restriction.lean` proves both transfers.
  Input attempts have a 1 s total budget; the output attempt has 200 ms, with
  512 MiB per attempt. Other outcomes resume the original solve. Synthesis skips
  them; disable with `--restriction-prepass off` (#451). The 17 s SYNTCOMP26
  screen adds ten solves with no losses (`restrict451-20261122`).

- tlsf-tools 9246712 adds checked parametric-support folds for constant-state bus-wide
  monitors such as load_balancer, plus the self-checking `tlsfobf` name/order obfuscator
  (#437, tlsf-tools #145).

- tlsf-tools 219270e adds checked monitor-state folds for
  binary-encoded index families. Both fold routes choose axes independently of names and
  verify target regions. Frontend expansion respects the lift budget; adaptation
  allocation failure reports a resource limit (#437).

## 2.10.0 (2026-11-20)

- Uses [posets 1.5](https://github.com/michaelcadilhac/posets/releases/tag/1.5)
  (a4387cb7), [tlsf-tools
  v1.12](https://github.com/gaperez64/tlsf-tools/releases/tag/v1.12) (da66170e) and Spot
  2.16.3. Spot fixes the wrong REALIZABLE obligation answer for `G i -> G(i <-> XXX
  false)` (#375, #439). Posets speeds up lazy KD intersection (#438); tlsf-tools adds
  the GR(1) region-certificate Lean proof and per-axis lift decline reasons. The lift
  result ABI changed: rebuild from scratch. Reasons reach phase/lifting reports;
  `--lift-max-predicate-arity` exposes the library's unchanged default.

- The default
  portfolio drops `unreal:automaton:forward`, leaving six arms; the arm remains
  selectable (#384). The first two `docker_default` presets use `closure-recurrence` for
  formula unrealizability, with `fallback` candidates and `low` effort for fallback
  translation. Synthesis retains `frozen-graph`. New `acacia_default_candidate_mode`
  defaults to `only`; runtime `--candidate-mode` overrides it (#407).

- Cap/recycle
  validation distinguishes durable final records from pipe deliveries (#443). Lazy KD
  epoch checks, a printer mutant and closure-row differential tests improve validation
  without changing production behaviour (#402). Enumerative closure expansion is
  test-only.

- Removes `--weakening extended`, `--weakening-attempt-ms` and
  `--weakening-total-ms` after zero-gain screens; basic/off remain. Removes the
  unused `timed_run_marker`, `MarkerTimeout`, `DEFAULT_BLOCKING_MARKERS` and
  `TIMED_RUN_MARKER` APIs.

## 2.9.0 (2026-11-15)

- Python registers I/O before translation, fixing input ownership that could produce
  wrong realizable answers (#393). It rejects out-of-range bounds/ranks instead of
  narrowing to signed char (#383), saturates successors instead of wrapping at 127
  (#389), and checks initial-state membership before picker-exhaustion success. The CLI
  was unaffected by #393.

- The shipped downset uses lazy KD trees with subtree/block
  bounds instead of rank buckets (#384). Sparse Oracle payloads split by mode, saving 96
  bytes per grouped prepared rank; native searches reduce hashing/copy costs. Native
  SIMD width replaces fixed 128 bits; `acacia_simd_width` accepts `native`, `compatible`
  and `max` (#380).

- Formula-unreal translation uses low effort; `--translation-effort`
  sets per-arm levels (#378). The default drops `real:small:forward`, leaving seven arms
  (#359).

- Dependency updates through posets 1.4 (7e75c1f) and tlsf-tools v1.11
  (1e94604) fix signed-bin/CST arithmetic, strong indexed next and debug/release OxiDD
  option layout; reduce allocations; share trie, antichain, reduction, rank and symmetry
  code; and improve installed headers, statistics and release tests. Earlier pins add
  `tlsfsym`, checked literal/range arithmetic and Lean proofs (9a2f748, 81b0e5b);
  4b69b83/383425a preserve preset options and SIMD widths. Acacia builds only the
  upstream helpers its tests need.

## 2.8.0 (2026-11-06)

- TLSF pipeline fuzzing checks parser/printer/allocation handling, native conversion
  against SyFCo, DPA languages against Spot and default-arm verdict agreement (#369).
  Seeds run in unit tests; weekly CI grows/replays corpora. tlsf-tools v1.9 fixes
  printed parentheses, finite-semantics strong-next deduplication and memory-failure
  handling; infinite-word verdicts were unaffected.

- Faster DPA construction visits
  reachable nodes and uses tagged probing. The default adds `both:parity:oxidd` beside
  `both:parity:spot`, reaching eight arms. Parity workers can exceed their equal memory
  share until the scope is nearly full; `--parity-memory-share equal` restores the old
  policy (#341).

- Known issue (#375, Spot #666): obligation synthesis can wrongly
  answer REALIZABLE, including for `G i -> G(i <-> X X X false)`. The affected arm
  remains in this release's default; Spot 2.16.3 fixes it in 2.10.0.

- Phase recording
  requests a 1 MiB Linux pipe, keeping default capacity if refused. Removes unsuccessful
  DPA route experiments and lazy parity arms, retaining eager parity and telemetry
  fixes.

## 2.7.2 (2026-10-27)

- MONA and semantic MONA retain empty responses (#335, #336). Dropping these legal
  choices could return UNKNOWN for realizable specifications such as `F (c <-> i) | G F
  c`; shipped Docker presets were affected. The bug could not produce a wrong decisive
  verdict.

- Shared equivariant solving falls back when a K raise would overflow its
  rank type (#337); the default schedule was unaffected. `proofs/` adds Lean proofs of
  guarded symmetry soundness (#331, #334); proof work found both fixes.

## 2.7.1 (2026-10-26)

Fixes a soundness bug: the equivariance detector accepted input/output swaps and could
wrongly answer REALIZABLE (#330). Permutations now preserve ownership. The default was
affected in 2.7.0; earlier releases with equivariance enabled may also be affected. Use
`--equivariance off` on older releases. Benchmark corpora showed no disagreement with
ltlsynt.

## 2.7.0 (2026-10-23)

- Faster semantic MONA endpoint decoding and explicit guard-letter actions for at most
  12 active APs preserve output bytes (#320, #321). BuDDy grows by up to 4M nodes
  instead of 512K; `-Dacacia_bdd_max_increase=0` keeps Spot's step (#319, #324). The
  gains cost higher memory peaks; see `perf-20261022` and `bddgrowth-20261023`.

- Reduces forward/symmetry search costs (#317, #318). Upstream OxiDD stacks fit
  `RLIMIT_AS`, and failed manager construction returns failure instead of panicking.
  tlsf-tools and posets move to main.

- Links yyjson statically (#291) and writes
  child-signal records atomically (#316).

## 2.6.1 (2026-10-18)

Tier 4 parity uses Spot directly; strpm-simd is removed (#282,
`tier4-spot282-20261018`). Python wheels build again with Bison 3.8.2 on manylinux and
no `std::print`, bringing 2.6 to PyPI.

## 2.6.0 (2026-10-18)

- The first `docker_default` preset adds parity and obligation arms. At release, parity
  used Spot on tiers 1-3 and strpm-simd with Spot fallback on tier 4. Four shipping
  slots retain their order: default, no-parity fallback, lean and synthesis.

- OxiDD
  always builds; fixes bounded stacks (#258), native preparation/packaging (#259) and
  cumulative resume summaries (#262). `ACACIA_PHASE_RECORDS` adds per-arm CPU, memory
  and phases.

- Retires research/benchmark tools; source and notes are in
  `refactor-closeout20261017`, along with earlier release notes. Original evidence
  includes `benchmarking-root-legacy`, `plots-three-way-full-20260905` and
  `plots-spot-otf-threeway-20260909`.

[Results](benchmarking/RESULTS.md) records measurements and limitations;
[decisions](benchmarking/README.md#decisions) records policy. Earlier wording and performance
tables from this changelog are archived in docs-20261122.
