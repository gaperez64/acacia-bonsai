# P1 checker attribution

`run.py` compares named `tlsfcertcheck` binaries on the deduplicated target
artifacts in the frozen P0 replay and S0 diagnostic roots. It never writes to
those roots. Runs are strictly serial, use AB then BA checker order in successive
rounds, and execute in fresh 8 GiB/no-swap user scopes.

Run from the Acacia repository root:

```sh
python3 benchmarking/gr1-par2-20260923/p1-attribution/run.py \
  --checker L0=subprojects/tlsf-tools/build-L0/tlsfcertcheck \
  --checker P1=/home/gperez/GIT-repos/tlsf-tools/build-P1-0435385/tlsfcertcheck \
  --rounds 2 \
  --timeout 300 \
  --output-prefix benchmarking/gr1-par2-20260923/p1-attribution/l0-vs-p1-20260924 \
  _bm-logs.gr1-par2-p0-replay-L0 \
  _bm-logs.gr1-par2-s0-run1/runs
```

This writes `l0-vs-p1-20260924.tsv` incrementally and
`l0-vs-p1-20260924.json` after all runs. Existing output is never overwritten.
The command exits 2 after writing the summary if decisive checker statuses
disagree, and exits 1 if a run has no status word or cgroup peak.

Use `--dry-run` (and omit `--output-prefix`) to print every target and checker
argv without starting a scope. The default command does not request checker
statistics, keeping L0 and P1 flags identical. To deliberately enable P1's
diagnostics in the timed P1 runs, add `--stats-for P1`; no extra checker run is
made.

## Discovered targets

The two frozen roots contain 33 unique target triples after SHA-256
deduplication. A target present in both roots retains both provenance paths in
the JSON summary.

System/REAL (`--method auto`):

- `amba_decomposed_lock-n15`
- `arbiter-n5`, `arbiter-n6`, `arbiter-n7`, `arbiter-n8`, `arbiter-n9`, `arbiter-n10`
- `arbiter_on_inpchange-n5`, `arbiter_on_inpchange-n6`, `arbiter_on_inpchange-n7`
- `arbiter_with_buffer-n6`, `arbiter_with_buffer-n7`, `arbiter_with_buffer-n8`
- `arbiter_with_cancel-n6`, `arbiter_with_cancel-n7`, `arbiter_with_cancel-n8`,
  `arbiter_with_cancel-n9`, `arbiter_with_cancel-n10`
- `load_balancer-n8`, `load_balancer-n9`
- `prioritized_arbiter-n5`, `prioritized_arbiter-n7`, `prioritized_arbiter-n8`,
  `prioritized_arbiter-n9`, `prioritized_arbiter-n10`, `prioritized_arbiter-n12`

Environment/UNREAL (`--method certificate`, using the exported environment
certificate and policy):

- `load_balancer_unreal2-n6`, `load_balancer_unreal2-n7`
- `prioritized_arbiter_unreal2-n3`
- `round_robin_arbiter_unreal2-n3`, `round_robin_arbiter_unreal2-n5`,
  `round_robin_arbiter_unreal2-n6`, `round_robin_arbiter_unreal2-n7`

## Checker argv

For a system target the checker argv is:

```text
CHECKER --method auto --timeout 300 --node-cap NODES \
  --json-out TEMP.json --certificate TARGET.certificate.aag \
  --certificate-json TARGET.certificate.aag.json \
  TARGET.game.aag TARGET.policy.aag
```

For an environment target it is:

```text
CHECKER --method certificate --timeout 300 --node-cap 67108864 \
  --json-out TEMP.json --certificate TARGET.certificate.aag \
  --certificate-json TARGET.certificate.aag.json \
  TARGET.game.aag TARGET.policy.aag
```

`NODES` reproduces the generalizer's target-check policy: 16,777,216 through
n=5, 33,554,432 for n=6 through n=10, and 67,108,864 thereafter. Environment
certificates always receive at least 67,108,864 nodes. The wrapper applies the
same 300-second value with `timeout -s KILL`, then reads its scope's
`memory.peak` before exiting and allowing `--collect` to remove the scope.
