#!/usr/bin/env python3
"""Check matched runtime routes with existing generic fixtures and attribution."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import tempfile

def module(name: str, file: str):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(file))
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


attribution = module("attribution", "check-attribution.py")
fixtures = module("native_fixtures", "check-native-gr1-lift-cli.py")


def check(binary: Path, args: list[str], directory: Path, r: bool, eq: bool) -> list[dict]:
    directory.mkdir()
    recorded = attribution.execute(binary, args, directory)
    plain = attribution.execute(binary, args, None)
    assert recorded.returncode == plain.returncode == 0, (recorded, plain)
    assert recorded.stdout == plain.stdout, (recorded, plain)
    records = attribution.rows(directory)
    attribution.complete_records(records)
    events = [row for row in records if "requested_backend" in row or row.get("event") == "worker_spec"]
    assert events and all(row["r_prepass"] == r and row["equivariance"] == eq
                          for row in events), events
    return [row for row in events if row["event"] == "route_start"]


def main() -> None:
    binary, build = map(lambda p: Path(p).resolve(), sys.argv[1:3])
    with tempfile.TemporaryDirectory(dir=build) as temporary:
        root = Path(temporary)
        source = root / "repeated.tlsf"
        source.write_text(fixtures.REAL)
        native = ["-T", str(source), "--arms", "both:gr1-real-lift:oxidd"]
        backward = ["-f", "G (i <-> X o)", "-i", "i", "-o", "o", "--spot-fast", "off",
                    "--arms", "real:small:backward"]
        for leg, r, eq in (("A", False, False), ("B", False, True),
                           ("C", True, False), ("D", True, True)):
            switches = ["--r-prepass", "on" if r else "off",
                        "--equivariance", "on" if eq else "off"]
            routes = check(binary, native + switches, root / f"native-{leg}", r, eq)
            names = [row["route"] for row in routes]
            assert "U" not in names, routes
            assert ("seed_discovery" in names) == r, routes
            assert ("R" in names) == r, routes
            assert ("direct" in names) == (not r), routes
            if not r:
                records = attribution.rows(root / f"native-{leg}")
                for phase in ("seed_cache", "seed_none"):
                    counts = [row["work_count"] for row in records if row.get("phase") == phase]
                    assert counts == [0], (phase, counts)
            routes = check(binary, backward + switches, root / f"backward-{leg}", r, eq)
            names = [row["route"] for row in routes]
            assert ("equivariance" in names) == eq, routes
            assert "backward" in names, routes
            assert all(row["effective_backend"] == "backward" for row in routes), routes
        # Omitted switches must select the exact explicit incumbent routes.
        for name, args in (("native", native), ("backward", backward)):
            default = check(binary, args, root / f"{name}-default", True, True)
            explicit = attribution.rows(root / f"{name}-D")
            explicit = [row for row in explicit if row.get("event") == "route_start"]
            assert [(row["route"], row["effective_backend"]) for row in default] == [
                (row["route"], row["effective_backend"]) for row in explicit]
        for option in ("--r-prepass", "--equivariance"):
            result = attribution.execute(binary, backward + [option, "invalid"], None)
            assert result.returncode == 3 and "expects on or off" in result.stderr, result
        # Forward and synthesis retain their existing exclusions.
        for switches in (["--equivariance", "off"], ["--equivariance", "on"]):
            forward = backward[:-1] + ["real:small:forward"] + switches
            routes = check(binary, forward, root / f"forward-{switches[-1]}", True,
                           switches[-1] == "on")
            assert not any(row["route"] == "equivariance" for row in routes), routes
            synthesis = backward + switches + ["-s", str(root / f"controller-{switches[-1]}.aag")]
            routes = check(binary, synthesis, root / f"synthesis-{switches[-1]}", True,
                           switches[-1] == "on")
            assert not any(row["route"] == "equivariance" for row in routes), routes
            assert any(row["route"] == "backward" for row in routes), routes
    print("P1 matched runtime ablation routes: PASS")


if __name__ == "__main__":
    main()
