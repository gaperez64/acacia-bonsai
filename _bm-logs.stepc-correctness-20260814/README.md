# Step C G0 and G4 evidence (2026-08-14)

This directory records the serial correctness gates for the native TLSF and
indexed-family-hint candidate.  The candidate was built and tested with one
job at a time under the campaign's 8 GiB/no-swap outer cgroup policy.

Authoritative terminal files:

- `status.txt`: `COMPLETE PASS` for the combined gate;
- `summary.txt`: compact G0/G4 outcome;
- `g0-acacia.txt`: all 18 Acacia unit tests passed;
- `g0-posets.txt`: all 14 Posets tests passed;
- `g4.txt`: 624 labelled cases, 567 answers, 57 performance timeouts,
  `Fail: 0`, and no false-verdict marker;
- `full-build.status` and `full-build.log`: final candidate rebuild passed;
- `posets-build-gcc.status` and `posets-build-gcc.log`: the authoritative
  GCC Posets build passed.

`posets-build.status` and `posets-build.log` are an excluded setup attempt:
Meson selected Clang while the project supplied GCC-only diagnostic flags, so
compilation stopped before testing.  The GCC build above supersedes it and is
the one used by G0.
