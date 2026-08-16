# Rebuilt Step-1 stack evidence

This directory records the revised-protocol batch measurement of the rebuilt
Step-1 stack on 2026-08-11--12.

- Baseline Acacia source: `a542a195a8ede61f91ec4878df8e54b3c23deec2`
- Baseline Posets source: `f169330e480df3fdb15f15ac38e159bae69ff93f`
- Candidate Acacia source: baseline plus picker/CPre buffer reuse (uncommitted,
  reverted after the failed gate)
- Candidate Posets source: `46054a26c2ca559d1c8ca981b94fb0268e32da65`
- Baseline binary SHA-256:
  `006bb68cc5428c011840764b33c4d6377c527d868f808b490b26d9c3b7794411`
- Candidate binary SHA-256:
  `bd295f4a15fde4637a864484d4d5ec50ed5c2de97d859ccff58bd325aeffb35d`

`solver-profile-samples.tsv` and `solver-profile-summary.tsv` are the clean
G2s rerun. It started only after a zombie/CPU audit and a five-second load
sample showed 96--98% idle CPU and no I/O wait. All 60 solver repetitions ran
sequentially in distinct systemd services with `MemoryMax=8G`,
`MemorySwapMax=0`, and `OOMPolicy=continue`. The earlier paused/resumed run in
`/tmp/acacia-step1-stack-g2s-20260811` is intentionally excluded because its
resumed sample exited 137 and was contaminated by the pause and competing CPU
load.

`posets-samples.tsv` and `posets-medians.tsv` contain the five-repeat advisory
G2 comparison of the same Posets stack against `f169330`.

The amended G2s rule was applied to the preserved medians on 2026-08-14; see
`g2s-rescore-20260814.txt`.  No target exceeds the 6% regression ceiling, the
geometric-mean cycle ratio is 1.02776 (2.78%), and
`syntcomp25/g-unreal-116.ltl` improves 24.68%.  The stack therefore passes the
proxy under case (iii) and may proceed to G3, but the proxy result alone is not
an adoption decision.  The candidate source and binary no longer exist, so a
G3 rerun requires reconstructing the preserved stack rather than reusing a
binary artifact.
