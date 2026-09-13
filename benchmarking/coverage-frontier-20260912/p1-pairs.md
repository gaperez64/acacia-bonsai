# P1: what two-arm pairs actually cost, measured

Sprint package P1, first half. Six cross-polarity pairs, each measured as a real
two-arm invocation rather than inferred from isolated per-arm minima.

The result reframes the problem: **as fixed choices the six pairs are
indistinguishable, but one binary decision inside them is worth about nine
instances, and a two-pair repertoire captures essentially all of it.**

## Run identity

| | |
|---|---|
| Instances | `tests/suites/benchmarks/syntcomp26/panel.list`, the 180-instance admission panel, family-diverse by construction |
| Pairs | the six of `{real:small:backward, real:small:forward, real:small:spot-guarded-sparse}` x `{unreal:formula:spot-guarded-sparse, unreal:automaton:forward}` |
| Runs | 1080 = 180 x 6, each one invocation with both arms inside one scope holding the whole budget |
| Order | interleaved, all six pairs per instance back to back, with the leading pair rotated |
| Regime | 17 s cap, `MemoryMax=8G`, `MemorySwapMax=0`, one invocation at a time |
| Binary | `4e6bbee3e9ef2165...`, preset `otf_sparse_formula` |
| Raw data | `_bm-logs.20260913-p1-pairs/`, gitignored; the summary and metadata are committed beside this file |

No pair disagreed with another about a verdict on any instance.

## As fixed choices, the pairs are the same

| pair | solved / 180 | PAR-2 |
|---|---:|---:|
| `rf-ugf` | 132 | 9.71 |
| `rf-ufa` | 131 | 9.78 |
| `rg-ugf` | 131 | 9.84 |
| `rb-ufa` | 130 | 9.86 |
| `rb-ugf` | 130 | 9.88 |
| `rg-ufa` | 130 | 9.91 |

The whole spread is two instances and 0.2 s of PAR-2. The repetition study in
this sprint measured the same binary varying by about a second on near-cap
instances, and the committed PAR-2 noise floor for SYNTCOMP26 is 12.1 s. Nothing
in this table is separable. Choosing a fixed pair on these numbers would be
choosing noise.

## Per instance they differ, and the difference has one cause

The oracle over all six pairs is **143** against a best fixed pair of **132**:
eleven instances of headroom, far outside the noise band above.

Eight of those eleven are solved by exactly the three pairs containing
`unreal:automaton:forward`, whichever real arm they carry. Two more are solved by
exactly the two pairs carrying `real:small:backward`, and one by the two
carrying `real:small:spot-guarded-sparse`. The headroom is therefore not
scattered; it is mostly one binary choice.

Holding the real arm fixed and choosing only the unreal arm per instance:

| real arm | formula-unreal | automaton-unreal | perfect chooser | gain |
|---|---:|---:|---:|---:|
| `backward` | 130 | 130 | 139 | +9 |
| `forward` | 132 | 131 | 140 | +8 |
| `spot-guarded-sparse` | 131 | 130 | 139 | +8 |

Across the whole panel, 9 instances are solved only by the formula-unreal
family, 8 only by the automaton-unreal family, and 126 by both. So 17 of 180
instances, 9.4%, turn on that single decision and the rest do not care.

## Two pairs are enough

| repertoire size | best subset | oracle |
|---:|---|---:|
| 1 | `rf-ugf` | 132 |
| 2 | `rb-ufa`, `rf-ugf` | 142 |
| 3 | `rb-ufa`, `rf-ufa`, `rg-ugf` | 143 |
| 6 | all | 143 |

Two pairs reach 142 of the 143 available. The remaining four pairs are worth one
instance between them. Any selector built from this should choose between two
options, not six.

## What this does and does not establish

It establishes that the headroom exists, is concentrated, and has a single
dominant cause. It does not establish that the winner is predictable. Those are
different claims, and only the first is measured here. The instances the
automaton-unreal arm unlocks are conspicuously named for unrealizability, but
filenames, status comments and family labels are excluded from model inputs by
the sprint's own rules, so that observation is a hint about specification shape
rather than a usable feature.

For comparison, the shipped four-arm race scored 140 of 180 on this panel in the
2026-09-10 evidence. A perfect unreal-arm chooser running two workers reaches the
same 140. That comparison crosses revisions and binaries, so it is indicative
rather than measured, but it is the reason the selector is worth testing: the
prize is not more coverage than the race, it is the same coverage at half the
worker count.

## Recommendation

Build the selector as a **binary decision on the unreal arm**, with a two-pair
repertoire, and evaluate it against the best fixed pair on held-out families. If
cheap source features cannot predict that one choice, the honest outcome is to
report the oracle gap and stop, because a fixed pair is then the right answer and
the six-pair repertoire is certainly not.
