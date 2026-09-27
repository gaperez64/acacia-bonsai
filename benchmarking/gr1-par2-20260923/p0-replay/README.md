# P0 dependency-pin replay commands

These commands replay the 26 decisive M6 rows (22 REAL, 4 UNREAL) and four small certificate
controls against `subprojects/tlsf-tools/build-L0`. They retain the original 120 s whole-invocation
budget and write only below this directory. They do not overwrite `m6-campaign.tsv`.

`param-lift-campaign.py` does **not** create a cgroup or invoke `systemd-run`; it only applies its
monotonic deadline and process-group cleanup. The outer `systemd-run` below is therefore required
for `MemoryMax=8G` and `MemorySwapMax=0`. Run from the Acacia-Bonsai repository root, and do not run
rows in parallel.

```bash
set -u

ROOT=$(git rev-parse --show-toplevel)
REPLAY="$ROOT/benchmarking/gr1-par2-20260923/p0-replay"
CAMPAIGN="$ROOT/benchmarking/param-lift-20260922/param-lift-campaign.py"
BUILD="$ROOT/subprojects/tlsf-tools/build-L0"
PYTHON="$ROOT/.venv/bin/python"
BINDINGS_PYTHON=/usr/bin/python3.13
BINDINGS_SITE=/usr/local/lib64/python3.13/site-packages

mkdir -p "$REPLAY/evidence/m6" "$REPLAY/evidence/controls"
mkdir -p "$REPLAY/runs/m6" "$REPLAY/runs/controls"

run_case() {
  local side=$1 group=$2 family=$3 target=$4 seeds=$5
  local run_dir="$REPLAY/runs/$group/${family}-n${target}"
  local evidence="$REPLAY/evidence/$group/${family}-n${target}.json"
  local rc expected expected_verdict
  local seed_args=()
  if [ "$seeds" != - ]; then
    seed_args=(--seeds "$seeds")
  fi
  if [ -e "$run_dir" ] || [ -e "$evidence" ]; then
    echo "refusing to overwrite replay output: $family n=$target" >&2
    return 1
  fi

  if systemd-run --user --scope --quiet --collect \
      --unit="acacia-p0-${group}-${family}-n${target}" \
      -p MemoryMax=8G -p MemorySwapMax=0 \
      "$PYTHON" "$CAMPAIGN" \
      --family "$family" --target "$target" "${seed_args[@]}" \
      --semantics exact --budget 120 \
      --tlsf-tools-build "$BUILD" \
      --bindings-python "$BINDINGS_PYTHON" \
      --bindings-site "$BINDINGS_SITE" \
      --output-dir "$run_dir" \
      --evidence-out "$evidence"
  then
    rc=0
  else
    rc=$?
  fi

  expected=0
  expected_verdict=REALIZABLE
  if [ "$side" = UNREAL ]; then
    expected=1
    expected_verdict=UNREALIZABLE
  fi
  if [ "$rc" -ne "$expected" ]; then
    echo "unexpected exit $rc (wanted $expected): $side $family n=$target" >&2
    return 1
  fi
  if [ ! -f "$evidence" ]; then
    echo "missing evidence after campaign exit: $evidence" >&2
    return 1
  fi
  if ! "$PYTHON" - "$evidence" "$expected_verdict" <<'PY'
import json
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
expected = sys.argv[2]
try:
    payload = json.loads(path.read_text(encoding="utf-8"))
except (OSError, json.JSONDecodeError) as error:
    raise SystemExit(f"invalid evidence {path}: {error}") from error
actual = payload.get("result", {}).get("verdict")
if payload.get("target_verified") is not True or actual != expected:
    raise SystemExit(
        f"unverified evidence {path}: "
        f"target_verified={payload.get('target_verified')!r}, "
        f"verdict={actual!r}, expected={expected!r}"
    )
PY
  then
    echo "campaign evidence did not confirm $side: $family n=$target" >&2
    return 1
  fi
}
```

## Decisive M6 rows

This list is the exact `target_verified=true`, `within_120s=true` subset of
`benchmarking/param-lift-20260922/m6-campaign.tsv`.

```bash
while read -r side family target seeds; do
  run_case "$side" m6 "$family" "$target" "$seeds" || exit 1
done <<'EOF'
REAL amba_decomposed_lock 15 2,3,4
REAL arbiter 6 3,4
REAL arbiter 7 3,4
REAL arbiter 8 3,4
REAL arbiter 9 3,4
REAL arbiter 10 3,4
REAL arbiter_on_inpchange 5 2,3,4
REAL arbiter_on_inpchange 6 2,3,4
REAL arbiter_with_buffer 6 2,3,4
REAL arbiter_with_buffer 7 2,3,4
REAL arbiter_with_buffer 8 2,3,4
REAL arbiter_with_cancel 6 2,3,4
REAL arbiter_with_cancel 7 2,3,4
REAL arbiter_with_cancel 8 2,3,4
REAL arbiter_with_cancel 9 2,3,4
REAL load_balancer 8 2,3,4
REAL load_balancer 9 2,3,4
UNREAL load_balancer_unreal2 6 -
REAL prioritized_arbiter 7 3,4
REAL prioritized_arbiter 8 3,4
REAL prioritized_arbiter 9 3,4
REAL prioritized_arbiter 10 3,4
REAL prioritized_arbiter 12 3,4
UNREAL round_robin_arbiter_unreal2 5 -
UNREAL round_robin_arbiter_unreal2 6 -
UNREAL round_robin_arbiter_unreal2 7 -
EOF
```

## Small certificate controls

The REAL controls are the last previously-solved sizes above the standard seeds for the two fast
families. The UNREAL controls are small, previously-solved exact-certificate rows, including the
prioritized-arbiter dual path.

```bash
while read -r side family target seeds; do
  run_case "$side" controls "$family" "$target" "$seeds" || exit 1
done <<'EOF'
REAL arbiter 5 3,4
REAL prioritized_arbiter 5 3,4
UNREAL round_robin_arbiter_unreal2 3 -
UNREAL prioritized_arbiter_unreal2 3 -
EOF
```

The campaign exit convention is 0 for REALIZABLE, 1 for UNREALIZABLE, and 2 for UNKNOWN. The
`run_case` treats both expected decisive exits as success only when the fresh evidence also records
`target_verified=true` and the matching verdict. Missing, malformed, or contradictory evidence and
all unexpected exits terminate the command block with a nonzero status.
