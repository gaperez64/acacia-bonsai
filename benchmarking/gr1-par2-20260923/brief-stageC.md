# Brief Stage C — generic online parametric lifting (no family knowledge whatsoever)

> Historical research note: the Python wrapper and lifting package below are retained
> only as differential oracles. Current solver runs use native `acacia-bonsai` arms.

Read decisions.md (every entry from "Owner stop" on), generic-design.md §3-§6, the owner rules:
**no hardcoding ever** (no family lists/names/templates/per-family data or switches; outcomes
invariant under random basenames and alpha-renamed signals), **a TLSF input without PARAMETERS never
enters lifting**, competition input is TLSF. Code: benchmarking/gr1-par2-20260923/oracle/acacia_lift/ (Stage A: direct.py, runner,
wrapper), the historical algorithm in benchmarking/param-lift-20260922/legacy_lift/generalizer.py
(REUSE its generic algebra — projection, per-predicate exact reconstruction, rank/invariant schema,
Skolemization/region export — but NONE of its family tables, name recognisers, collector or named
arbiter branches), and tlsf-tools frontend provenance (tlsf2tlsf --provenance-out; format in
tlsf-tools docs/frontend-provenance.md; gr1_monitor_game.py source_origin_metadata with
provenance_source "frontend"). NEVER stage or commit by any path. One job; scratch
build_scratch/stageC/; never /tmp.
Build benchmarking/gr1-par2-20260923/oracle/acacia_lift/lifting/ (new modules) implementing, for a parametric TLSF input whose
target lowers exactly or strictly to GR(1):
1. Seeds from the input itself: instantiate the SAME bytes at smaller values of one index-bearing
   parameter at a time (others at target values), via tlsf2tlsf overrides; never the target size.
   Seed window: two consecutive valid sizes whose frontend provenance agrees structurally (same
   declaration ids / formula-node ids / conjunct-origin multiset shape per index, same monitor ABI
   and role signatures) plus an optional confirmation size; a detected regime change moves the
   window up; bounded by global knobs (at most 6 sizes per axis). Require provenance_source
   "frontend" and not ambiguous; otherwise decline.
2. Roles and indices from provenance only: index tuples and declaration ids, never names/suffixes;
   special positions by structural incidence, not index==0; encoded-width buses as representation
   bits of one element.
3. Schema: per invariant and rank predicate try arity k=0..K (global K=4) with at
   least one seed larger than k, exact reconstruction on every seed and equal normalized templates
   across seeds; bus-wide cardinality/temporal schemas derived semantically (symbolic equivalence /
   count signatures), never from signal names; decline if unproven.
   Owner decision of 2026-09-24: derive moves from these lifted predicates and
   the exact target game's transition relation, then Skolemize the policy.
   Learned move schemas are optional behind one global knob (off by default);
   an enabled move attempt must pass exact reconstruction and instantiation or
   decline. Record `move_source` in evidence.
4. Instantiate at the target and prove it: policy route first; if Skolemization/policy check fails
   on capacity/deadline and time remains, region check (tlsfcertcheck --method region) of the same
   certificate — one global order, no per-input switch. Only an independently VERIFIED target
   certificate for the exact game of THIS input (hash-bound) yields REALIZABLE; lifting never
   yields UNREALIZABLE (UNREAL stays with Stage A's direct exact route).
5. Integration with Stage A: inside the wrapper's lift slice, for a parametric input run a global
   order (e.g. direct exact solve first for a bounded share, then lifting; or lifting first — choose
   ONE global rule now, document it, and measure later on the development corpus); a non-parametric
   input only gets the direct route. Global knobs (max sizes per axis, K, discovery share of the
   slice, direct/lift order) are declared in one place, documented, and not per-family.
6. Evidence: route "attempted-declined" until independent verification, then
   "lifted-certified" or "direct-certified"; seeds used (override vectors),
   arities chosen, method, all stage timings, and the provenance format version.
Tests: unseen generated parametric specs (write a small generator of parametric GR(1) TLSF families
with random names — cardinality/mutex/request-grant style — that are NOT copies of any corpus
family) where lifting succeeds and is verified; a degenerate smallest size; a pairwise conjunct
hidden at size 2; a k=n predicate (must decline, not answer); alpha-renamed and basename-randomized
twins give identical decisions; mutation of the lifted certificate is rejected by the checker; the
anti-hardcoding guard still passes (extend its forbidden list with anything new you notice).
Informal numbers (no timed campaign): run the route on the first 20 parametric corpus inputs of the
old 72 and 20 others, one at a time with timeout 60, and report outcomes and stage timings.
VERDICT at end.
