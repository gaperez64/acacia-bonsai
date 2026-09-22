# Evaluated, not admitted: S (structural real-backend selector)

S — the same tree/dependencies/options as B, with the existing structural real-backend selector
enabled at its current threshold (PR #189) — was the mandatory broad candidate for this sprint. It
was fully re-evaluated (plan section 1.2's "research continuation" bar) and then confirmation-tested
(plan section 5) at both caps. **Disposition: `research_value` = confirmed; `production_admission`
= REJECT at both caps.** These are separate, correct labels, not a contradiction — see plan section
1.2's own three-way distinction and `../README.md` for the full account.

## Why research_value is real

Broad, full-corpus, two-epoch discovery evidence (`../../opening/{17s,120s}/epoch-{1,2}/three-way-par2.md`):
S beats B on both PAR-2 and coverage, at both caps, consistently across both epochs (17s: −0.95%
PAR-2, net +4/+5 solved; 120s: −1.93% PAR-2, net +6/+7 solved). Gains are broad, not concentrated in
one family (`../gains-losses.tsv`, 369 rows across 4 legs, zero verdict conflicts anywhere).

## Why production_admission is REJECT

Five fresh confirmation pairs at each cap (`../confirmations.tsv`,
`../../opening/confirmations/{17s,120s}/admission.md`) found real, validated losses that discovery
alone did not surface clearly: a systematic TIMEOUT-to-MEMOUT regression cluster at 120s (multiple
families, not one disputed instance), and two unequivocal regressions at 17s. PAR-2 scoring alone
cannot see this — `benchlib.par2` scores TIMEOUT and MEMOUT identically, so the aggregate "S wins"
headline above is real but does not by itself rule out this cluster. Coverage preservation is a set
condition (plan section 12.1), not a net count, and it is not met.

## What this means going forward

The confirmed regression cluster is a genuine, useful research finding on its own — *why* S's backend
choice produces a memory blowup on this specific instance cluster instead of degrading gracefully to
a timeout the way B does is the natural next diagnostic step (plan section 5.1 item 4/5), not
undertaken this session. Until that mechanism is understood and repaired or routed around, S remains
a validated research candidate, not a production one.
