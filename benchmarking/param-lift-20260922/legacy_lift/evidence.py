"""Output-only generalizer result artifact."""

from __future__ import annotations

import csv
import pathlib


RESULT_COLUMNS = [
    "family", "target", "seeds", "arity", "role_classes",
    "predicate_arities", "stage_reached", "stages_passed",
    "cegis_rounds", "verdict",
    "seed_s", "canonicalize_s", "anti_unify_s", "bus_schemas_s",
    "ranks_s", "instantiate_s", "cegis_s", "wall_s",
    "seed_monitor_s", "seed_solve_s", "target_monitor_s",
    "target_solve_s", "target_check_s", "probe_check_s",
    "driver_overhead_s", "peak_rss_kib", "solver_nodes", "solver_cache",
    "checker_node_caps", "reason",
]


def write_result(path: pathlib.Path, row: dict[str, object]) -> None:
    """Write a single invocation result without consuming an earlier ledger."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=RESULT_COLUMNS,
                                delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerow({column: row.get(column, "") for column in RESULT_COLUMNS})
