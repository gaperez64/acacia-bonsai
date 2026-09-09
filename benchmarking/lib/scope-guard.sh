# Shared campaign scope guard.  Source this; do not execute it.
#
# A campaign must not start while stray Acacia systemd scopes from an earlier,
# interrupted campaign are still running: they contend for the CPU, and the
# measurement is then not the one the gate believes it took.  The outermost
# campaign claims the guard, refuses to start next to strays, and stops what it
# started.  A nested campaign sees the marker in the environment and leaves both
# the claim and the cleanup to its parent.
#
#     . "$repo_root/benchmarking/lib/scope-guard.sh"
#     acacia_scope_guard_begin regression-gate || exit 1
#     # ... and, from the script's own EXIT trap:
#     acacia_scope_guard_end
#
# The three gate scripts kept a copy of this each.  They had already drifted:
# one removed its snapshot unconditionally, one only on the outer path (leaking
# the file whenever it ran nested), and one put it inside a scratch directory.

acacia_scope_guard_outer=0
_acacia_scope_guard_snapshot=
_acacia_scope_guard_sweeper=$(
  cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd
)/sweep-acacia-scopes.py

# acacia_scope_guard_begin LABEL [SNAPSHOT_PATH]
# Returns non-zero if strays are present and ACACIA_ALLOW_STRAY_SCOPES is unset,
# so callers should write `|| exit 1`.
acacia_scope_guard_begin () {
  local label=$1
  _acacia_scope_guard_snapshot=${2:-$(mktemp /tmp/acacia-scope-snapshot.XXXXXX)}

  # Already inside a campaign: the parent owns the claim.
  if [[ -n ${ACACIA_CAMPAIGN_SCOPE_GUARD:-} ]]; then
    return 0
  fi

  if ! python3 "$_acacia_scope_guard_sweeper" \
       --check --snapshot "$_acacia_scope_guard_snapshot"; then
    [[ ${ACACIA_ALLOW_STRAY_SCOPES:-0} == 1 ]] || return 1
    echo "$label: continuing with ACACIA_ALLOW_STRAY_SCOPES=1; measurements may be under contention" >&2
  fi

  export ACACIA_CAMPAIGN_SCOPE_GUARD="$label:$$"
  acacia_scope_guard_outer=1
  return 0
}

# Safe to call unconditionally from an EXIT trap; never changes the exit status.
acacia_scope_guard_end () {
  if (( acacia_scope_guard_outer == 1 )); then
    python3 "$_acacia_scope_guard_sweeper" \
      --stop --snapshot "$_acacia_scope_guard_snapshot" || true
  fi
  [[ -n $_acacia_scope_guard_snapshot ]] && rm -f "$_acacia_scope_guard_snapshot"
  return 0
}
