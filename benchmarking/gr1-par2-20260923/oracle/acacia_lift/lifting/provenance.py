"""Structural seed windows and role coordinates from frontend provenance only."""
from __future__ import annotations

import json
import pathlib
import time
from collections import Counter, defaultdict
from dataclasses import dataclass

from acacia_lift.direct import Decline, run_command, sha256_file
from acacia_lift.tools import ToolConfiguration
from .settings import MAX_PREDICATE_ARITY, MAX_SIZES_PER_AXIS, SEED_CONFIRMATION
from .source import InstanceFiles, lower, parameters, require_frontend

FRONTEND_SCHEMA = "tlsf-tools.frontend-provenance.v1"
FRONTEND_VERSION = 1


@dataclass(frozen=True)
class SeedWindow:
    axis_name: str
    values: tuple[int, ...]
    instances: tuple[InstanceFiles, ...]
    members: tuple[int, ...]
    role_signatures: dict[int, tuple]


def frontend_version(snapshot_path: pathlib.Path, output: pathlib.Path,
                     config: ToolConfiguration, deadline: float) -> int:
    path = output / "frontend.json"
    proc = run_command([str(config.tlsf2tlsf), "--provenance-out", str(path),
                        "--output", "/dev/null", str(snapshot_path)], deadline,
                       "frontend_provenance")
    if proc.returncode != 0 or not path.is_file():
        raise Decline("frontend_provenance", "unavailable")
    data = json.loads(path.read_text(encoding="utf-8"))
    if (data.get("schema") != FRONTEND_SCHEMA or
            data.get("format_version") != FRONTEND_VERSION or
            data.get("ambiguous") is not False or
            data.get("source_sha256") != sha256_file(snapshot_path)):
        raise Decline("frontend_provenance", "ambiguous_or_version_mismatch")
    return FRONTEND_VERSION


def _declarations(data: dict) -> dict[str, list[dict]]:
    groups: dict[str, list[dict]] = defaultdict(list)
    for row in [*data["inputs"], *data["outputs"]]:
        groups[row["declaration_id"]].append(row)
    return groups


def axis_members(target: InstanceFiles, probe: InstanceFiles) -> tuple[tuple[int, ...], tuple[int, ...]]:
    """Find changed element buses; encoded-width bits are never clients."""
    target_groups, probe_groups = _declarations(target.data), _declarations(probe.data)
    if set(target_groups) != set(probe_groups):
        raise Decline("axis", "declaration_abi_changed")
    changed_target = []
    changed_probe = []
    for declaration, target_rows in target_groups.items():
        probe_rows = probe_groups[declaration]
        if len(target_rows) == len(probe_rows):
            continue
        if any(row.get("index_role") != "element" or row.get("dimensions") != 1
               for row in [*target_rows, *probe_rows]):
            raise Decline("axis", "unproved_encoding_width")
        changed_target.append(tuple(sorted(row["index_tuple"][0] for row in target_rows)))
        changed_probe.append(tuple(sorted(row["index_tuple"][0] for row in probe_rows)))
    if not changed_target:
        raise Decline("axis", "parameter_does_not_index_elements")
    if (any(values != changed_target[0] for values in changed_target[1:]) or
            any(values != changed_probe[0] for values in changed_probe[1:])):
        raise Decline("axis", "inconsistent_declaration_coordinates")
    return changed_target[0], changed_probe[0]


def _symmetric_key(record: dict, data: dict) -> tuple | None:
    signature = record.get("symmetric_signature")
    if not isinstance(signature, dict):
        return None
    names = {row["source_name"]: row["declaration_id"]
             for row in [*data["inputs"], *data["outputs"]]}
    result = []
    for source_name, counts in signature.items():
        declaration = names.get(source_name)
        if declaration is None or not isinstance(counts, list):
            return None
        width = sum(row["declaration_id"] == declaration
                    for row in [*data["inputs"], *data["outputs"]])
        if not counts:
            normalized = ("empty",)
        elif counts == list(range(counts[0], counts[-1] + 1)):
            normalized = ("interval", counts[0],
                          "all" if counts[-1] == width else counts[-1])
        else:
            normalized = ("set", *("all" if count == width else count
                                   for count in counts))
        result.append((declaration, normalized))
    return tuple(sorted(result))


