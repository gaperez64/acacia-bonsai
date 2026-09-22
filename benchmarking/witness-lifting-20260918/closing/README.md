# Closing report: broad re-evaluation and verified witness lifting

Sprint handoff: `581dfb32-acacia_broad_campaign_verified_witness_lifting_sprint_2026-09-18.md`.
`../decisions.md` is the full running log with every measurement, finding, and judgment call, in the
order they happened; this file is the final synthesis against the handoff's own two deliverables and
its "Definition of done" checklist. Read `../decisions.md` for anything this report summarizes.

## The two deliverables

**1. Measure the general usefulness of existing unshipped work (S).** Done. Full-corpus, two-epoch
discovery at both 17s and 120s caps, followed by five-fresh-pair confirmation at each cap.
**Disposition: `research_value` = confirmed (real, broad benefit); `production_admission` = REJECT
at both caps** (a systematic TIMEOUT→MEMOUT regression cluster at 120s and two unequivocal
regressions at 17s, found by confirmation, not visible in the discovery-epoch PAR-2 headline). The
admitted candidate is therefore the baseline, B, unchanged — see `evaluated-not-admitted/README.md`
for the full account and why "research value" and "shipping" are correctly different answers here,
not a contradiction.

**2. Implement a small verified witness-lifting path.** Done, further than the plan's own stated bar
requires. Three pilot families were selected and carried through W3→W7: `arbiter` (indexed/replicated
REAL) reaches exact target verification (n=10, `VERIFIED`, the plan's explicit success bar);
`round_robin_arbiter` (bounded-counter REAL) and `round_robin_arbiter_unreal2` (small-support UNREAL
obstruction) each reach a concrete failed-stage-and-reproducer (the same exponential ASSUME-antecedent
blowup, precisely isolated and measured) rather than a fabricated success. Along the way, this sprint
also landed a genuine capability Acacia did not have before: `-u formula` combined with `-s` now
exports an actual, mechanically-proven-sound environment witness on UNREALIZABLE — not part of the
plan's committed scope, but directly in service of the second deliverable and independently verified
before being trusted.

## Work package status

| Package | Status | Where |
|---|---|---|
| W0 freeze | complete | `../manifest.json`, `../reuse.tsv` |
| W1 broad campaign | complete, both caps, both epochs | `../opening/{17s,120s}/epoch-{1,2}/` |
| W2 attribution/confirmation | complete, REJECT at both caps | `confirmations.tsv`, `evaluated-not-admitted/README.md` |
| W3 family provenance | complete, 3 pilot families | `../families/selected.json`, `../families/target-checks.tsv` |
| W4 exact target verification | complete | arbiter VERIFIED at n=10; two families reach a documented obstacle instead |
| UNREAL adapter (not in the original W-numbering; in service of W3/W4) | landed, mechanically verified sound | `../decisions.md`'s UNREAL-adapter section; commits `0bc262d6`..`830913e2` |
| W5 bounded automatic proposer | complete | `../families/proposals/propose_schema.py` |
| W6 candidate-restricted product construction | evaluated, deliberately not attempted | `../decisions.md`'s W6 section |
| W7 cold-cost research wrapper | complete | `../acacia-witness-lift.py` |

## Definition of done, checked against what actually happened

- [x] Existing promising work received the full broad/long re-evaluation before new extrapolation
      infrastructure was prioritized — W1 ran to completion (both caps, ~57.5h wall) before W3 began.
- [x] Full 17-second and 120-second observations are separate, balanced, complete and reproducible;
      every known loss remains visible — the 120s MEMOUT cluster and the 17s regressions are reported
      in `evaluated-not-admitted/README.md`, not suppressed by the favorable discovery-epoch headline.
- [x] Research continuation and shipping admission are separate decisions; correctness is never
      relaxed — S's two labels above; the UNREAL adapter's C++ change was independently reviewed and
      mechanically re-verified before being committed, not just built and trusted.
- [x] The selected parametric families have real original templates, legal seed values and
      proved/checked correspondence to the benchmark target — `../families/selected.json`, read from
      the actual templates, not inferred from filenames.
- [x] At least one feasibility pilot reaches exact target verification, or a concrete failed stage
      and reproducer explain why none did; no fabricated success criterion — arbiter n=10 `VERIFIED`
      (both the original manual schema and W5's independently-discovered automatic one); the other two
      families' exponential-blowup obstacle is precisely reproducible, not a vague "it timed out."
- [x] Seed witnesses, not merely small verdicts, inform automatic proposals; third-parameter checking
      is not called a proof — W5's proposer reads the actual Acacia-synthesized seed AAGs (BFS over
      reachable states, a genuine memory-detecting trace search), and n=4 sanity is checked but never
      conflated with the n=10 target result.
- [x] REAL/UNREAL timing, totality and quantifiers are independently validated, including all extra
      opponent outputs in a lifted environment witness — the UNREAL adapter's soundness rests on a
      mechanized full-containment check (`L(delayed circuit) ⊆ L(!phi)`, not mere nonempty
      intersection), cross-validated against an independent by-hand proof on a hand-picked game before
      being trusted on the real family.
- [ ] Restricted construction (W6), if implemented, is complete for its stated region and never
      reuses partial results as global completeness — **not applicable: W6 was not implemented**, a
      deliberate scoping decision (cross-repository, correctness-critical, multi-day-to-multi-week
      scope; see `../decisions.md`'s W6 section), not a silent gap. The two exact reproducers it would
      need are preserved.
- [x] Every reported online gain charges both seed solves, proposal, grounding, target checking,
      wrapper overhead and any fallback; warm reuse is separate — `cold-cost-breakdown.tsv`, real
      measured stage timings from a genuinely cold, fresh invocation (seeds re-synthesized, not read
      from the already-committed `families/seeds/` cache); the optional warm-cache mode was not built.
- [x] Production activation still requires no validated regression and confirmed useful benefit in
      the actual deployed configuration — this is exactly why S was rejected despite its real,
      broad, confirmed research benefit.
- [x] Existing three-way infrastructure generates PAR-2 totals/means and matching PNG/PDF cactus plots
      from saved observations, without a new scoring/plotting pipeline — `export-cactus`,
      `cactus-report.py` used unmodified throughout; `17s/README.md`, `120s/README.md` point at the
      existing plots rather than duplicating multi-hundred-KB artifacts a second time.
- [x] No all-parameter theorem, generic cutoff, or unrestricted fast translator is claimed from a
      successful finite-target experiment — every VERIFIED result in `../families/target-checks.tsv`
      is explicitly scoped to its own parameter; W5/W7's docstrings and evidence sidecars say this
      directly, not just this report.

## What a future session should pick up

1. **W6**, now genuinely motivated (not speculatively): `round_robin_arbiter` n=10 and
   `round_robin_arbiter_unreal2` n=7 both hit the identical exponential ASSUME-antecedent blowup,
   exact reproducers preserved in `../families/target-checks.tsv`. This is real engineering scope, not
   a quick follow-on — see `../decisions.md`'s W6 section for why.
2. **Why S's backend choice produces the confirmed MEMOUT cluster instead of degrading to a timeout**
   — the natural diagnostic next step for the rejected-but-real research candidate (plan section
   5.1 items 4-5), not undertaken this session.
3. **W5's recognizer doesn't generalize to `round_robin_arbiter`'s own synthesized circuit shape**
   even though the same hand-written schema works there (found by W7's honest `UNKNOWN` decline, see
   `cold-cost-breakdown.tsv`) — a concrete, bounded target for extending the recognizer's structural
   predicates, not a rewrite.
4. **`round_robin_arbiter_unreal2`'s real target (n=7) and its whole family's UNREAL schema class**
   are outside W7's current eligibility rule (REAL-controller schemas only) — extending W5/W7 to the
   now-landed UNREAL-adapter capability is a natural next step once W6 unblocks the n=7 target check,
   or independently if a decomposition-based check (matching arbiter's own approach) turns out to
   suffice there too.
