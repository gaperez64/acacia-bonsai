#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

export LEAN_NUM_THREADS="${LEAN_NUM_THREADS:-1}"
export LEAN_STACK_SIZE_KB="${LEAN_STACK_SIZE_KB:-2048}"
export LEAN_MAIN_USE_THREAD="${LEAN_MAIN_USE_THREAD:-0}"

# No custom axioms, admitted steps, native decisions or partial definitions.
time grep -nREw --include='*.lean' 'sorry|admit|axiom|native_decide|partial' \
  SymmetryProofs.lean SymmetryProofs && exit 1
status=$?
if ((status != 1)); then
  exit "$status"
fi

# Imports must be refreshed before their users; keep the module checks serial.
for module in Automaton Detector Game Equivariance FixedPoint Transports Reuse \
  Backward Closure Enumeration NextPermutation LayoutBuilder PlanBuilder \
  Pickers BooleanBoundary ActionTable Mona AdaptiveK BuilderExamples Examples; do
  # Always elaborate the source, even on an exact .lake cache hit. The flags
  # match lakefile.toml, with worker/stack settings overridable for this check.
  time timeout 300 lake env lean -j"$LEAN_NUM_THREADS" -s"$LEAN_STACK_SIZE_KB" \
    -DElab.async=false -DwarningAsError=true "SymmetryProofs/$module.lean"
  time timeout 300 lake --rehash build "SymmetryProofs.$module"
done
time timeout 300 lake env lean -j"$LEAN_NUM_THREADS" -s"$LEAN_STACK_SIZE_KB" \
  -DElab.async=false -DwarningAsError=true SymmetryProofs.lean
time timeout 1500 lake --rehash build

# Build exactly the modules we audit, including unimported nested sources.
shopt -s globstar nullglob
modules=(SymmetryProofs.lean SymmetryProofs/**/*.lean)
modules=("${modules[@]%.lean}")
modules=("${modules[@]//\//.}")
time timeout 1500 lake --rehash build "${modules[@]}"
# Separate processes let executable roots declare the same global main.
for module in "${modules[@]}"; do
  time timeout 300 lake env lean -j"$LEAN_NUM_THREADS" -s"$LEAN_STACK_SIZE_KB" \
    --run AuditAxioms.lean "$module"
done
