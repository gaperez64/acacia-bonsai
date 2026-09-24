"""Global cost and route rules for online lifting.

These constants do not depend on source text, basename, or benchmark identity.
The lift slice runs discovery and proof first; exact direct solving receives any
remaining time. Nonparametric inputs go directly to the exact route.
"""
from __future__ import annotations

MAX_SIZES_PER_AXIS = 6
MAX_PREDICATE_ARITY = 4
MAX_SUBSETS_PER_PREDICATE = 2000
MOVE_SCHEMA_SECONDS = 0.05
POLICY_PROOF_FRACTION = 0.75
DISCOVERY_SHARE = 0.20
SEED_CONFIRMATION = True
ROUTE_ORDER = "lift-then-direct"
SOLVER_NODE_CAP = 1 << 25
CHECKER_NODE_CAP = 1 << 26
