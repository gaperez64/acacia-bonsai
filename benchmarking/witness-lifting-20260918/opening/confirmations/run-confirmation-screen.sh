#!/usr/bin/env bash
# Run alternating, serialized B/S confirmation pairs for one timeout cap.
# Usage: run-confirmation-screen.sh <cap> <list-file> <rounds>
set -euo pipefail

if [[ "$#" -ne 3 ]]; then
  echo "usage: $0 <cap> <list-file> <rounds>" >&2
  exit 2
fi

CAP="$1"
LIST_FILE="$2"
ROUNDS="$3"

if [[ ! "$CAP" =~ ^[1-9][0-9]*$ ]]; then
  echo "error: cap must be a positive integer" >&2
  exit 2
fi
if [[ ! "$ROUNDS" =~ ^[1-9][0-9]*$ ]]; then
  echo "error: rounds must be a positive integer" >&2
  exit 2
fi
if [[ ! -f "$LIST_FILE" ]]; then
  echo "error: list file does not exist: $LIST_FILE" >&2
  exit 2
fi
if [[ ! -s "$LIST_FILE" ]]; then
  echo "error: list file is empty: $LIST_FILE" >&2
  exit 2
fi

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../../../.." && pwd)"
LIST_FILE="$(realpath -- "$LIST_FILE")"

# The override keeps validation evidence out of the repository. Real campaign
# runs use the default, placing each screen below this script's directory.
CONFIRMATIONS_ROOT="${CONFIRMATIONS_ROOT:-$SCRIPT_DIR}"
OUT_DIR="$CONFIRMATIONS_ROOT/${CAP}s"
LOG="$OUT_DIR/screen.log"
TAG="${CAP}s-confirm"

TLSF_MAP="tests/suites/benchmarks/syntcomp26/tlsf-sources.tsv"
TLSF_CORPUS="tlsf-corpus"
RUNNER="benchmarking/run-syntcomp26-coverage.py"
PRESET="otf_sparse_formula"
ACACIA_SHA="50384cf69a733199fa17c9c571da1761d8451991"
B_BIN="$REPO_ROOT/build_w1_B/src/acacia-bonsai"
S_BIN="$REPO_ROOT/build_w1_S/src/acacia-bonsai"
B_SHA="398a420bfa939a7c80c67a0357022ed6f09279a1c2878116c0602abd14392e4a"
S_SHA="f80ff3f62daaa6e22a6753a3f30a127baab424d0320949bc9957a6ff62ea5ebe"

mkdir -p "$OUT_DIR"

log() {
  local line
  line="[$(date '+%Y-%m-%dT%H:%M:%S')] $*"
  echo "$line"
  echo "$line" >> "$LOG"
}

verify_binary() {
  local label="$1" binary="$2" expected_sha="$3" actual_sha
  if [[ ! -x "$binary" ]]; then
    log "ERROR $label binary is missing or not executable: $binary"
    return 1
  fi
  actual_sha="$(sha256sum "$binary")"
  actual_sha="${actual_sha%% *}"
  if [[ "$actual_sha" != "$expected_sha" ]]; then
    log "ERROR $label binary SHA-256 mismatch: expected=$expected_sha actual=$actual_sha"
    return 1
  fi
}

verify_binary B "$B_BIN" "$B_SHA"
verify_binary S "$S_BIN" "$S_SHA"

if [[ -e "$OUT_DIR/targets.list" ]]; then
  if ! cmp -s "$LIST_FILE" "$OUT_DIR/targets.list"; then
    log "ERROR existing targets.list differs from $LIST_FILE"
    exit 1
  fi
else
  cp -- "$LIST_FILE" "$OUT_DIR/targets.list"
fi

LIST_SHA="$(sha256sum "$OUT_DIR/targets.list")"
LIST_SHA="${LIST_SHA%% *}"
{
  printf 'tag\t%s\n' "$TAG"
  printf 'list_sha256\t%s\n' "$LIST_SHA"
  printf 'rounds\t%s\n' "$ROUNDS"
  printf 'B\tB|%s|%s||%s\n' "$B_BIN" "$B_SHA" "$PRESET"
  printf 'S\tS|%s|%s||%s\n' "$S_BIN" "$S_SHA" "$PRESET"
  printf 'kernel\t%s\n' "$(uname -r)"
} > "$OUT_DIR/screen-manifest.tsv"

run_side() {
  local round="$1" label="$2" binary out rc marker
  case "$label" in
    B) binary="$B_BIN" ;;
    S) binary="$S_BIN" ;;
    *) log "ERROR unknown side: $label"; return 2 ;;
  esac
  out="$OUT_DIR/r${round}-${label}.tsv"
  if [[ -f "$out.done" ]]; then
    marker="$(< "$out.done")"
    if [[ "$marker" == 0 ]]; then
      log "skip r$round $label (.done=0)"
      return 0
    fi
    log "ERROR existing failed marker: r$round $label (.done=$marker)"
    exit 1
  fi

  log "start r$round $label"
  if python3 "$RUNNER" \
      --bin "$binary" --solver-label "$TAG-$label-r$round" \
      --list "$OUT_DIR/targets.list" --tlsf-map "$TLSF_MAP" \
      --tlsf-corpus "$TLSF_CORPUS" --caps "$CAP" \
      --memory-max 8G --memory-swap-max 0 \
      --conflict-policy collect --collect-rusage --preset "$PRESET" \
      --acacia-sha "$ACACIA_SHA" --output "$out" >> "$LOG" 2>&1; then
    rc=0
  else
    rc=$?
  fi
  printf '%s\n' "$rc" > "$out.done"
  log "end r$round $label rc=$rc"
  return "$rc"
}

cd "$REPO_ROOT"
export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig:${PKG_CONFIG_PATH:-}"

log "SCREEN START $TAG targets=$(wc -l < "$OUT_DIR/targets.list") rounds=$ROUNDS"
for ((round = 1; round <= ROUNDS; round++)); do
  if ((round % 2 == 1)); then
    run_side "$round" B
    run_side "$round" S
  else
    run_side "$round" S
    run_side "$round" B
  fi
done

# Refuse to mark a screen complete if a prior invocation left a failed marker.
for ((round = 1; round <= ROUNDS; round++)); do
  for label in B S; do
    done_file="$OUT_DIR/r${round}-${label}.tsv.done"
    if [[ ! -f "$done_file" || "$(< "$done_file")" != 0 ]]; then
      log "ERROR incomplete side: r$round $label"
      exit 1
    fi
  done
done

log "SCREEN DONE $TAG"
touch "$OUT_DIR/SCREEN_DONE"