def monitor_indices(record: dict, data: dict) -> tuple[int, ...]:
    """Use the origin's exact signal inventory when no binder owns a monitor."""
    bound = tuple(record["source_origin"]["index_tuple"])
    if bound:
        return bound
    signals = {row["name"]: row for row in [*data["inputs"], *data["outputs"]]}
    values = set()
    for name in record["source_origin"]["signals"]:
        row = signals.get(name)
        if row is None:
            raise Decline("provenance", "unmatched_monitor_signal")
        if row["index_role"] == "element":
            values.update(row["index_tuple"])
    return tuple(sorted(values))


def monitor_key(record: dict, data: dict) -> tuple:
    origin = record["source_origin"]
    identity = (origin["source_formula_id"], origin["source_node_id"])
    mode = data.get("_lift_modes", {}).get(identity)
    if mode == "template":
        if len(monitor_indices(record, data)) > MAX_PREDICATE_ARITY:
            raise Decline("bus_schema", "unproved_large_template")
        semantic = record["template"]
        kind = "bounded_support"
    elif mode == "symmetric" or record["arity_kind"] == "bus_wide":
        semantic = _symmetric_key(record, data)
        if semantic is None:
            if len(monitor_indices(record, data)) > MAX_PREDICATE_ARITY:
                raise Decline("bus_schema", "unproved_bus_wide_semantics")
            semantic = ("bounded_support", record["template"])
            kind = "bounded_support"
        else:
            kind = "symmetric"
    else:
        semantic = record["template"]
        kind = "bounded_support"
    return (origin["source_formula_id"], origin["source_node_id"],
            record["side"], record["role"], record["mp_class"],
            kind, record["state_count"], semantic)


def target_modes(target: InstanceFiles, probes: list[InstanceFiles]) -> None:
    """Interpret each seed origin with the target's semantic bus category."""
    modes = {}
    for record in target.data["monitors"]:
        origin = record["source_origin"]
        identity = (origin["source_formula_id"], origin["source_node_id"])
        modes[identity] = ("symmetric" if record["arity_kind"] == "bus_wide" and
                           _symmetric_key(record, target.data) is not None
                           else "template")
    for instance in [target, *probes]:
        instance.data["_lift_modes"] = modes


def structural_shape(instance: InstanceFiles) -> tuple:
    require_frontend(instance)
    data = instance.data
    declarations = tuple(sorted({(row["declaration_id"], row["direction"],
                                  row["dimensions"], row["index_role"],
                                  row["width_kind"])
                                 for row in [*data["inputs"], *data["outputs"]]}))
    monitors = tuple(sorted({monitor_key(row, data) for row in data["monitors"]},
                            key=repr))
    source_nodes = tuple(sorted({(row["source_formula_id"], row["source_node_id"],
                                 tuple(binding["binder_id"] for binding in row["bindings"]))
                                for row in data["source_conjuncts"]}, key=repr))
    return declarations, source_nodes, monitors, data["latch_encoding"]


