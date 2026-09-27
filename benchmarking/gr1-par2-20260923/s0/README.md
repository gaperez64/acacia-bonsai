# S0 diagnostic capture

Run the block below from the Acacia-Bonsai repository root. It is the complete, serialized target
list from plan §1.2 plus the two controls requested by Brief S0. Every solver invocation gets one
fresh cgroup scope, the same 120-second whole-invocation budget, an 8 GiB memory limit, no swap, the
L0 tlsf-tools build, and historical dataset-reproducer request binding. Runs are deliberately not
parallelized.

```bash
set -u

ROOT=$(git rev-parse --show-toplevel)
RAW="$ROOT/benchmarking/gr1-par2-20260923/s0/raw"
CAMPAIGN="$ROOT/benchmarking/param-lift-20260922/param-lift-campaign.py"
PYTHON="$ROOT/.venv/bin/python"
BUILD="$ROOT/subprojects/tlsf-tools/build-L0"
BINDINGS_PYTHON=/usr/bin/python3.13
BINDINGS_SITE=/usr/local/lib64/python3.13/site-packages

if [ -e "$RAW/invocations.tsv" ]; then
  echo "refusing to overwrite S0 invocation manifest: $RAW/invocations.tsv" >&2
  exit 1
fi
mkdir -p "$RAW/runs" "$RAW/evidence"
printf 'family\tn\texit_code\tdiagnostics\tevidence\n' > "$RAW/invocations.tsv"

run_case() {
  local family=$1 target=$2 seeds=$3
  local stem="${family}-n${target}"
  local diagnostic="$RAW/${stem}.json"
  local evidence="$RAW/evidence/${stem}.json"
  local run_dir="$RAW/runs/${stem}"
  local seed_args=()
  local rc

  if [ "$seeds" != - ]; then
    seed_args=(--seeds "$seeds")
  fi
  if [ -e "$diagnostic" ] || [ -e "$evidence" ] || [ -e "$run_dir" ] || \
      [ -e "$RAW/${stem}.log" ]; then
    echo "refusing to overwrite S0 output: $stem" >&2
    return 125
  fi

  if systemd-run --user --scope --quiet --collect \
      --unit="acacia-s0-${stem}" \
      -p MemoryMax=8G -p MemorySwapMax=0 \
      "$PYTHON" "$CAMPAIGN" \
      --request-mode reproducer \
      --family "$family" --target "$target" "${seed_args[@]}" \
      --semantics exact --budget 120 \
      --tlsf-tools-build "$BUILD" \
      --bindings-python "$BINDINGS_PYTHON" \
      --bindings-site "$BINDINGS_SITE" \
      --output-dir "$run_dir" \
      --evidence-out "$evidence" \
      --diagnostics "$diagnostic" \
      > "$RAW/${stem}.log" 2>&1
  then
    rc=0
  else
    rc=$?
  fi
  printf '%s\t%s\t%s\t%s\t%s\n' \
    "$family" "$target" "$rc" "$diagnostic" "$evidence" \
    >> "$RAW/invocations.tsv"
}

while read -r family target seeds; do
  run_case "$family" "$target" "$seeds" || exit $?
done <<'EOF'
arbiter_with_cancel 8 2,3,4
arbiter_with_cancel 9 2,3,4
arbiter_with_cancel 10 2,3,4
load_balancer_unreal2 6 -
load_balancer_unreal2 7 -
load_balancer 8 2,3,4
load_balancer 9 2,3,4
amba_decomposed_lock 15 2,3,4
arbiter_with_buffer 8 2,3,4
arbiter_with_buffer 9 2,3,4
arbiter_on_inpchange 6 2,3,4
arbiter_on_inpchange 7 2,3,4
round_robin_arbiter_unreal2 5 -
round_robin_arbiter_unreal2 6 -
round_robin_arbiter_unreal2 7 -
collector_v1 11 3
arbiter 6 3,4
prioritized_arbiter 7 3,4
EOF
```

The campaign samples `/sys/fs/cgroup/<own-cgroup>/memory.peak` during finalization, after every
solver/checker child has exited and before the final evidence and diagnostic writes. The evidence
field is `cgroup_memory_peak`; it records the byte value, resolved cgroup path, source file, and exact
sample point. This is the whole scope's peak, not the older `max(RUSAGE_SELF, RUSAGE_CHILDREN)` value,
which remains separately labelled as `peak_rss_kib`. When cgroup v2 or `memory.peak` is unavailable,
`available` is false rather than substituting process RSS.

Each top-level `--diagnostics` path is the sole diagnostic artifact for that campaign invocation. The
campaign embeds its child's generalizer document and removes the temporary child copy. A checker
`--help` probe records whether `--stats` is supported; supported checker JSON is included per attempt,
while L0 records `supported: false` without changing the checker command. The schema is
[`diagnostics.schema.json`](diagnostics.schema.json).

Only `raw/runs/` is ignored: it contains the bulky regenerated AIG workspaces. After the serialized
run, commit `raw/invocations.tsv`, `raw/*.json`, `raw/evidence/*.json`, and `raw/*.log` so verdicts,
diagnostics, evidence, and logs remain visible to normal Git review.

If the ignored workspaces are needed to support those records, relocate them to a named frozen
artifact directory such as `_bm-logs.gr1-par2-s0-run1/runs/`, make every file there read-only, and
commit a small `raw/frozen-runs.manifest.sha256` whose first line names that location and whose
remaining lines contain SHA-256 hashes relative to it. Do not regenerate or overwrite a completed
row or frozen artifact in place.

Summarize a completed capture with:

```bash
"$PYTHON" benchmarking/gr1-par2-20260923/s0/summarize.py "$RAW"
```

This prints the report and writes `s0-summary.md` beside `raw/`.
