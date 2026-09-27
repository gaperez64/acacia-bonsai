"""Global cost and route rules for online lifting.

These constants do not depend on source text, basename, or benchmark identity.
The lift slice runs discovery and proof first; exact direct solving receives any
remaining time. Nonparametric inputs go directly to the exact route.
"""
from __future__ import annotations

MAX_SIZES_PER_AXIS = 6
MAX_PREDICATE_ARITY = 4
MAX_SUBSETS_PER_PREDICATE = 2000
# Experimental alternative: require every seed move to reconstruct exactly.
# A failed enabled attempt declines; the target transition is the default.
LEARN_MOVE_SCHEMAS = False
MOVE_SCHEMA_SECONDS = 0.05
POLICY_PROOF_FRACTION = 0.75
DISCOVERY_SHARE = 0.20
SEED_CONFIRMATION = True
ROUTE_ORDER = "lift-then-direct"
DEFAULT_LIFT_FRACTION = 1 / 3
DEFAULT_RUNNER_BUDGET_SECONDS = 120.0
DEFAULT_ELIGIBILITY_BUDGET_SECONDS = 1.0
ELIGIBILITY_CAP_FRACTION = 0.05
CHECKER_TIMEOUT_FLOOR_SECONDS = 0.001
PROCESS_GROUP_CLEANUP_SECONDS = 1.0
PROCESS_POLL_INTERVAL_SECONDS = 0.005
SOLVER_NODE_CAP = 1 << 25
SOLVER_CACHE_CAP = 1 << 23
CHECKER_NODE_CAP = 1 << 26
BUDDY_INITIAL_NODES = 4_000_000
BUDDY_INITIAL_CACHE = 400_000
BUDDY_MAX_INCREASE = 1_000_000
