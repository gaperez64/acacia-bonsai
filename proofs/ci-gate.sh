#!/usr/bin/env bash
# This is the decision used by the required GitHub check.
set -euo pipefail
if [[ "${DETECTOR_RESULT:-}" == success ]]; then
  case "${LEAN_NEEDED:-}:${LEAN_RESULT:-}" in
    true:success|false:success|false:skipped) exit 0 ;;
  esac
fi
echo "Lean gate failed: detector=${DETECTOR_RESULT:-missing}, needed=${LEAN_NEEDED:-missing}, lean=${LEAN_RESULT:-missing}" >&2
exit 1
