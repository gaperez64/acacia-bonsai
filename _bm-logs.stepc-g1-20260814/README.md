# Step C G1 evidence (2026-08-14)

The native TLSF and indexed-family-hint candidate preserved all 40 frozen
verdicts in the amended regression gate.  Candidate PAR-2 was 95.229 seconds
against 102.226 seconds for the baseline.

- `status.txt`: authoritative terminal `COMPLETE PASS` sentinel;
- `summary.txt`: compact counts and PAR-2 comparison;
- `g1.txt`: complete gate output, including the frozen-row validation.

Solver invocations were sequential and used the gate's 8 GiB/no-swap cgroup
policy.
