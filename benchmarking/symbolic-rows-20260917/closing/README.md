# Closing three-way comparison — symbolic-rows / selective-sparse-real sprint

**Outcome: neither package was admitted, so the closing candidate is the**
**baseline configuration**, exactly as plan §9.6/§12.4 anticipate. This
report **reuses the previous sprint's closing rows byte-for-byte** rather
than measuring the same executable twice, because the reuse is exact under
the plan's own criteria:

- This sprint's pinned production baseline is `7e6e932a8c44a3e263ddde9ebd3c6e60ea1e0aab`.
- The previous sprint's frozen closing baseline binary
  (`candidate-f7919b83`) is documented as "solver source identical to
  `6410fd34`".
- `git diff --stat 6410fd34 7e6e932a -- src/ meson.build meson.options
  config/ subprojects/posets` is **empty** — every commit between them
  (PRs #185–188) touched only benchmarking/reporting tooling and the
  previous sprint's own record, never solver source, build configuration,
  or the posets submodule pin.

So `7e6e932a`'s solver binary is provably source-identical to the already-measured
`candidate-f7919b83`, on the same corpus/regime/harness/host. Copying is not
a shortcut around measurement; it is the same measurement.

## Primary report (required)

[three-way-par2.md](three-way-par2.md), [three-way.png](three-way.png),
[three-way.pdf](three-way.pdf), from
[acacia-baseline.csv](acacia-baseline.csv), [acacia-candidate.csv](acacia-candidate.csv)
(byte-identical to the baseline, SHA-256
`922ac3533a56a4427d66f74e9d27c7fb03e42948cef63376fd11573864623c39` for
both) and [ltlsynt.csv](ltlsynt.csv) (SHA-256
`f81beba80ab5639b01c05d921d000aa0022060adf8255d9b4a4b739f8d0e1cfb`) —
copied unchanged from
[`../../demand-sparse-20260916/closing/`](../../demand-sparse-20260916/closing/).

| Series | Solved / 1,524 | REAL | UNREAL | PAR-2 total (s) | PAR-2 mean (s) | Timeouts | UNKNOWN | Memory failures | Errors |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Acacia baseline | 1,176 | 533 | 643 | 12,624.620 | 8.284 | 345 | 1 | 2 | 0 |
| Acacia candidate (= baseline) | 1,176 | 533 | 643 | 12,624.620 | 8.284 | 345 | 1 | 2 | 0 |
| ltlsynt | 1,258 | 574 | 684 | 9,504.731 | 6.237 | 254 | 2 | 0 | 3 |

Same identities, regime and exact commands as the reused report; see
[the original README](../../demand-sparse-20260916/closing/README.md) for
full detail (host, epoch, cap/memory/CPU-quota settings, TLSF-frontend
boundary caveat). Nothing here is a new full-corpus run.

## Why nothing was admitted this sprint

Full package-level reasoning: [../decisions.md](../decisions.md).

- **A1 (Boolean-guard folding, closure-buchi provider):** a real, tested,
  measured mechanism fix (up to −32% branches on an already-solving P2
  target, up to −25% on a still-failing one; see
  [../boolean-rows.md](../boolean-rows.md)) that does not, by itself,
  newly complete any of the 19 previously-blocked P2 targets' first row at
  frozen default budgets. No useful end-to-end provider solve exists to
  attribute a deployment candidate to (§4.1/§5.3); the provider stays
  research-only, as it already was.
- **B (selective sparse-real dispatch):** B1 confirms a real, hint-free
  positive contrast (5/5 coverage gains) but also the previous sprint's
  exact regression (`workstation_resupply_pb_3_pe_`, validated systematic
  under a 51 s cap). B2's frozen rule (`boolean_states/states <= 30%`) is
  evidence-selected and directly confirmed correct — it picks
  `spot-guarded-sparse` for every gain and `forward` for workstation,
  verified live via capture — but the **compiled, deployed candidate**
  still shows workstation regressing (0/5 vs a same-screen control of 2/5,
  down from B1's own 3/5), even though the selector's own decision for
  that instance is unchanged (`forward`, confirmed). This matches the
  previous sprint's own documented P0 finding on the same instance almost
  exactly: unrelated code linked into the same LTO release image can shift
  a hot loop's alignment and flip a knife-edge ~17 s instance, with
  identical logic (`benchmarking/demand-sparse-20260916/diag-perfdiff/ATTRIBUTION.md`,
  memory `lto_code_placement_noise`). A full disassembly-level attribution
  (same-path rebuild, perf stat, hot-function diff) matching that
  precedent was not performed this sprint; per §10.1 ("an unresolved
  near-cap loss blocks activation; do not call it noise by default") the
  selector is **not admitted** regardless. It stays on this sprint's
  branch as tested, mechanism-correct research code.

Per §12.4/§9.6, this is not a failure to report — a validated, working
decision rule that still doesn't clear a strict, set-based no-regression
bar is exactly the outcome the admission gate exists to catch.
