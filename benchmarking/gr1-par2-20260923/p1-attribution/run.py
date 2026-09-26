#!/usr/bin/env python3
"""Compare tlsfcertcheck binaries on frozen target certificate artifacts."""

from __future__ import annotations

import argparse
import csv
import dataclasses
import hashlib
import json
import math
import os
import pathlib
import re
import secrets
import shlex
import statistics
import subprocess
import sys
import tempfile
import time
from collections import Counter, defaultdict
from itertools import combinations


CERTIFICATE_SUFFIX = ".certificate.aag"
TARGET_RE = re.compile(r"(?P<family>.+)_(?P<n>[0-9]+)\.certificate\.aag\Z")
CHECKER_RE = re.compile(r"[A-Za-z0-9_.-]+\Z")
STATUS_WORDS = {
    "CERT_FAILED",
    "ERROR",
    "INTERNAL-ERROR",
    "INVALID",
    "REFUTED",
    "UNKNOWN",
    "VERIFIED",
}
DECISIVE_STATUS_WORDS = {"REFUTED", "VERIFIED"}
TSV_COLUMNS = (
    "target",
    "side",
    "n",
    "checker",
    "binary_sha256",
    "round",
    "argv",
    "exit_code",
    "status",
    "stdout_sha256",
    "wall_seconds",
    "memory_peak_bytes",
)


class HarnessError(RuntimeError):
    """A configuration, discovery, or measurement error."""


@dataclasses.dataclass(frozen=True)
class Checker:
    """One named checker executable."""

    name: str
    path: pathlib.Path
    sha256: str


@dataclasses.dataclass(frozen=True)
class Candidate:
    """One target triple before content deduplication."""

    family: str
    n: int
    side: str
    method: str
    node_cap: int
    game: pathlib.Path
    policy: pathlib.Path
    certificate: pathlib.Path
    certificate_json: pathlib.Path
    policy_json: pathlib.Path
    certificate_metadata: object
    policy_metadata: object
    hashes: tuple[str, str, str]
    root: pathlib.Path
    artifact_dir: pathlib.Path


@dataclasses.dataclass(frozen=True)
class Target:
    """A unique, content-addressed target checker input."""

    label: str
    family: str
    n: int
    side: str
    method: str
    node_cap: int
    game: pathlib.Path
    policy: pathlib.Path
    certificate: pathlib.Path
    certificate_json: pathlib.Path
    hashes: tuple[str, str, str]
    triple_sha256: str
    provenance: tuple[dict[str, str], ...]


@dataclasses.dataclass(frozen=True)
class Measurement:
    """One cold checker invocation."""

    target: str
    side: str
    n: int
    checker: str
    binary_sha256: str
    round: int
    argv: tuple[str, ...]
    exit_code: int
    status: str
    stdout_sha256: str
    wall_seconds: float
    memory_peak_bytes: int | None

    def tsv_row(self) -> dict[str, object]:
        """Return the stable TSV representation."""
        return {
            "target": self.target,
            "side": self.side,
            "n": self.n,
            "checker": self.checker,
            "binary_sha256": self.binary_sha256,
            "round": self.round,
            "argv": json.dumps(self.argv, separators=(",", ":")),
            "exit_code": self.exit_code,
            "status": self.status,
            "stdout_sha256": self.stdout_sha256,
            "wall_seconds": f"{self.wall_seconds:.9f}",
            "memory_peak_bytes": (
                "" if self.memory_peak_bytes is None else self.memory_peak_bytes
            ),
        }


def sha256_file(path: pathlib.Path) -> str:
    """Hash a file without loading it into memory."""
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_json(path: pathlib.Path) -> object:
    """Read a required JSON sidecar with a useful error."""
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise HarnessError(f"cannot read JSON sidecar {path}: {error}") from error


