# Step C G3 evidence (2026-08-14)

All three landing panels passed for the native TLSF and indexed-family-hint
candidate under the serial 17-second, 8 GiB/no-swap protocol.

- `status.txt`: authoritative terminal `COMPLETE PASS` sentinel;
- `summary.txt`: compact outcome for all panels;
- `candidate-syntcomp{21,24,25}.csv`: complete candidate measurements;
- `landing-syntcomp{21,24,25}.txt`: comparisons against the preserved
  baseline measurements.

The candidate retained 90/94 answers on `syntcomp21/crit`, improved
`syntcomp24/panel` from 99/174 to 100/174, and improved
`syntcomp25/panel` from 105/180 to 106/180.  Every landing comparison printed
`GATE PASS`.
