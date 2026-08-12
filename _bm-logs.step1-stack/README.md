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
