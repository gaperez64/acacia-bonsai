"""Instantiate exact games from one immutable TLSF snapshot."""
from __future__ import annotations

import json
import pathlib
import shutil
from dataclasses import dataclass

from acacia_lift.direct import Decline, run_command, sha256_file
from acacia_lift.tools import ToolConfiguration, bindings_environment
from .settings import SOLVER_NODE_CAP

MONITOR_SCHEMA = "tlsf-tools.gr1-monitor-game.provenance.v3"


@dataclass(frozen=True)
class InstanceFiles:
    game: pathlib.Path
    provenance: pathlib.Path
    data: dict
    overrides: tuple[tuple[str, int], ...]


def snapshot(source: pathlib.Path, output: pathlib.Path) -> tuple[pathlib.Path, str]:
    """Spool exactly once and bind all later overrides to those bytes."""
    output.mkdir(parents=True, exist_ok=True)
    target = output / "source.tlsf"
    with source.open("rb") as src, target.open("xb") as dst:
        shutil.copyfileobj(src, dst)
    return target, sha256_file(target)


def lower(source: pathlib.Path, output: pathlib.Path, config: ToolConfiguration,
          deadline: float, overrides: tuple[tuple[str, int], ...] = (),
          semantics: str = "exact") -> InstanceFiles:
    if semantics not in {"exact", "strict"}:
        raise ValueError("unsupported reduction semantics")
    output.mkdir(parents=True, exist_ok=True)
    game = output / "game.aag"
    provenance = output / "provenance.json"
    command = [str(config.bindings_python), str(config.monitor), str(source),
               "--semantics", semantics, "--output", str(game),
               "--provenance-out", str(provenance),
               "--tlsf2ltl", str(config.tlsf2ltl),
               "--tlsf2tlsf", str(config.tlsf2tlsf),
               "--tlsfinfo", str(config.tlsfinfo)]
    for name, value in overrides:
        command.extend(("--param", f"{name}={value}"))
    result = run_command(command, deadline, "exact_reduction",
                         environment=bindings_environment(config))
    if result.returncode != 0 or not game.is_file() or not provenance.is_file():
        raise Decline("exact_reduction", "unsupported_or_failed")
    data = json.loads(provenance.read_text(encoding="utf-8"))
    if data.get("schema") != MONITOR_SCHEMA or data.get("semantics") != semantics:
        raise Decline("exact_reduction", "provenance_schema_or_semantics")
    if data.get("source_origin_metadata", {}).get("source_sha256") != sha256_file(source):
        raise Decline("source_binding", "provenance_source_hash_mismatch")
    return InstanceFiles(game, provenance, data, overrides)


def require_frontend(instance: InstanceFiles) -> None:
    data = instance.data
    origin = data.get("source_origin_metadata", {})
    if (data.get("provenance_source") != "frontend" or
            origin.get("available") is not True or
            origin.get("provenance_source") != "frontend" or
            any(record.get("provenance_source") != "frontend" for record in
                [*data.get("inputs", []), *data.get("outputs", []),
                 *data.get("monitors", [])])):
        raise Decline("provenance", "ambiguous_or_unavailable")
    names = [row.get("name") for row in [*data["inputs"], *data["outputs"]]]
    if len(names) != len(set(names)):
        raise Decline("provenance", "duplicate_signal")
    origins = [(row["source_formula_id"], row["generated_position"])
               for row in data["source_conjuncts"]]
    if len(origins) != len(set(origins)):
        raise Decline("provenance", "duplicate_conjunct_origin")
    for row in data["monitors"]:
        if not row.get("source_origin"):
            raise Decline("provenance", "missing_monitor_origin")


def parameters(instance: InstanceFiles) -> tuple[tuple[str, int], ...]:
    """Return only concrete frontend parameters; no text or filename inference."""
    result = []
    for row in instance.data.get("source_parameters", []):
        name, value = row.get("name"), row.get("value")
        if not isinstance(name, str) or not name or type(value) is not int:
            raise Decline("parameters", "nonconcrete")
        result.append((name, value))
    if len({name for name, _ in result}) != len(result):
        raise Decline("parameters", "duplicate")
    return tuple(result)


def solve_seed(instance: InstanceFiles, output: pathlib.Path,
               config: ToolConfiguration, deadline: float) -> pathlib.Path:
    output.mkdir(parents=True, exist_ok=True)
    cert = output / "certificate.aag"
    proc = run_command([
        str(config.solver), "--semantics", instance.data["semantics"],
        "--game-profile", "gr1",
        "--certificate", str(cert), "--certificate-json", str(cert) + ".json",
        "--oxidd-nodes", str(SOLVER_NODE_CAP),
        "--oxidd-cache", str(SOLVER_NODE_CAP // 4), str(instance.game),
    ], deadline, "seed_solve")
    if proc.returncode != 0 or not cert.is_file() or not pathlib.Path(str(cert) + ".json").is_file():
        raise Decline("seed_solve", "no_system_certificate")
    meta = json.loads(pathlib.Path(str(cert) + ".json").read_text(encoding="utf-8"))
    if (meta.get("status") != "realizable" or meta.get("side") != "system" or
            meta.get("reduction_semantics") != instance.data["semantics"]):
        raise Decline("seed_solve", "metadata_mismatch")
    return cert
