# Native TLSF 2025 campaign (2026-08-14)

This directory archives the two CSVs produced by the completed
`acacia-native-tlsf-overnight-20260814-r2.service` campaign.  The campaign ran
from 2026-08-14 07:25:21 CEST to 11:56:09 CEST and exited successfully.

Provenance:

- Acacia commit: `23d4089c`
- tlsf-tools commit: `0632198`
- corpus: `selection-ltl-2025v2/selection-ltl-2025` (1,586 TLSF files)
- per-solver timeout: 17 seconds
- execution: sequential solver scopes, 8 GiB memory, no swap
- native binary/build: `/tmp/acacia-native-tlsf-clang-safe-20260813`

`finding-nemo.csv` covers the seven specifications newly accepted by the
native frontend.  Acacia timed out on all seven; ltlsynt solved five.

`syntcomp25-native-vs-ltlsynt.csv` contains all 1,586 instances.  Acacia
solved 1,043, ltlsynt solved 1,257, both solved 1,003, Acacia alone solved 40,
ltlsynt alone solved 254, and neither solved 289.  There were zero verdict
disagreements among instances answered by both solvers.  ltlsynt returned an
error on three instances (`helipad-contradict0`, `helipad-contradict1`, and
`package-delivery-real`); all three timed out in Acacia.
