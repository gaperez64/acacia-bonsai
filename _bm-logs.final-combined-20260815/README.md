# Final combined landing evidence (2026-08-15--16)

This directory is the final performance/correctness gate set for the native
TLSF, indexed-family-hint, and gate-repair branch.  The candidate and baseline use
the same GCC/release/LTO configuration.

- `build.status`: `COMPLETE PASS`; `build.log`, `candidate.sha256`, revision
  files, and `meson-options.txt` record the build.
- `correctness/`: G0 passes 18/18 Acacia unit tests and 14/14 Posets tests; G4
  has 566 correct answers, 58 timeouts, `Fail: 0`, and no false-verdict marker.
- `g1/`: all 40 frozen verdicts are preserved.
- `g3/`: all landing reports pass.  Candidate results are 90/94 and 231.630
  PAR-2 seconds on `syntcomp21/crit`, 100/174 and 2818.051 seconds on
  `syntcomp24/panel`, and 106/180 and 2780.392 seconds on
  `syntcomp25/panel`.
- `g5-enum*`: historical final-binary interoperability with SyFCo on all nine
  enum-bearing common inputs.  This is not a semantic oracle: the later TLSF
  specification correction intentionally supersedes SyFCo behavior for Strict
  and target-mismatch semantics.

Every solver campaign ran sequentially with an 8 GiB memory limit and swap
disabled.  The recovered Step-1 candidate is intentionally absent; its
separate G3 evidence records a genuine per-instance regression.
