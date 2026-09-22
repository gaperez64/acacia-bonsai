#!/usr/bin/env bash
# Extract the W2 queues and run the long-budget then historical-budget screens.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../../../.." && pwd)"
QUEUE="$REPO_ROOT/benchmarking/witness-lifting-20260918/opening/confirmations-queue.tsv"
LIST_120="$SCRIPT_DIR/120s-queue.list"
LIST_17="$SCRIPT_DIR/17s-queue.list"
RUN_SCREEN="$SCRIPT_DIR/run-confirmation-screen.sh"

log() { echo "[$(date '+%Y-%m-%dT%H:%M:%S')] $*"; }

counts="$(python3 - "$QUEUE" "$LIST_120" "$LIST_17" <<'PY'
import csv
import sys
from pathlib import Path

queue, out_120, out_17 = map(Path, sys.argv[1:])
buckets = {"120": [], "17": []}
with queue.open(encoding="utf-8", newline="") as stream:
    reader = csv.DictReader(stream, delimiter="\t")
    if not {"cap", "instance"}.issubset(reader.fieldnames or []):
        raise SystemExit(f"{queue}: expected cap and instance columns")
    for line_number, row in enumerate(reader, 2):
        cap = row["cap"].strip()
        instance = row["instance"].strip()
        if cap not in buckets:
            raise SystemExit(f"{queue}:{line_number}: unexpected cap {cap!r}")
        if not instance:
            raise SystemExit(f"{queue}:{line_number}: empty instance")
        buckets[cap].append(instance)

for cap, output in (("120", out_120), ("17", out_17)):
    instances = buckets[cap]
    if not instances:
        raise SystemExit(f"{queue}: no cap={cap} instances")
    if len(instances) != len(set(instances)):
        raise SystemExit(f"{queue}: duplicate cap={cap} instances")
    output.write_text("".join(f"{instance}\n" for instance in instances), encoding="utf-8")

print(len(buckets["120"]), len(buckets["17"]))
PY
)"
read -r count_120 count_17 <<< "$counts"

log "W2 CONFIRMATIONS START (120s=$count_120, 17s=$count_17)"
"$RUN_SCREEN" 120 "$LIST_120" 5
"$RUN_SCREEN" 17 "$LIST_17" 5
log "ALL CONFIRMATIONS COMPLETE"