def role_signatures(instance: InstanceFiles, members: tuple[int, ...]) -> dict[int, tuple]:
    """Classify positions by declaration membership and incident source origins."""
    data = instance.data
    n = len(members)
    incidents: dict[int, Counter] = {index: Counter() for index in members}
    for signal in [*data["inputs"], *data["outputs"]]:
        if signal["index_role"] == "element" and len(signal["index_tuple"]) == 1:
            index = signal["index_tuple"][0]
            if index in incidents:
                incidents[index][("declaration", signal["declaration_id"])] += 1
    for record in data["monitors"]:
        key = monitor_key(record, data)
        if key[5] == "symmetric":
            continue
        indices = monitor_indices(record, data)
        for position, index in enumerate(indices):
            if index in incidents:
                incidents[index][("monitor", key, position,
                                  len(record["source_origin"]["index_tuple"]))] += 1
    signals = {row["name"]: row for row in [*data["inputs"], *data["outputs"]]}
    for row in data["source_conjuncts"]:
        bindings = tuple(item["value"] for item in row["bindings"])
        signal_shape = []
        for name in row["signals"]:
            signal = signals.get(name)
            if signal is None:
                raise Decline("provenance", "unmatched_conjunct_signal")
            relation = tuple(("bound", tuple(position for position, value in
                                               enumerate(bindings) if value == coordinate))
                             if coordinate in bindings else ("fixed", coordinate)
                             for coordinate in signal["index_tuple"])
            signal_shape.append((signal["declaration_id"], relation))
        shape = (row["source_formula_id"], row["source_node_id"],
                 tuple(sorted(signal_shape, key=repr)))
        for position, index in enumerate(bindings):
            if index in incidents:
                incidents[index][("conjunct", shape, position, len(bindings))] += 1
    def degree(key: tuple, count: int) -> tuple:
        if key[0] in {"monitor", "conjunct"} and key[-1] > 1 and count == n - 1:
            return ("all_except_self",)
        if count == 1:
            return ("one",)
        if count == n:
            return ("all",)
        return ("count", count)
    return {index: tuple(sorted(((key, degree(key, count))
                                 for key, count in counts.items()), key=repr))
            for index, counts in incidents.items()}


def discover(snapshot_path: pathlib.Path, target: InstanceFiles, output: pathlib.Path,
             config: ToolConfiguration, deadline: float) -> SeedWindow:
    """Bounded one-axis probes, with a matching window and optional confirmation."""
    require_frontend(target)
    axes = parameters(target)
    if not axes:
        raise Decline("parameters", "absent")
    best: SeedWindow | None = None
    for name, target_value in axes:
        valid: list[tuple[int, InstanceFiles, tuple[int, ...]]] = []
        for size in range(1, min(target_value, MAX_SIZES_PER_AXIS + 1)):
            if time.monotonic() >= deadline:
                raise Decline("seed_window", "discovery_budget_exhausted")
            overrides = tuple((key, size if key == name else value)
                              for key, value in axes)
            destination = output / f"axis_{len(valid)}_{size}"
            try:
                instance = lower(snapshot_path, destination, config, deadline,
                                 overrides, target.data["semantics"])
                require_frontend(instance)
                _target_members, members = axis_members(target, instance)
                valid.append((size, instance, members))
            except Decline:
                continue
        target_modes(target, [row[1] for row in valid])
        target_shape = structural_shape(target)
        for left, right in zip(valid, valid[1:]):
            if right[0] != left[0] + 1:
                continue
            try:
                if structural_shape(left[1]) != target_shape or structural_shape(right[1]) != target_shape:
                    continue
                target_members, _probe_members = axis_members(target, left[1])
                if len(left[2]) >= len(right[2]) or len(right[2]) >= len(target_members):
                    # Target must have more positions than every seed. The
                    # equality check also rejects a parameter with no growth.
                    continue
                selected = [left, right]
                if SEED_CONFIRMATION:
                    following = next((row for row in valid if row[0] == right[0] + 1), None)
                    if following is not None:
                        if structural_shape(following[1]) != target_shape:
                            continue
                        selected.append(following)
                signatures = [role_signatures(item, members) for _, item, members in selected]
                classes = [set(values.values()) for values in signatures]
                if len({repr(value) for value in classes}) != 1:
                    continue
                candidate = SeedWindow(name, tuple(row[0] for row in selected),
                                       tuple(row[1] for row in selected),
                                       target_members,
                                       role_signatures(target, target_members))
                best = candidate
                break
            except Decline:
                continue
        if best is not None:
            break
    if best is None:
        raise Decline("seed_window", "no_stable_index_axis")
    return best
