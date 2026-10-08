# Equivariance pre-pass allowance

`--equivariance-budget` defaults to the absolute allowance `3s` (3,000,000,000 ns).
Select `--equivariance-budget unbounded` explicitly to preserve the incumbent route.
A finite budget applies only to backward decision workers with equivariance enabled;
forward workers, synthesis and equivariance-off retain their ordinary paths.

| Argument | With an outer deadline | Without an outer deadline |
|---|---|---|
| Omitted (default `3s`) | 3 s allowance, also clipped by the outer deadline | 3 s allowance |
| `unbounded` | Incumbent pre-pass | Incumbent pre-pass |
| Fraction `0 <= F < 1` | F times time remaining at first pre-pass entry | F times a fixed 5 s reference |
| Time, e.g. `3s`, `3000ms`, `0.5s` | Absolute allowance, also clipped by the outer deadline | Absolute allowance |
| `0`, `0s`, `0ms` | Immediate STOPPED and ordinary backward fallback | Immediate STOPPED and ordinary backward fallback |

The global no-deadline reference is **5 seconds**, matching the extended weakening
pre-pass's fixed reference. Equivariance is optional work preceding a complete ordinary
backward route in the same worker. A fixed seconds-scale horizon makes fractional caps
finite without assuming a wrapper deadline, and avoids spending the whole invocation
on an optional recognition/orbit/search route. For example, `0.25` allocates 1.25 seconds
without a deadline. This is one global time policy selected from that route structure
and the existing pre-pass policy, with no instance, family or measured-verdict tuning.
It does not set an invocation timeout. The default is one global 3 s optional-work
allowance, independent of input names or families.

The first entry fixes the total allowance. Every decomposed subgame charges only time
inside its equivariance call, including exception cleanup; translation and ordinary
backward solving do not spend it. Exhaustion remains typed `STOPPED`, with no proof or
applicability decline, followed by ordinary backward in the same worker. Cooperative
checkpoints retain the existing limit: an individual noninterruptible operation and
its cleanup may overrun the slice before fallback begins.

The CLI accepts finite nonnegative times with `s` or `ms`; fractional times are allowed
and converted to whole nanoseconds by truncation. Explicit nonnegative zero (for example,
`0`, `0s`, `0ms`, or `+0.0`) requests immediate fallback. Negative spellings, including
negative zero such as `-0`, `-0s`, or `-0ms`, are invalid.

Positive values must represent at least **one nanosecond** before truncation. Absolute
values therefore require at least `0.000000001s` or `0.000001ms`. Fractions are validated
against the fixed 5 s reference in both deadline regimes, so a positive fraction must
be at least `2e-10`; the stored fraction must also remain below 1. This validation is
independent of a supplied deadline. A shorter or exhausted deadline can still leave
less than one nanosecond at pre-pass entry and cause ordinary fallback. Values that
underflow during parsing or conversion are invalid: `1e-400`, `0.0000000001s`, and
`0.0000001ms` cannot silently become explicit zero. Values above the minimum still
truncate, for example `1.9e-9s` becomes one nanosecond.

Invalid units, nonfinite values, negative values, underflow, sub-nanosecond positive
allowances and times outside the nanosecond representation fail with **exit 3** and an
error naming `--equivariance-budget`. Repeating the option replaces the earlier mode.
The runtime default lives in `arg_parse_result`, alongside the existing CLI state.
The configuration registry, `meson.options` and `acacia_build_config.hh.in` describe
compile-time variants; a registry option would add an unnecessary build-time choice
for this runtime allowance. There is no new compile-time option.

Existing `equivariance_budget` records retain fraction, initial remainder/reference,
total, consumed, current allowance and deadlines. A separate compact
`equivariance_limit` record discloses mode (`absolute`, `remaining`, `reference`),
absolute nanoseconds and reference nanoseconds. This avoids expanding the budget
packet on platforms with a 512-byte `PIPE_BUF`. Unbounded diagnostics retain the
existing events.

## Default admission — 2026-10-08

Evidence campaign `cov20261007-eqfull` (to be archived) is a fresh, matched, rotated
17 s screen over the full 1,524-input corpus, without an invocation deadline and
with one executable for both explicit treatments. `3s` solves 1,206 inputs versus
1,205 for `unbounded`: one gain (`collector_v3_pb_9`, 9.76 s), zero losses, zero
verdict conflicts and identical memory outcomes. Paired PAR-2 improves by 23.3 s,
exceeding the 12.1 s noise floor. This admits `3s` as the runtime default; all
explicit fractional, duration and unbounded forms remain selectable.

## Screen reproduction

Build the shipping preset with the normal optimized profile in
`build_scratch/p2b-eq-zero/release`, then freeze the executable and its dynamically linked libraries under
`build_scratch/p2b-eq-zero/frozen`. Use the same updated executable for both treatments; the unbounded treatment is the
incumbent control. Record the source diff and executable digest alongside the screen.
The source label below identifies the uncommitted implementation on `0bc8d9f1` and must
be replaced by the final revision if the driver later commits it.

The paired 17 s full-manifest screen command is:

```bash
env -u ACACIA_OUTER_DEADLINE_MONOTONIC \
  /home/gperez/GIT-repos/acacia-bonsai/.venv/bin/python3 \
  /home/gperez/GIT-repos/acacia-bonsai/_bm-logs.cov-20261006/screen.py \
  /home/gperez/GIT-repos/acacia-bonsai/_bm-logs.cov-20261006/p2b-eq-absolute-screen17 \
  /home/gperez/GIT-repos/acacia-bonsai/tests/suites/benchmarks/syntcomp26/all.list \
  17 \
  '[["EQ-inc","/home/gperez/GIT-repos/acacia-bonsai/build_scratch/cov/wt-p2b-eq/build_scratch/p2b-eq-zero/frozen/src/acacia-bonsai","0bc8d9f17199ddff329ec5d45496537e8c3846d0+eq-absolute-zero-dirty","--equivariance-budget unbounded"],["EQ-3s","/home/gperez/GIT-repos/acacia-bonsai/build_scratch/cov/wt-p2b-eq/build_scratch/p2b-eq-zero/frozen/src/acacia-bonsai","0bc8d9f17199ddff329ec5d45496537e8c3846d0+eq-absolute-zero-dirty","--equivariance-budget 3s"]]'
```

`3s` is the owner's suggested absolute treatment, frozen before measurements. The
screen driver rotates the pair per input, runs serially in 8 GiB no-swap scopes,
collects phase records and rusage, and omits `--route-records` to retain shipping's
no-deadline regime. The measured admission above is supplied by the driver; archival
of `cov20261007-eqfull` remains with the driver.
