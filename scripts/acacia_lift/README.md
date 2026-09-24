# Source-bound GR(1) portfolio route

`python -m acacia_lift.runner --request-mode source` hashes and snapshots the
actual TLSF bytes. It first lowers that snapshot in exact mode and, if the
exact reduction declines, tries a strict reduction for REAL-only lifting. For a concrete
parametric input with unambiguous frontend provenance, it tries online lifting
inside the wrapper's lift slice. It then gives remaining time to the generic
exact direct route. An input without `PARAMETERS` goes directly to the exact
route. Either route returns a verdict only after an independent checker accepts
the source-bound target game and certificate; lifted candidates can return only
REALIZABLE. The wrapper owns the outer budget and Acacia fallback.

The one global rule is `lift-then-direct`. Cost knobs are declared in
`lifting/settings.py`: at most six smaller sizes per index-bearing parameter,
arity at most four, discovery limited to 20% of the lift slice, a 2,000-subset
projection/instantiation cap, a 50 ms move-schema attempt per goal, 75% of
proof time for policy export/check before region fallback, and fixed
solver/checker node caps. Seeds vary one
axis at a time with all other parameters at their target values; the target
size is never solved as a seed. Two consecutive sizes must agree structurally;
a third is used for confirmation when available. The route declines lifting
when source origins, roles, bus semantics, rank depths, or exact predicate
reconstructions disagree.

The default proof order is policy construction plus certificate checking, then
region checking of the same certificate only on policy capacity or deadline
failure. Evidence records source, game and artifact hashes, override vectors,
per-predicate arities, provenance version, all completed stage timings, and the
checker method. The checker is the sole decision authority for lifted targets.

Historical experiments remain outside the production runner.
