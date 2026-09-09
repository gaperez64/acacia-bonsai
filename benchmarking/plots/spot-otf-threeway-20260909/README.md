# Closing three-way, SYNTCOMP26 full selection (2026-09-09)

The full 1,524-instance selection, not the 180-case panel. Four series plus a
virtual best, at a 17-second cap with 8 GiB and zero swap.

| series | solved / 1524 | PAR-2 (s) |
|---|---:|---:|
| ltlsynt 2.15.1.dev | **1257** | **9515.826** |
| acacia VB(arriving+departing) | 1173 | 12684.211 |
| `otf_sparse_formula` (arriving) | 1171 | 12736.344 |
| `best_four_arm_contradiction` (departing) | 1123 | 14254.294 |
| Acacia 1.x | 811 | 24785.471 |

## A note on the series names

There is no "default" Acacia configuration, so the series are named for the
presets they are. `scripts/acacia-bonsai.sh` requires an explicit configuration
name and exits with usage if given none; CI and `scripts/compile.sh` build every
`docker_default` member; nothing treats the group's first entry as special. The
group name means "what the Docker image ships", not "the default". Earlier
comparisons in this tree used a "current default" label; it was never a property
the repository defines.

`acacia VB(...)` is `cactus-report.py --virtual-best`: a synthetic series taking
the better outcome per instance across the two named Acacia presets. It is not a
runnable configuration and no race between those workers was measured.

## Reading it honestly

The shipping menu's slot-1 change moves that slot from 1,123 to 1,171, **+48**,
and improves its PAR-2 by 10.6 %. Every gained answer is UNREALIZABLE; REAL
coverage is identical at 533. The gap to ltlsynt narrows from 134 instances to
86, and Acacia still trails it on both coverage and PAR-2.

The virtual best of the arriving and departing presets is 1,173, only two above
the arriving preset alone. The departing preset answers almost nothing the
arriving one misses, which is the measurement behind replacing it rather than
growing the menu to five.

`syntcomp26-full-par2.md` is the generated table; `syntcomp26-full.{png,pdf}` the
cactus plot. `PROVENANCE.txt` records versions, routes, protocol, what was reused
and why, and the two standing caveats.
