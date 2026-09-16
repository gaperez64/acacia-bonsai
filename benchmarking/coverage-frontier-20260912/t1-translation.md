# T1: where translation time goes, and what cannot move it

Sprint package T1. The handoff asks for a bounded demand-generated automaton
prototype, on the strength of S0's finding that most of the frontier never
reaches the game.

**T1 closes as an attribution result, not a provider.** Two of its three possible
routes were already closed by this repository's own measured record; the third —
that the cost is a configuration artifact — is closed by the measurements here.
A registry defect was found on the way. Recommendation: **KEEP RESEARCH TOOLING**
for the evidence, **STOP** on the prototype.

## What S0 established

On the frozen 46-instance cohort, 31 of 46 instances never reach the game: the
sparse guarded worker stops in translation or preprocessing. The sharpest single
observation was `robot-to-target-charging10`, where a worker spent 15.3 s of a
17 s budget to produce a **115-state** automaton.

## Route 1, the lazy provider: closed by the prior record

`ltl_to_taa` is the only public Spot lazy formula provider. It was audited,
implemented end to end, measured on the full corpus and **NOT ADMITTED** — it
failed G3 and G4, its own factory is eager enough to be killed by SIGALRM at one
second on `F p0 & ... & F p19` *before any successor request*, and both positive
examples eventually requested 100% of the wrapper's rows. The remaining named
route, an FM-explorer acceptance-freezing wrapper, is recorded there as *not
feasibility-established*, and writing a tableau is on that document's
do-not-implement list. See `OTF-AND-SPOT.md`.

## Route 2, the AST hand-off: measured, and worth nothing here

The frontend serializes `preprocessed_ltl` to a string and every forked child
re-parses it. `tlsf-tools` exposes a public AST and an existing, tested
`spot::formula` builder, so the round trip could be removed. Measured on the
worst instance in the cohort, a 53 KB TLSF file producing a 50 KB LTL string:
full parse, expand and emit is about 5 ms, and adding the Spot parse takes it to
about 9 ms. Against a `translation_ms` of 15,313 on that same instance that is
0.03%. **The AST route buys structural access, not time.**

## Route 3, configuration: measured here, and inert

### The translator preference does nothing

`translation_pref` is a per-arm field, so `Small` and `Any` can be compared from
the command line with no rebuild. One real arm, `real:<pref>:backward`, on all 46
cohort instances, 17 s cap, alternating which preference ran first:

| measure | small | any |
|---|---:|---:|
| translation completed | 24 / 46 | 24 / 46 |
| instances whose outcome differs | 0 | 0 |
| automaton identical to the other preference | 24 / 24 | 24 / 24 |

Median delta (any − small) over the 24 comparable instances: **+0 ms**; any is
faster on 11, slower on 9, identical on 26.

Not one instance changes, the automata are the same size everywhere, and the
time difference is noise in both directions. The likely reason is visible in
`translator_options.hh`: the build already
passes `simul=0, ba-simul=0, det-simul=0`, so the simulation-based reduction that
distinguishes `Small` from `Any` is disabled before the preference is consulted.

### The failure is intrinsic, not contention

S0's numbers came from four-worker runs, which left open whether translation
failed because four workers shared one budget. It did not. With **a single arm,
alone, holding the entire 17 s budget and 8 GiB**, 22 of 46 instances still fail
to translate. Among the 24 that succeed the cost is very uneven: median 276 ms,
maximum 7,316 ms, and automaton size does not predict it — `Alarm_a5f99bc6`
takes 4,636 ms for 1,245 states while `06` takes 4,976 ms for 13,609.

### Skipping the state-based lowering does not help either

`create_automaton` requests `BA`, which Spot documents as implying `SBAcc`, and
that lowering duplicates states to move acceptance onto them. The registry
already has `acacia_transition_acceptance`, which asks for `Buchi` without the
state-based requirement. On the one instance where the comparison completed it
produced a **smaller** automaton, 11,977 states against 13,609, and was **8%
slower** to translate, 5,373 ms against 4,976. Smaller automaton, no time saved.

## A registry defect found on the way

`acacia_transition_acceptance = true` combined with the shipping
`boolean_states = forward_saturation` passes `acacia-config.py validate` and then
throws at run time:

```
Exception caught: state_is_accepting() should only be called on automata with
state-based acceptance
ERROR   (exit 3)
```

The option's own comment names `boolean_states::transition_core` as its intended
pairing, but nothing enforces it: the registry's only cross-rule today is the one
relating `actioner` to `ios_precomputer`. A configuration that validates must not
abort the solver. This is fixed separately in this branch.

## Conclusion

The translation cost is inside Spot's translator, it is not reachable by the
preference, not caused by worker contention, not removed by skipping the
state-based lowering, and not attributable to the string hand-off. The two
architectural routes that could address it directly are closed by prior measured
evidence and by an explicit do-not-implement boundary.

**STOP on the prototype. KEEP the evidence.** A future sprint that wants this
should start from the one question these measurements leave open: what inside
`spot::translator::run` accounts for 4-7 s on automata of a few thousand states,
which needs attribution inside Spot rather than around it.
