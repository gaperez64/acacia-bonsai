# Closing three-way, SYNTCOMP26 full selection (2026-09-09)

The full 1,524-instance selection, not the 180-case panel. Four series plus a
virtual best, at a 17-second cap with 8 GiB and zero swap.

| series | solved / 1524 | PAR-2 (s) |
|---|---:|---:|
| ltlsynt 2.15.1.dev | **1257** | **9515.826** |
| acacia VB(otf+default) | 1173 | 12684.211 |
| spot OTF (new default) | 1171 | 12736.344 |
| current default (departing) | 1123 | 14254.294 |
| Acacia 1.x | 811 | 24785.471 |

## Reading it honestly

The sprint moves Acacia's shipping default from 1,123 to 1,171, **+48**, and
improves PAR-2 by 10.6 %. Every one of those answers is UNREALIZABLE; REAL
coverage is identical at 533. The gap to ltlsynt narrows from 134 instances to
86, and Acacia still trails it on both coverage and PAR-2.

The virtual best of the new and old defaults is 1,173, only two above the new
default alone. The old default contributes almost nothing the new one does not
already answer, which is the measurement behind dropping it from the shipping
menu.

`syntcomp26-full-par2.md` is the generated table; `syntcomp26-full.{png,pdf}`
the cactus plot. `PROVENANCE.txt` records versions, routes, protocol, what was
reused and why, and the two standing caveats.
