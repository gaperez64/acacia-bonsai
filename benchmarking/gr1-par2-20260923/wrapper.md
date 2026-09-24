# Checked-lifting portfolio wrapper

`scripts/acacia-lift-portfolio.py` is an Acacia-compatible sequential wrapper. It gives the
source-bound checked GR(1) route a bounded first attempt and, unless that attempt produces evidence
with `target_verified: true` and a decisive verdict, replaces itself with the unchanged fallback
Acacia process. Both stages therefore remain in the caller's one cgroup and wall-clock budget.

The `--` separator is mandatory:

```sh
scripts/acacia-lift-portfolio.py \
  --lift-budget-fraction 0.3333333333333333 \
  --lift-entry benchmarking/param-lift-20260922/param-lift-campaign.py \
  --tlsf-tools-build subprojects/tlsf-tools/build-P14-dbe8c2b \
  --bindings-python /usr/bin/python3.13 \
  --bindings-site /usr/local/lib64/python3.13/site-packages \
  --buddy-adapter /absolute/path/to/buddy_veccompose.so \
  --real-check policy \
  --cap 120 \
  -- build_w1_B/src/acacia-bonsai -T instance.tlsf
```

Everything after `--`, including option order and the TLSF argument, is B's argv. On fallback the
wrapper calls `os.execv` with that list unchanged. If B names `-T /dev/stdin` (or `-T -`), the
wrapper reads stdin once into a private file, gives that file to lifting, then unlinks it after
restoring it onto file descriptor 0 for B.

Use either `--lift-budget-fraction F` (default `1/3`) or `--lift-budget-seconds S`. The remainder of
the outer cap is reserved for B. `ACACIA_OUTER_DEADLINE_MONOTONIC` should contain an absolute
`CLOCK_MONOTONIC` value supplied by the outer runner. If that variable is absent, `--cap` creates a
deadline relative to wrapper entry; that mode is less exact because launcher and `systemd-run`
startup are outside the wrapper's clock. When an outer deadline is present and `--cap` is omitted,
the wrapper uses the time remaining at its own entry as the effective cap.

The lift entry is invoked in `--request-mode source` with its own output directory and evidence
path. `--tlsf-tools-build`, `--bindings-python`, `--bindings-site`, `--buddy-adapter`, and
`--real-check` pass through to that entry. Its stdout and stderr are buffered and never leak into
the Acacia result stream. A lifting win prints exactly `REALIZABLE\n` or `UNREALIZABLE\n` and exits
0 or 1. The wrapper accepts that win only when all source and target-certificate source bindings
contain the actual input SHA-256. It also enforces the bound capability's `route_kind`:
`exact-game-both-sides` may establish either side, while `real-proposal` and `sound-one-sided` may
establish only `REALIZABLE`. Decline, UNKNOWN, timeout, missing or malformed evidence, a missing or
mismatched input binding, an unlicensed verdict, an unverified verdict claim, or a decisive exit
without evidence falls back. Before fallback it synchronously kills and reaps the complete lifting
process group. Wrapper/configuration failures which prevent fallback exit 3.

## Coverage runner hook

Pass `--route-records DIR` to `benchmarking/run-syntcomp26-coverage.py` to enable the hook. For each
row the runner computes the absolute deadline immediately before launching the scope, creates a
unique record name, and propagates these variables through systemd with `--setenv`:

```text
ACACIA_OUTER_DEADLINE_MONOTONIC=<absolute monotonic seconds>
ACACIA_ROUTE_RECORD=<unique JSON path>
```

For example, a uniform-cap wrapper campaign can put the wrapper options and B command in the
existing `--flags` value:

```sh
python3 benchmarking/run-syntcomp26-coverage.py \
  --bin scripts/acacia-lift-portfolio.py \
  --flags '--lift-budget-fraction 0.3333333333333333 -- build_w1_B/src/acacia-bonsai' \
  --route-records /absolute/path/to/route-records \
  --caps 120 --memory-max 8G --memory-swap-max 0 \
  --solver-label N-a --list LIST --tlsf-map MAP --tlsf-corpus CORPUS --output N-a.tsv
```

With the hook enabled, `winner`, `lift_elapsed`, and `fallback_start` are appended to the raw TSV.
The route JSON also contains the input SHA-256, capability/binding reason, complete lift argv and
exit, evidence path/hash, stage censoring, fallback remaining time, and the winner. A timed-out
outer invocation may have blank TSV route fields because the wrapper was killed before its atomic
record write. Without `--route-records`, command construction, environment, and the raw TSV schema
remain the historical default; `export-cactus` is unchanged in both modes.