def checker_node_cap(n: int, side: str) -> int:
    """Reproduce generalize_gr1.py's target checker arena selection."""
    exponent = min(26, 24 + max(0, (n - 1) // 5))
    node_cap = 1 << exponent
    if side == "environment":
        node_cap = max(node_cap, 1 << 26)
    return node_cap


def certificate_side(metadata: object, path: pathlib.Path) -> str:
    """Validate and extract the side proved by a certificate sidecar."""
    if not isinstance(metadata, dict):
        raise HarnessError(f"certificate sidecar is not an object: {path}")
    status = metadata.get("status")
    side = metadata.get("side")
    if status == "realizable" and side in (None, "system"):
        return "system"
    if status == "unrealizable" and side == "environment":
        return "environment"
    raise HarnessError(
        f"unsupported or inconsistent certificate metadata in {path}: "
        f"status={status!r}, side={side!r}"
    )


def discover_candidates(root: pathlib.Path) -> list[Candidate]:
    """Discover complete target triples, excluding seed and probe artifacts."""
    if not root.is_dir():
        raise HarnessError(f"artifact root is not a directory: {root}")
    candidates = []
    for certificate in sorted(root.rglob(f"*{CERTIFICATE_SUFFIX}")):
        match = TARGET_RE.fullmatch(certificate.name)
        if match is None:
            continue
        family = match.group("family")
        n = int(match.group("n"))
        relative = certificate.relative_to(root)
        if (
            f"{family}-n{n}" not in relative.parts
            or any(part.startswith("probe-") for part in relative.parts)
        ):
            continue
        prefix = pathlib.Path(str(certificate)[: -len(CERTIFICATE_SUFFIX)])
        game = pathlib.Path(f"{prefix}.game.aag")
        policy = pathlib.Path(f"{prefix}.policy.aag")
        certificate_json = pathlib.Path(f"{certificate}.json")
        policy_json = pathlib.Path(f"{policy}.json")
        required = (game, policy, certificate_json, policy_json)
        if not all(path.is_file() for path in required):
            continue
        certificate_metadata = read_json(certificate_json)
        policy_metadata = read_json(policy_json)
        side = certificate_side(certificate_metadata, certificate_json)
        if not isinstance(policy_metadata, dict):
            raise HarnessError(f"policy sidecar is not an object: {policy_json}")
        policy_side = policy_metadata.get("side")
        if side == "system" and policy_side not in (None, "system"):
            raise HarnessError(f"system policy has side {policy_side!r}: {policy_json}")
        if side == "environment" and policy_side != "environment":
            raise HarnessError(
                f"environment policy has side {policy_side!r}: {policy_json}"
            )
        candidates.append(
            Candidate(
                family=family,
                n=n,
                side=side,
                method="certificate" if side == "environment" else "auto",
                node_cap=checker_node_cap(n, side),
                game=game.resolve(),
                policy=policy.resolve(),
                certificate=certificate.resolve(),
                certificate_json=certificate_json.resolve(),
                policy_json=policy_json.resolve(),
                certificate_metadata=certificate_metadata,
                policy_metadata=policy_metadata,
                hashes=(
                    sha256_file(game),
                    sha256_file(policy),
                    sha256_file(certificate),
                ),
                root=root,
                artifact_dir=certificate.parent.resolve(),
            )
        )
    return candidates


def triple_digest(hashes: tuple[str, str, str]) -> str:
    """Give the ordered triple a compact content identity."""
    digest = hashlib.sha256()
    for item in hashes:
        digest.update(bytes.fromhex(item))
    return digest.hexdigest()


def normalized_sidecar(metadata: object) -> object:
    """Remove the producer-local circuit path from otherwise semantic metadata."""
    if not isinstance(metadata, dict):
        return metadata
    normalized = dict(metadata)
    circuit = normalized.get("circuit")
    if isinstance(circuit, dict):
        normalized["circuit"] = {
            key: value for key, value in circuit.items() if key != "path"
        }
    return normalized


def deduplicate(candidates: list[Candidate]) -> list[Target]:
    """Deduplicate identical triples while retaining every provenance path."""
    grouped: dict[tuple[str, str, str], list[Candidate]] = defaultdict(list)
    for candidate in candidates:
        grouped[candidate.hashes].append(candidate)

    provisional = []
    for hashes, members in grouped.items():
        members.sort(key=lambda item: str(item.certificate))
        canonical = members[0]
        for duplicate in members[1:]:
            identity = (duplicate.family, duplicate.n, duplicate.side)
            expected = (canonical.family, canonical.n, canonical.side)
            if identity != expected:
                raise HarnessError(
                    "identical artifact triple has conflicting target identity: "
                    f"{expected!r} versus {identity!r}"
                )
            if normalized_sidecar(duplicate.certificate_metadata) != normalized_sidecar(
                canonical.certificate_metadata
            ):
                raise HarnessError(
                    "identical certificate AAG has conflicting JSON sidecars: "
                    f"{canonical.certificate_json} versus {duplicate.certificate_json}"
                )
            if normalized_sidecar(duplicate.policy_metadata) != normalized_sidecar(
                canonical.policy_metadata
            ):
                raise HarnessError(
                    "identical policy AAG has conflicting JSON sidecars: "
                    f"{canonical.policy_json} versus {duplicate.policy_json}"
                )
        provenance = tuple(
            {
                "root": str(member.root),
                "artifact_dir": str(member.artifact_dir),
            }
            for member in members
        )
        provisional.append((canonical, hashes, triple_digest(hashes), provenance))

    identity_counts = Counter(
        (candidate.family, candidate.n, candidate.side)
        for candidate, _hashes, _digest, _provenance in provisional
    )
    targets = []
    for candidate, hashes, digest, provenance in provisional:
        identity = (candidate.family, candidate.n, candidate.side)
        label = f"{candidate.family}-n{candidate.n}"
        if identity_counts[identity] > 1:
            label = f"{label}@{digest[:12]}"
        targets.append(
            Target(
                label=label,
                family=candidate.family,
                n=candidate.n,
                side=candidate.side,
                method=candidate.method,
                node_cap=candidate.node_cap,
                game=candidate.game,
                policy=candidate.policy,
                certificate=candidate.certificate,
                certificate_json=candidate.certificate_json,
                hashes=hashes,
                triple_sha256=digest,
                provenance=provenance,
            )
        )
    return sorted(
        targets,
        key=lambda target: (
            target.family,
            target.n,
            target.side,
            target.triple_sha256,
        ),
    )


def parse_checker(value: str) -> tuple[str, pathlib.Path]:
    """Parse NAME=PATH for argparse."""
    if "=" not in value:
        raise argparse.ArgumentTypeError("checker must have the form NAME=PATH")
    name, raw_path = value.split("=", 1)
    if not name or CHECKER_RE.fullmatch(name) is None:
        raise argparse.ArgumentTypeError(f"invalid checker name: {name!r}")
    if not raw_path:
        raise argparse.ArgumentTypeError("checker path must not be empty")
    return name, pathlib.Path(raw_path)


def load_checkers(specifications: list[tuple[str, pathlib.Path]]) -> list[Checker]:
    """Resolve, validate, and hash checker binaries in argument order."""
    names = set()
    checkers = []
    for name, raw_path in specifications:
        if name in names:
            raise HarnessError(f"duplicate checker name: {name}")
        names.add(name)
        path = raw_path.expanduser().resolve()
        if not path.is_file():
            raise HarnessError(f"checker is not a file: {path}")
        if not os.access(path, os.X_OK):
            raise HarnessError(f"checker is not executable: {path}")
        checkers.append(Checker(name=name, path=path, sha256=sha256_file(path)))
    return checkers


def timeout_text(seconds: float) -> str:
    """Format a positive timeout without an unnecessary decimal suffix."""
    if seconds.is_integer():
        return str(int(seconds))
    return format(seconds, ".15g")


def checker_argv(
    target: Target,
    checker: Checker,
    timeout: float,
    json_out: pathlib.Path,
    stats_enabled: bool,
) -> tuple[str, ...]:
    """Reproduce the generalizer's target-check argv for one binary."""
    argv = [
        str(checker.path),
        "--method",
        target.method,
        "--timeout",
        timeout_text(timeout),
        "--node-cap",
        str(target.node_cap),
        "--json-out",
        str(json_out),
        "--certificate",
        str(target.certificate),
        "--certificate-json",
        str(target.certificate_json),
    ]
    if stats_enabled:
        argv.append("--stats")
    argv.extend((str(target.game), str(target.policy)))
    return tuple(argv)


def status_word(stdout: bytes) -> str:
    """Extract the checker's terminal status line."""
    text = stdout.decode("utf-8", errors="replace")
    for line in reversed(text.splitlines()):
        word = line.strip()
        if word in STATUS_WORDS:
            return word
    return ""


def read_peak(path: pathlib.Path) -> int | None:
    """Read a wrapper-published cgroup memory.peak value."""
    try:
        value = int(path.read_text(encoding="ascii").strip())
    except (OSError, UnicodeError, ValueError):
        return None
    return value if value >= 0 else None


def run_measurement(
    target: Target,
    checker: Checker,
    round_number: int,
    timeout: float,
    stats_enabled: bool,
    scratch: pathlib.Path,
    sequence: int,
) -> Measurement:
    """Run one checker in a fresh bounded transient user scope."""
    token = secrets.token_hex(4)
    json_out = scratch / f"result-{sequence}-{token}.json"
    peak_file = scratch / f"peak-{sequence}-{token}.txt"
    argv = checker_argv(target, checker, timeout, json_out, stats_enabled)
    timed_argv = ("timeout", "-s", "KILL", f"{timeout_text(timeout)}s", *argv)
    shell_command = (
        f"{shlex.join(timed_argv)}; rc=$?; "
        "cat /sys/fs/cgroup$(cut -d: -f3 /proc/self/cgroup)/memory.peak > "
        f"{shlex.quote(str(peak_file))}; exit $rc"
    )
    unit = f"p1attr-{os.getpid()}-{sequence}-{token}"
    scope_argv = (
        "systemd-run",
        "--user",
        "--scope",
        "--quiet",
        "--collect",
        "-p",
        "MemoryMax=8G",
        "-p",
        "MemorySwapMax=0",
        f"--unit={unit}",
        "sh",
        "-c",
        shell_command,
    )
    started = time.monotonic()
    completed = subprocess.run(
        scope_argv,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    wall_seconds = time.monotonic() - started
    exit_code = completed.returncode
    stdout = completed.stdout
    stderr = completed.stderr
    if stderr:
        sys.stderr.buffer.write(stderr)
        if not stderr.endswith(b"\n"):
            sys.stderr.buffer.write(b"\n")
        sys.stderr.buffer.flush()
    peak = read_peak(peak_file)
    try:
        peak_file.unlink(missing_ok=True)
        json_out.unlink(missing_ok=True)
    except OSError as error:
        print(f"warning: could not remove temporary checker output: {error}", file=sys.stderr)
    return Measurement(
        target=target.label,
        side=target.side,
        n=target.n,
        checker=checker.name,
        binary_sha256=checker.sha256,
        round=round_number,
        argv=argv,
        exit_code=exit_code,
        status=status_word(stdout),
        stdout_sha256=hashlib.sha256(stdout).hexdigest(),
        wall_seconds=wall_seconds,
        memory_peak_bytes=peak,
    )


def ratio(numerator: float | int | None, denominator: float | int | None) -> float | None:
    """Return a finite benchmark ratio when both measurements exist."""
    if numerator is None or denominator is None or denominator <= 0:
        return None
    return numerator / denominator


def find_disagreements(
    measurements: list[Measurement], checker_names: list[str]
) -> list[dict[str, object]]:
    """Find internal and cross-checker decisive-status disagreements."""
    by_target: dict[str, dict[str, set[str]]] = defaultdict(lambda: defaultdict(set))
    for measurement in measurements:
        if measurement.status in DECISIVE_STATUS_WORDS:
            by_target[measurement.target][measurement.checker].add(measurement.status)
    disagreements = []
    for target, checker_statuses in sorted(by_target.items()):
        for checker in checker_names:
            statuses = sorted(checker_statuses.get(checker, set()))
            if len(statuses) > 1:
                disagreements.append(
                    {
                        "target": target,
                        "kind": "within-checker",
                        "checker": checker,
                        "statuses": statuses,
                    }
                )
        for left, right in combinations(checker_names, 2):
            left_statuses = checker_statuses.get(left, set())
            right_statuses = checker_statuses.get(right, set())
            if left_statuses and right_statuses and left_statuses != right_statuses:
                disagreements.append(
                    {
                        "target": target,
                        "kind": "between-checkers",
                        "checkers": [left, right],
                        "statuses": {
                            left: sorted(left_statuses),
                            right: sorted(right_statuses),
                        },
                    }
                )
    return disagreements


def make_summary(
    roots: list[pathlib.Path],
    targets: list[Target],
    checkers: list[Checker],
    rounds: int,
    timeout: float,
    stats_for: set[str],
    measurements: list[Measurement],
) -> dict[str, object]:
    """Aggregate the append-only rows into the requested JSON summary."""
    by_target_checker: dict[tuple[str, str], list[Measurement]] = defaultdict(list)
    for measurement in measurements:
        by_target_checker[(measurement.target, measurement.checker)].append(measurement)

    target_summaries = {}
    for target in targets:
        checker_summaries = {}
        aggregates: dict[str, tuple[float | None, int | None]] = {}
        for checker in checkers:
            rows = sorted(
                by_target_checker[(target.label, checker.name)],
                key=lambda row: row.round,
            )
            walls = [row.wall_seconds for row in rows]
            peaks = [
                row.memory_peak_bytes
                for row in rows
                if row.memory_peak_bytes is not None
            ]
            median_wall = statistics.median(walls) if walls else None
            max_peak = max(peaks) if peaks else None
            aggregates[checker.name] = (median_wall, max_peak)
            checker_summaries[checker.name] = {
                "statuses": [row.status for row in rows],
                "status_counts": dict(sorted(Counter(row.status for row in rows).items())),
                "exit_codes": [row.exit_code for row in rows],
                "median_wall_seconds": median_wall,
                "max_memory_peak_bytes": max_peak,
            }
        p1_wall, p1_peak = aggregates.get("P1", (None, None))
        l0_wall, l0_peak = aggregates.get("L0", (None, None))
        target_summaries[target.label] = {
            "family": target.family,
            "n": target.n,
            "side": target.side,
            "method": target.method,
            "node_cap": target.node_cap,
            "triple_sha256": target.triple_sha256,
            "artifact_sha256": {
                "game": target.hashes[0],
                "policy": target.hashes[1],
                "certificate": target.hashes[2],
            },
            "provenance": list(target.provenance),
            "checkers": checker_summaries,
            "ratios": {
                "P1/L0": {
                    "median_wall_seconds": ratio(p1_wall, l0_wall),
                    "max_memory_peak_bytes": ratio(p1_peak, l0_peak),
                }
            },
        }

    checker_names = [checker.name for checker in checkers]
    disagreements = find_disagreements(measurements, checker_names)
    missing_peaks = [
        {
            "target": row.target,
            "checker": row.checker,
            "round": row.round,
            "exit_code": row.exit_code,
        }
        for row in measurements
        if row.memory_peak_bytes is None
    ]
    missing_statuses = [
        {
            "target": row.target,
            "checker": row.checker,
            "round": row.round,
            "exit_code": row.exit_code,
        }
        for row in measurements
        if not row.status
    ]
    return {
        "format": "acacia-p1-checker-attribution-v1",
        "artifact_roots": [str(root) for root in roots],
        "rounds": rounds,
        "timeout_seconds": timeout,
        "checker_order": checker_names,
        "stats_for": sorted(stats_for),
        "checkers": {
            checker.name: {
                "path": str(checker.path),
                "binary_sha256": checker.sha256,
            }
            for checker in checkers
        },
        "targets": target_summaries,
        "disagreements": disagreements,
        "measurement_errors": {
            "missing_memory_peak": missing_peaks,
            "missing_status_word": missing_statuses,
        },
    }


def print_dry_run(
    targets: list[Target],
    checkers: list[Checker],
    rounds: int,
    timeout: float,
    stats_for: set[str],
) -> None:
    """Print all target identities and checker argument vectors."""
    print(f"targets: {len(targets)} unique triples")
    for target in targets:
        print(
            f"  {target.label}: side={target.side} n={target.n} "
            f"sha256={target.triple_sha256} provenance={len(target.provenance)}"
        )
        for provenance in target.provenance:
            print(f"    {provenance['artifact_dir']}")
    print("plan:")
    for round_number in range(1, rounds + 1):
        ordered = checkers if round_number % 2 else list(reversed(checkers))
        for target in targets:
            for checker in ordered:
                placeholder = pathlib.Path(
                    f"<temporary-json:{target.label}:{checker.name}:r{round_number}>"
                )
                argv = checker_argv(
                    target,
                    checker,
                    timeout,
                    placeholder,
                    checker.name in stats_for,
                )
                print(
                    f"  round={round_number} target={target.label} "
                    f"checker={checker.name}: {shlex.join(argv)}"
                )


def parser() -> argparse.ArgumentParser:
    """Build the command-line parser."""
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument(
        "roots",
        type=pathlib.Path,
        nargs="+",
        metavar="ROOT",
        help="frozen artifact root to scan (repeat positionally)",
    )
    result.add_argument(
        "--checker",
        action="append",
        required=True,
        type=parse_checker,
        metavar="NAME=PATH",
        help="named tlsfcertcheck binary; repeat in baseline order",
    )
    result.add_argument("--rounds", type=int, default=2)
    result.add_argument("--timeout", type=float, default=300.0)
    result.add_argument(
        "--stats-for",
        action="append",
        default=[],
        metavar="NAME",
        help="pass --stats to this checker; repeat for multiple checkers",
    )
    result.add_argument(
        "--output-prefix",
        type=pathlib.Path,
        help="write PREFIX.tsv and PREFIX.json (required unless --dry-run)",
    )
    result.add_argument("--dry-run", action="store_true")
    return result


def main(argv: list[str] | None = None) -> int:
    """Run the attribution harness."""
    args = parser().parse_args(argv)
    if args.rounds <= 0:
        raise HarnessError("--rounds must be positive")
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        raise HarnessError("--timeout must be positive")
    if not args.dry_run and args.output_prefix is None:
        raise HarnessError("--output-prefix is required unless --dry-run is used")

    roots = [root.expanduser().resolve() for root in args.roots]
    checkers = load_checkers(args.checker)
    checker_names = {checker.name for checker in checkers}
    stats_for = set(args.stats_for)
    unknown_stats = sorted(stats_for - checker_names)
    if unknown_stats:
        raise HarnessError(
            "--stats-for names no configured checker: " + ", ".join(unknown_stats)
        )
    candidates = []
    for root in roots:
        candidates.extend(discover_candidates(root))
    targets = deduplicate(candidates)
    if not targets:
        raise HarnessError("no complete target artifact triples found")

    if args.dry_run:
        print_dry_run(targets, checkers, args.rounds, args.timeout, stats_for)
        return 0

    output_prefix = args.output_prefix.expanduser().resolve()
    output_prefix.parent.mkdir(parents=True, exist_ok=True)
    tsv_path = pathlib.Path(f"{output_prefix}.tsv")
    summary_path = pathlib.Path(f"{output_prefix}.json")
    collisions = [path for path in (tsv_path, summary_path) if path.exists()]
    if collisions:
        raise HarnessError(
            "refusing to overwrite output: " + ", ".join(map(str, collisions))
        )

    measurements = []
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_APPEND
    descriptor = os.open(tsv_path, flags, 0o644)
    with os.fdopen(descriptor, "w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(
            stream,
            fieldnames=TSV_COLUMNS,
            delimiter="\t",
            lineterminator="\n",
        )
        writer.writeheader()
        stream.flush()
        os.fsync(stream.fileno())
        sequence = 0
        with tempfile.TemporaryDirectory(prefix="p1-attribution-") as raw_scratch:
            scratch = pathlib.Path(raw_scratch)
            for round_number in range(1, args.rounds + 1):
                ordered = checkers if round_number % 2 else list(reversed(checkers))
                for target in targets:
                    for checker in ordered:
                        sequence += 1
                        print(
                            f"run {sequence}: round={round_number} "
                            f"target={target.label} checker={checker.name}",
                            file=sys.stderr,
                            flush=True,
                        )
                        measurement = run_measurement(
                            target,
                            checker,
                            round_number,
                            args.timeout,
                            checker.name in stats_for,
                            scratch,
                            sequence,
                        )
                        measurements.append(measurement)
                        writer.writerow(measurement.tsv_row())
                        stream.flush()
                        os.fsync(stream.fileno())
                        peak = (
                            "missing"
                            if measurement.memory_peak_bytes is None
                            else str(measurement.memory_peak_bytes)
                        )
                        status = measurement.status or "<missing>"
                        print(
                            f"  status={status} exit={measurement.exit_code} "
                            f"wall={measurement.wall_seconds:.6f}s peak={peak}",
                            file=sys.stderr,
                            flush=True,
                        )

    summary = make_summary(
        roots,
        targets,
        checkers,
        args.rounds,
        args.timeout,
        stats_for,
        measurements,
    )
    with summary_path.open("x", encoding="utf-8") as stream:
        json.dump(summary, stream, indent=2, sort_keys=True)
        stream.write("\n")

    disagreements = summary["disagreements"]
    measurement_errors = summary["measurement_errors"]
    assert isinstance(measurement_errors, dict)
    missing_peaks = measurement_errors["missing_memory_peak"]
    missing_statuses = measurement_errors["missing_status_word"]
    if disagreements:
        print("STATUS DISAGREEMENT:", file=sys.stderr)
        print(json.dumps(disagreements, indent=2, sort_keys=True), file=sys.stderr)
    if missing_peaks or missing_statuses:
        print("MEASUREMENT ERROR:", file=sys.stderr)
        print(json.dumps(measurement_errors, indent=2, sort_keys=True), file=sys.stderr)
    print(f"wrote {tsv_path}")
    print(f"wrote {summary_path}")
    if disagreements:
        return 2
    if missing_peaks or missing_statuses:
        return 1
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except HarnessError as error:
        print(f"p1-attribution: {error}", file=sys.stderr)
        raise SystemExit(2) from error
