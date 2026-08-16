# Step C TLSF parity evidence (2026-08-14--15)

This directory contains the native-TLSF compatibility gates.  Only the files
listed below are authoritative landing evidence; the other `debug-*`,
`focused-r3` through `focused-r5`, `formula-io-50-final*`, and earlier
50-instance runs are diagnostic attempts superseded by the final repairs.

Authoritative completed evidence:

- `formula-io-50-r4.status`, `.tsv`, and `.log`: canonical formula AST keys
  and full input/output lists match the SyFCo pair for all 50 deterministic
  sampled instances.  Literal formula serialization matches 0/50 and is
  reported separately because the independent printers use different
  parenthesization and derived-operator spelling.  `r3` is the diagnostic run
  that exposed the repaired enum-validity and enum-signal-order mismatch;
- `focused-r6.status` and `.tsv`: final focused regression set passed after
  the ordering and semantics repairs;
- `compat-build.status`/`.log` and `order-build.status`/`.log`: prerequisite
  compatibility and ordering builds passed;
- `indexed-conjunction-instances.txt` and
  `indexed-conjunction-summary.txt`: the sorted 730-instance source-syntax
  cohort and its SHA-256 digest.

The full G5 evidence is `g5-native-vs-syfco.csv`, `g5-status.txt`, and
`g5-summary.txt`.  The completed 1,579-instance run has zero opposite verdicts
and zero frontend/process errors: 1,574 matches, two native-only answers, two
converted-only answers, and one nonsolved resource-limit/timeout difference.
Three native and two converted runs hit the explicit 8 GiB limit.  The
enum-only invalidation closure made after the final compatibility repair is in
`_bm-logs.final-combined-20260815/g5-enum*` and passes all nine affected
instances with no mismatch or resource limit.
