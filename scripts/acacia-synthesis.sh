#!/bin/bash
# Compatibility entry for the shared controller/stdout mode.
exec "$(dirname "$0")/acacia-bonsai.sh" --synthesis "$@"
