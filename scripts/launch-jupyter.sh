#!/usr/bin/env bash
# Wrapper that puts Spot + acacia-bonsai Python bindings on the search paths,
# then launches a Jupyter notebook server. Intended as the entrypoint of the
# Jupyter Docker image, but also works standalone if the paths below point at
# a compiled checkout.
set -euo pipefail

SPOT_PREFIX="${SPOT_PREFIX:-/opt/spot_install}"
ACACIA_ROOT="${ACACIA_ROOT:-/opt/acacia-bonsai}"

# Spot installs its Python bindings under lib/pythonX.Y/site-packages. Glob
# the minor version so this keeps working if the base image's Python changes.
SPOT_PY_SITE="$(ls -d "${SPOT_PREFIX}"/lib/python*/site-packages 2>/dev/null | head -n1 || true)"
if [[ -z "${SPOT_PY_SITE}" ]]; then
    echo "launch-jupyter: could not find Spot site-packages under ${SPOT_PREFIX}/lib/python*/site-packages" >&2
    exit 1
fi

ACACIA_PY_DIR="${ACACIA_ROOT}/builddir/src/python"
if [[ ! -d "${ACACIA_PY_DIR}" ]]; then
    echo "launch-jupyter: acacia boomslang build dir not found at ${ACACIA_PY_DIR}" >&2
    exit 1
fi

export PYTHONPATH="${SPOT_PY_SITE}:${ACACIA_PY_DIR}${PYTHONPATH:+:${PYTHONPATH}}"
export LD_LIBRARY_PATH="${SPOT_PREFIX}/lib:${ACACIA_PY_DIR}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
# macOS-equivalent; no-op on Linux but kept in line with the original spec so
# the script is portable if someone runs it outside Docker on a Mac.
export DYLD_LIBRARY_PATH="${SPOT_PREFIX}/lib:${ACACIA_PY_DIR}${DYLD_LIBRARY_PATH:+:${DYLD_LIBRARY_PATH}}"

cd "${NOTEBOOK_DIR:-${ACACIA_ROOT}/python_examples}"

exec jupyter notebook \
    --ip=0.0.0.0 \
    --port="${JUPYTER_PORT:-8888}" \
    --no-browser \
    --allow-root \
    "$@"
