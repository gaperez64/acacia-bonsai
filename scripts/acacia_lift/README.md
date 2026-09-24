# Generic direct-certified GR(1) route

`python -m acacia_lift.runner --request-mode source` lowers the actual TLSF
input in exact mode, solves the GR(1) game with an explicit profile, and checks
the exported winning certificate and policy independently. It accepts either
side only with a VERIFIED certificate bound to the input and game hashes. The
portfolio wrapper owns the shared budget and Acacia fallback.

Stage A contains no REAL lifting path. Historical family-specific experiments
live under `benchmarking/param-lift-20260922/legacy_lift/` and are unreachable
from the wrapper and production runner.
