"""Generic, source-bound exact GR(1) reduction and checked direct solve."""

from __future__ import annotations

import hashlib
import json
import os
import pathlib
import signal
import subprocess
import time
from dataclasses import dataclass

from acacia_lift.tools import ToolConfiguration, bindings_environment


class Decline(RuntimeError):
    def __init__(self, stage: str, reason: str):
        self.stage = stage
        self.reason = reason
        super().__init__(f"{stage}: {reason}")


@dataclass(frozen=True)
class CheckedResult:
    verdict: str
    side: str
    game: pathlib.Path
    game_sha256: str
    certificate: pathlib.Path
    certificate_sha256: str
    policy: pathlib.Path
    policy_sha256: str
    checker: dict
    stages: dict


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run_command(command: list[str], deadline: float, stage: str, *,
                environment: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    remaining = deadline - time.monotonic()
    if remaining <= 0:
        raise Decline(stage, "budget_exhausted")
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               text=True, start_new_session=True, env=environment)
    try:
        stdout, stderr = process.communicate(timeout=remaining)
    except subprocess.TimeoutExpired as error:
        os.killpg(process.pid, signal.SIGKILL)
        process.communicate()
        raise Decline(stage, "budget_exhausted") from error
    return subprocess.CompletedProcess(command, process.returncode, stdout, stderr)


def lower_exact(source: pathlib.Path, output: pathlib.Path, config: ToolConfiguration,
                deadline: float) -> tuple[pathlib.Path, pathlib.Path]:
    """Lower the actual TLSF bytes, with no parameter overrides."""
    game = output / "target.game.aag"
    provenance = output / "target.provenance.json"
    command = [str(config.bindings_python), str(config.monitor), str(source),
               "--semantics", "exact", "--output", str(game),
               "--provenance-out", str(provenance),
               "--tlsf2ltl", str(config.tlsf2ltl),
               "--tlsf2tlsf", str(config.tlsf2tlsf),
               "--tlsfinfo", str(config.tlsfinfo)]
    result = run_command(command, deadline, "exact_reduction",
                         environment=bindings_environment(config))
    if result.returncode != 0 or not game.is_file() or not provenance.is_file():
        raise Decline("exact_reduction", "unsupported_or_failed")
    data = json.loads(provenance.read_text(encoding="utf-8"))
    if data.get("semantics") != "exact":
        raise Decline("exact_reduction", "semantics_mismatch")
    return game, provenance


def run_exact_direct(source: pathlib.Path, output: pathlib.Path,
                     config: ToolConfiguration, deadline: float,
                     eligibility_budget_seconds: float) -> CheckedResult:
    """Solve either side and accept only the independent certificate check."""
    output.mkdir(parents=True, exist_ok=True)
    stages: dict[str, dict] = {}
    started = time.monotonic()
    game, provenance = lower_exact(
        source, output, config,
        min(deadline, started + eligibility_budget_seconds),
    )
    game_hash = sha256_file(game)
    stages["exact_reduction"] = {
        "elapsed_s": time.monotonic() - started,
        "game_sha256": game_hash,
        "provenance_sha256": sha256_file(provenance),
    }
    certificate = output / "target.certificate.aag"
    policy = output / "target.policy.aag"
    cert_json = pathlib.Path(str(certificate) + ".json")
    policy_json = pathlib.Path(str(policy) + ".json")
    started = time.monotonic()
    solve = run_command([
        str(config.solver), "--semantics", "exact", "--game-profile", "gr1",
        "--certificate", str(certificate), "--certificate-json", str(cert_json),
        "--policy", str(policy), "--policy-json", str(policy_json),
        "--oxidd-nodes", str(1 << 25), "--oxidd-cache", str(1 << 23), str(game),
    ], deadline, "target_solve")
    stages["target_solve"] = {"elapsed_s": time.monotonic() - started,
                              "exit_code": solve.returncode}
    if solve.returncode not in (0, 1):
        raise Decline("target_solve", "no_decisive_export")
    side = "system" if solve.returncode == 0 else "environment"
    verdict = "REALIZABLE" if solve.returncode == 0 else "UNREALIZABLE"
    if not all(path.is_file() for path in (certificate, policy, cert_json, policy_json)):
        raise Decline("target_solve", "missing_artifact")
    cert_meta = json.loads(cert_json.read_text(encoding="utf-8"))
    policy_meta = json.loads(policy_json.read_text(encoding="utf-8"))
    if (cert_meta.get("status") != verdict.lower() or
            cert_meta.get("side") != side or policy_meta.get("side") != side or
            cert_meta.get("reduction_semantics") != "exact" or
            policy_meta.get("reduction_semantics") != "exact" or
            (side == "environment" and
             cert_meta.get("environment_counter_strategy_exported") is not True)):
        raise Decline("target_solve", "artifact_metadata_mismatch")
    if sha256_file(game) != game_hash:
        raise Decline("target_check", "game_changed")
    check_json = output / "target.check.json"
    started = time.monotonic()
    check = run_command([
        str(config.checker), "--method", "certificate", "--timeout",
        str(max(0.001, deadline - time.monotonic())), "--node-cap", str(1 << 26),
        "--json-out", str(check_json), "--certificate", str(certificate),
        "--certificate-json", str(cert_json), str(game), str(policy),
    ], deadline, "target_check")
    stages["target_check"] = {"elapsed_s": time.monotonic() - started,
                              "exit_code": check.returncode}
    if check.returncode != 0 or not check_json.is_file():
        raise Decline("target_check", "certificate_not_verified")
    payload = json.loads(check_json.read_text(encoding="utf-8"))
    if (payload.get("verdict") != "VERIFIED" or
            payload.get("methods", {}).get("certificate", {}).get("verdict") != "VERIFIED"):
        raise Decline("target_check", "certificate_not_verified")
    if sha256_file(game) != game_hash:
        raise Decline("target_check", "game_changed")
    return CheckedResult(verdict, side, game, game_hash, certificate,
                         sha256_file(certificate), policy, sha256_file(policy),
                         payload, stages)
