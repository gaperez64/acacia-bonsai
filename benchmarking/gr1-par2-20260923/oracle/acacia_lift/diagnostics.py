"""Small, opt-in helpers for the GR(1) S0 diagnostic documents."""

from __future__ import annotations

import hashlib
import json
import pathlib
import sys
import time
import uuid
from collections import Counter
from typing import Any


SCHEMA_VERSION = 1


def sha256(path: pathlib.Path) -> str | None:
    """Return a file hash, or ``None`` for a missing/non-file path."""
    try:
        if not path.is_file():
            return None
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
        return digest.hexdigest()
    except OSError:
        return None


def atomic_json(path: pathlib.Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.{uuid.uuid4().hex}.tmp")
    try:
        temporary.write_text(
            json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        temporary.replace(path)
    finally:
        try:
            temporary.unlink(missing_ok=True)
        except OSError:
            pass


def cgroup_memory_peak(
    proc_cgroup: pathlib.Path = pathlib.Path("/proc/self/cgroup"),
    cgroup_root: pathlib.Path = pathlib.Path("/sys/fs/cgroup"),
) -> dict[str, Any]:
    """Sample cgroup-v2 memory.peak for this process's whole scope."""
    result: dict[str, Any] = {
        "available": False,
        "bytes": None,
        "cgroup_path": None,
        "source": None,
        "sample_point": "campaign-finalization-before-process-exit",
    }
    try:
        row = next(
            line for line in proc_cgroup.read_text(encoding="utf-8").splitlines()
            if line.startswith("0::")
        )
        relative = pathlib.PurePosixPath(row[3:])
        safe_parts = tuple(part for part in relative.parts if part not in ("", "/"))
        if any(part == ".." for part in safe_parts):
            return result
        peak_path = cgroup_root.joinpath(*safe_parts, "memory.peak")
        raw = peak_path.read_text(encoding="utf-8").strip()
        if raw == "max":
            return result
        result.update({
            "available": True,
            "bytes": int(raw),
            "cgroup_path": "/" + "/".join(safe_parts),
            "source": str(peak_path),
        })
    except (OSError, StopIteration, ValueError):
        pass
    return result


class Diagnostics:
    """Aggregate phase timers and persist useful partial state on cancellation."""

    def __init__(self, tool: str, output: pathlib.Path):
        self.tool = tool
        self.output = output
        self.started = time.monotonic()
        self.phases: dict[str, dict[str, float | int]] = {}
        self.counters: Counter[str] = Counter()
        self.active: dict[int, tuple[str, str, float]] = {}
        self.censored: list[dict[str, Any]] = []
        self._censored_tokens: set[int] = set()
        self._next_token = 0
        self.extra: dict[str, Any] = {}
        self.errors: list[dict[str, str]] = []

    def record_error(self, operation: str, error: BaseException) -> None:
        """Remember a contained instrumentation failure without raising it."""
        try:
            self.errors.append({
                "operation": operation,
                "type": type(error).__name__,
                "message": str(error),
            })
        except Exception:
            # Diagnostics must never become part of the proof/control path.
            pass

    def begin(self, name: str, kind: str = "phase") -> int:
        self._next_token += 1
        token = self._next_token
        self.active[token] = (name, kind, time.monotonic())
        return token

    def end(self, token: int) -> float:
        item = self.active.pop(token, None)
        if item is None:
            return 0.0
        name, kind, started = item
        elapsed = max(0.0, time.monotonic() - started)
        if kind == "phase":
            phase = self.phases.setdefault(name, {"calls": 0, "wall_s": 0.0})
            phase["calls"] = int(phase["calls"]) + 1
            phase["wall_s"] = float(phase["wall_s"]) + elapsed
        return elapsed

    def add_phase(self, name: str, elapsed_s: float, calls: int = 1) -> None:
        phase = self.phases.setdefault(name, {"calls": 0, "wall_s": 0.0})
        phase["calls"] = int(phase["calls"]) + calls
        phase["wall_s"] = float(phase["wall_s"]) + max(0.0, elapsed_s)

    def censor_active(self, reason: str) -> None:
        now = time.monotonic()
        for token, (name, kind, started) in self.active.items():
            if token in self._censored_tokens:
                continue
            self.censored.append({
                "stage": name,
                "kind": kind,
                "elapsed_lower_bound_s": max(0.0, now - started),
                "reason": reason,
            })
            self._censored_tokens.add(token)

    def censor(self, stage: str, elapsed_s: float, reason: str,
               kind: str = "phase") -> None:
        self.censored.append({
            "stage": stage,
            "kind": kind,
            "elapsed_lower_bound_s": max(0.0, elapsed_s),
            "reason": reason,
        })

    def document(self, status: str, **fields: Any) -> dict[str, Any]:
        payload: dict[str, Any] = {
            "schema": "acacia-gr1-s0-diagnostics-v1",
            "schema_version": SCHEMA_VERSION,
            "tool": self.tool,
            "status": status,
            "elapsed_s": max(0.0, time.monotonic() - self.started),
            "phases": self.phases,
            "counters": dict(self.counters),
            "censored": self.censored,
            "diagnostic_errors": list(self.errors),
            "environment": {
                "interpreter": {
                    "executable": sys.executable,
                    "version": sys.version,
                }
            },
        }
        payload.update(self.extra)
        payload.update(fields)
        return payload

    def write(self, status: str, **fields: Any) -> dict[str, Any]:
        """Best-effort persistence which can never change the caller's result."""
        try:
            payload = self.document(status, **fields)
        except Exception as error:
            self.record_error("serialize_document", error)
            payload = self._minimal_document(status)
        try:
            atomic_json(self.output, payload)
        except Exception as error:
            self.record_error("write", error)
            # A non-serializable optional field may have caused the first
            # failure.  Preserve a small error-bearing document when the path
            # itself is usable; a path failure is contained by this retry too.
            payload = self._minimal_document(status)
            try:
                atomic_json(self.output, payload)
            except Exception as retry_error:
                self.record_error("write_fallback", retry_error)
        return payload

    def _minimal_document(self, status: str) -> dict[str, Any]:
        return {
            "schema": "acacia-gr1-s0-diagnostics-v1",
            "schema_version": SCHEMA_VERSION,
            "tool": self.tool,
            "status": status,
            "elapsed_s": max(0.0, time.monotonic() - self.started),
            "phases": {},
            "counters": {},
            "censored": [],
            "diagnostic_errors": list(self.errors),
            "environment": {
                "interpreter": {
                    "executable": sys.executable,
                    "version": sys.version,
                }
            },
        }
