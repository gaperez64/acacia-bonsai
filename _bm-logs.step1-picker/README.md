# Critical-picker portfolio experiment (2026-08-12)

This directory records the rejected Step-1 picker-diversification experiment.
The candidate added a second REAL solver child for every selected translation
preference.  The original child retained the shipping `critical` picker's
input order and output-action MRU updates; its sibling scanned inputs in a
fixed reverse order and did not mutate output-action order.  The two existing
UNREAL children were unchanged, so the shipping default grew from three to
four concurrent solver children.  Synthesis remained single-child.

The exact pre-change executable was built from Acacia `a542a195` with Posets
`f169330`.  The candidate was the uncommitted change on Acacia `3e751fd8`, also
with Posets `f169330`.  Before G3, both halves of G0 passed 14/14 and G1
verified all 40 frozen verdicts.  Every G3 invocation ran sequentially in its
own systemd scope with `MemoryMax=8G`, `MemorySwapMax=0`, and a 17-second wall
limit.  The interrupted first campaign emitted no CSV; all six files here
come from the clean from-scratch `acacia-picker-g3-20260812-r2` campaign.

The 2024 panel failed the landing bar: baseline 99/174 and PAR-2 2851.402 s,
candidate 98/174 and PAR-2 2897.506 s.  The 2021 critical panel failed in the
same way: baseline 90/94 and PAR-2 233.260 s, candidate 89/94 and PAR-2
259.398 s.  In both suites the sole lost answer was `collector_v215.ltl`:
the baseline answered REALIZABLE in 15.876/16.042 seconds while the candidate
timed out in 17.082/17.081 seconds.  The 2025 panel passed with 105/180 for
both binaries and nearly neutral PAR-2 (2811.292 vs 2812.548 seconds).

A direct diagnostics/perf run on `amba_decomposed_arbiter6.ltl` confirmed the
order sensitivity but did not find a reverse-order payoff.  The shipping MRU
child solved with 34 automaton states, 85 edges, 29 loops, `max_f=904`, and
1,741,717 meets (1.870 seconds alone; 1.963 seconds in the portfolio).  The
reverse child was still unknown when its sibling won, already at 24 loops,
`max_f=904`, and 1,408,083 meets after 1.472 seconds.  The baseline perf
profile attributed 57.40% to equivariant CPre, including 52.35% to downset
intersection, and 20.57% directly to the critical picker.

There was no OOM marker or campaign crash associated with the landing-bar
loss.  The repeated near-cap regression shows that merely retaining the
baseline child does not structurally retain the baseline wall-time answer:
the additional CPU-bound sibling changes the shared resource envelope.  The
candidate code was therefore reverted and does not land.
