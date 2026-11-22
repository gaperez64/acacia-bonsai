#!/usr/bin/env bash
# Print a GitHub job output: true if the full Lean check is needed.
set -euo pipefail
event=$1
base=${2:-}

if [[ "$event" != pull_request && "$event" != push ]]; then
  echo true
  exit 0
fi
# New branches and force-pushes can have no available predecessor.
if [[ -z "$base" || "$base" =~ ^0+$ ]] || ! git cat-file -e "$base^{commit}"; then
  echo true
  exit 0
fi

# Read both directions: --name-only reports only a rename's destination.
# A file preserves NULs and lets either git failure stop the detector.
changed=$(mktemp)
trap 'rm -f -- "$changed"' EXIT
git diff -z --name-only --find-renames "$base" HEAD > "$changed"
git diff -z --name-only --find-renames HEAD "$base" >> "$changed"
while IFS= read -r -d '' path; do
  case "$path" in
    # All Lean sources, toolchain/Lake configuration and check/detection scripts.
    proofs/*) echo true; exit 0 ;;
    # Changes to installation, caches or execution must exercise the workflow.
    .github/workflows/lean.yml) echo true; exit 0 ;;
  esac
done < "$changed"
# Acacia's lakefile has no lean_exe targets or external golden/data inputs.
echo false
