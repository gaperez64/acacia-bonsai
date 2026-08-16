# Recovered Step-1-stack G3 evidence (2026-08-15)

This is the final same-toolchain landing decision for the Step-1 buffer-reuse
candidate that the repaired G2s proxy allowed through to G3.  The candidate is
the preserved Acacia `a542a195` source plus picker/CPre buffer reuse and Posets
`46054a2`, built with the shipping GCC/release/LTO options.

- `build.status` is `COMPLETE PASS`; `build.log`, `candidate.sha256`, source
  revisions, and `meson-options.txt` record exact provenance.
- `landing-syntcomp21.txt` and `landing-syntcomp24.txt` pass.
- `landing-syntcomp25.txt` is the authoritative rejection: aggregate PAR-2
  improves from 2811.292 to 2783.750 seconds at unchanged 105/180 coverage,
  but `load_balancer_unreal2_pb_5_pe_.ltl` moves from the 11.559-second
  UNREALIZABLE baseline to a 17.038-second timeout.
- `status.txt` is therefore `COMPLETE FAIL exit=1`.  This is a genuine
  per-instance regression below the 80%-of-cap remeasurement threshold, so no
  Step-1 code or Posets gitlink change is integrated.

The three `candidate-*.csv` files and `service.log` are the complete campaign
output.  A combined `summary.txt` is absent because the script stops at the
failing 2025 landing report.
