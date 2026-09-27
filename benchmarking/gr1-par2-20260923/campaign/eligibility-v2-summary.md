# Source-binding eligibility census, bounded version

The serial `eligibility-census.py` rerun used the same 1,524 SYNTCOMP26 IDs,
TLSF source map, production source binder and route guard, and pinned
`/home/gperez/GIT-repos/tlsf-tools/build-P5-9212e2b` as the first census.
It read no benchmark verdict or STATUS field. The binding budget was 1.0 s.
At the wrapper's 17 s cap, the default budget is 0.85 s.

| Decision | IDs | Tool calls per ID | Total binding time | Median | P90 | P99 | Max |
|---|---:|---|---:|---:|---:|---:|---:|
| Eligible | 91 | 18 × 43, 24 × 32, 30 × 16 | 1.242 s | 0.0112 s | 0.0185 s | 0.0518 s | 0.1342 s |
| Decline | 1,433 | 1 × 1,070, 5 × 285, 18 × 32, 24 × 7, 30 × 39 | 3.966 s | 0.0009 s | 0.0042 s | 0.0172 s | 0.4055 s |

The 91 eligible IDs are identical to `eligible.list` and the first census.
All 33 source-verified members rejected by the seed-size route guard kept
`target_must_exceed_every_seed`. No input hit `eligibility_budget_exhausted`;
all declines finished below both 1.0 s and the 0.85 s default at a 17 s cap.
The overall decline median remained near its prior 0.0008 s value. Decline
P99 fell from 0.8748 s to 0.0172 s and the maximum from 7.0792 s to 0.4055 s.

Decline reasons are `unsupported_parameter_signature` (1,070) and
`source_not_content_verified_for_capability` (330). The latter includes the
three previous five-second lowering timeouts: `ltl2dba_theta_pb_300_pe_`,
`ltl2dba_theta_pb_500_pe_`, and `shift_pb_500_pe_`. Their signal signatures
now fail before lowering. Every source still has to pass the unchanged basic
TLSF and lowered-LTL equality checks before admission.

Validation: the broad runnable Python suite passed (996 tests, 49 subtests;
7 skipped), followed by 68 focused tests and 23 subtests against the final
code. Those tests cover all 91 eligible and 33 route-guarded members, a slow
lowering process with a child, wrapper budget forwarding, and evidence reasons.
Ruff passes on every changed Python file; a repository-wide run finds 228
existing violations outside those files. Two Python API test modules cannot
be collected in this checkout because `build_python=false` and the
`acacia_boomslang` module is absent.
