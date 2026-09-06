# Sprint-closing comparison, full SYNTCOMP26 selection (2026-09-05)

The full 1,524-instance selection, **not** the 180-instance stratified panel used
by `three-way-20260829`. That panel understated a full-corpus effect by 4x earlier
in this sprint (+14 predicted against +62 measured), so a closing claim must not
rest on it. Protocol follows the full-selection precedent in
`_bm-logs.tlsf-native-20260814`: one 17 s cap per solver, sequential systemd
scopes, 8 GiB, no swap, 120 s cooldowns.

| series | solved / 1524 | PAR-2 (s) |
|---|---:|---:|
| ltlsynt 2.15.1.dev | **1257** | **9515.826** |
| acacia VB(sprint + default) | 1057 | 16667.551 |
| sprint four-arm portfolio | 1057 | 16687.479 |
| current shipping default | 976 | 19160.668 |
| Acacia 1.x (`5ffd8f99`) | 811 | 24785.471 |

## Routes

- **acacia arms** use the native TLSF frontend (`-T`), all 1,524.
- **ltlsynt** takes SyFCo's unadapted pairs plus `--semantics`, matching the
  reference protocol.
- **Acacia 1.x** takes SyFCo pairs with the semantics overwritten to the declared
  target, since v1 predates the TLSF frontend and has no `--semantics` flag. The
  adaptation was re-derived and verified to reproduce
  `_bm-logs.three-way-20260828/syfco-adapted` byte-for-byte.

## The seven SyFCo failures are real, and were checked

`finding_nemo_pb_1..7` cannot be converted by SyFCo's `ltlxba` printer, so both
SyFCo-routed arms are charged `SYFCO-FAIL` on them. ltlsynt's own native
`--tlsf` route was tested as a fairness check and fails identically on all seven
("Conversion Error: unsupported operator"), because it calls SyFCo internally.
Acacia's native frontend solves two of them (`pb_1` in 0.235 s, `pb_2` in
15.427 s); the 2025 precedent recorded Acacia timing out on all seven.

## Reading it honestly

The sprint moves Acacia from 976 to 1057, **+81**, and past Acacia 1.x by 246.
It does **not** close the gap to ltlsynt, which leads by 200 instances and by a
factor of 1.75 on PAR-2. That gap is the same shape the August panel showed
(165/180 against 137/180) and is the standing result, not a regression.

One finding matters for what ships: the virtual best of the four-arm portfolio and
the current default is **exactly the four-arm portfolio's own 1057**. The shipping
default contributes nothing the portfolio does not already answer.
