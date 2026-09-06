#!/usr/bin/env python3
"""Reconstruct and verify the flat SYNTCOMP TLSF release corpus.

The committed manifest makes the sparse upstream benchmark checkout an exact,
auditable replacement for the much larger release archive.  For example:

    python3 benchmarking/syntcomp-corpus.py init
    python3 benchmarking/syntcomp-corpus.py materialize --out /tmp/syntcomp-tlsf
    python3 benchmarking/syntcomp-corpus.py write-manifest \
        --reference selection-ltl-2025v2/selection-ltl-2025
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
import pathlib
import re
import subprocess
import sys
from typing import NamedTuple


ROOT = pathlib.Path(__file__).resolve().parents[1]
SUBMODULE = ROOT / "tests" / "syntcomp-benchmarks"
SUBMODULE_PATH = pathlib.PurePosixPath("tests/syntcomp-benchmarks")
MANIFEST = ROOT / "tests" / "suites" / "benchmarks" / "tlsf-manifest.tsv"
MANIFEST_HEADER = "instance\torigin\tsha256"
TAG_COLUMNS = {"status", "refsize"}
SHA256_RE = re.compile(r"[0-9a-f]{64}")
FLAT_ALIAS_RE = re.compile(r"(.+)_pb_([^_]+)_pe_\.tlsf")


class CorpusError(Exception):
    """An actionable corpus reconstruction error."""


class ManifestEntry(NamedTuple):
    instance: str
    origin: str
    sha256: str


class Source(NamedTuple):
    name: str
    origin: str
    data: bytes


class Candidate(NamedTuple):
    priority: int
    kind: str
    source: Source


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def run_git(arguments: list[str], cwd: pathlib.Path = ROOT) -> str:
    result = subprocess.run(
        ["git", *arguments],
        cwd=cwd,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        diagnostic = result.stderr.strip() or result.stdout.strip()
        command = " ".join(["git", *arguments])
        raise CorpusError(f"{command} failed: {diagnostic}")
    return result.stdout.strip()


def pinned_commit() -> str:
    output = run_git(["ls-files", "--stage", "--", str(SUBMODULE_PATH)])
    rows = [line for line in output.splitlines() if line]
    if len(rows) != 1:
        raise CorpusError(f"cannot determine the index pin for {SUBMODULE_PATH}")
    metadata, indexed_path = rows[0].split("\t", 1)
    mode, commit, stage = metadata.split()
    if mode != "160000" or stage != "0" or indexed_path != str(SUBMODULE_PATH):
        raise CorpusError(f"invalid submodule index entry: {rows[0]}")
    return commit


def submodule_url() -> str:
    return run_git(
        [
            "config",
            "--file",
            str(ROOT / ".gitmodules"),
            "--get",
            f"submodule.{SUBMODULE_PATH}.url",
        ]
    )


def is_git_checkout(path: pathlib.Path) -> bool:
    if not (path / ".git").exists():
        return False
    result = subprocess.run(
        ["git", "-C", str(path), "rev-parse", "--git-dir"],
        capture_output=True,
        text=True,
    )
    return result.returncode == 0


def git_value(path: pathlib.Path, arguments: list[str]) -> str:
    return run_git(["-C", str(path), *arguments])


def verify_submodule() -> str:
    if not is_git_checkout(SUBMODULE):
        raise CorpusError(f"{SUBMODULE}: submodule is not populated; run init")

    expected = pinned_commit()
    actual = git_value(SUBMODULE, ["rev-parse", "HEAD"])
    problems: list[str] = []
    if actual != expected:
        problems.append(f"HEAD is {actual}, expected index pin {expected}")
    if git_value(SUBMODULE, ["rev-parse", "--is-shallow-repository"]) != "true":
        problems.append("checkout is not shallow")
    if git_value(SUBMODULE, ["config", "--bool", "--get", "core.sparseCheckout"]) != "true":
        problems.append("core.sparseCheckout is not enabled")
    if (
        git_value(SUBMODULE, ["config", "--bool", "--get", "core.sparseCheckoutCone"])
        != "true"
    ):
        problems.append("core.sparseCheckoutCone is not enabled")
    sparse_paths = git_value(SUBMODULE, ["sparse-checkout", "list"]).splitlines()
    if sparse_paths != ["tlsf"]:
        problems.append(f"sparse paths are {sparse_paths!r}, expected ['tlsf']")
    if git_value(SUBMODULE, ["config", "--bool", "--get", "remote.origin.promisor"]) != "true":
        problems.append("origin is not configured as a partial-clone promisor")
    partial_filter = git_value(
        SUBMODULE, ["config", "--get", "remote.origin.partialclonefilter"]
    )
    if partial_filter != "blob:none":
        problems.append("origin partial-clone filter is not blob:none")
    if not (SUBMODULE / "tlsf").is_dir():
        problems.append("tlsf sparse checkout is missing")
    if problems:
        raise CorpusError(f"{SUBMODULE}: invalid checkout:\n  " + "\n  ".join(problems))
    return actual


def allocated_size(path: pathlib.Path) -> int:
    total = 0
    seen: set[tuple[int, int]] = set()
    for member in [path, *path.rglob("*")]:
        stat = member.lstat()
        identity = (stat.st_dev, stat.st_ino)
        if identity in seen:
            continue
        seen.add(identity)
        total += getattr(stat, "st_blocks", (stat.st_size + 511) // 512) * 512
    return total


def human_size(size: int) -> str:
    amount = float(size)
    for unit in ("B", "KiB", "MiB", "GiB", "TiB"):
        if amount < 1024.0 or unit == "TiB":
            return f"{amount:.1f} {unit}"
        amount /= 1024.0
    raise AssertionError("unreachable")


def init_submodule() -> None:
    if not is_git_checkout(SUBMODULE):
        if SUBMODULE.exists() and any(SUBMODULE.iterdir()):
            raise CorpusError(f"{SUBMODULE}: non-empty directory is not a Git checkout")
        run_git(
            [
                "clone",
                "--depth",
                "1",
                "--filter=blob:none",
                "--sparse",
                submodule_url(),
                str(SUBMODULE),
            ]
        )
        expected = pinned_commit()
        available = subprocess.run(
            ["git", "-C", str(SUBMODULE), "cat-file", "-e", f"{expected}^{{commit}}"],
            capture_output=True,
        )
        if available.returncode != 0:
            git_value(SUBMODULE, ["fetch", "--depth", "1", "origin", expected])
        git_value(SUBMODULE, ["checkout", "--detach", expected])
        git_value(SUBMODULE, ["sparse-checkout", "set", "tlsf"])

    commit = verify_submodule()
    size = human_size(allocated_size(SUBMODULE))
    print(f"{SUBMODULE}: {commit} verified; on-disk size {size}")


def parameter_rows(template: pathlib.Path) -> list[dict[str, str]]:
    csv_path = template.with_suffix(".csv")
    if not csv_path.is_file():
        raise CorpusError(f"{template}: missing sibling {csv_path.name}")
    with csv_path.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream)
        if not reader.fieldnames:
            raise CorpusError(f"{csv_path}: missing CSV header")
        rows: list[dict[str, str]] = []
        for line_number, row in enumerate(reader, 2):
            if None in row or any(value is None for value in row.values()):
                raise CorpusError(f"{csv_path}:{line_number}: malformed CSV row")
            rows.append(dict(row))
    return rows


def parameter_values(row: dict[str, str]) -> list[tuple[str, str]]:
    return [(name, value) for name, value in row.items() if name not in TAG_COLUMNS]


def expand_template(template: pathlib.Path, row: dict[str, str]) -> bytes:
    values = parameter_values(row)
    patterns = {
        name: re.compile(rf"^\s*{re.escape(name)}\s*=\s*(.*)\s*;.*$")
        for name, _ in values
    }
    output = io.StringIO()
    with template.open(encoding="utf-8") as stream:
        for line in stream:
            for name, value in values:
                if patterns[name].match(line):
                    line = f"{name} = {value};\n"
                    break
            output.write(line)
    output.write("//#!SYNTCOMP\n")
    if "status" in row:
        output.write(f"//STATUS : {row['status']}\n")
    if "refsize" in row:
        output.write(f"//REF_SIZE : {row['refsize']}\n")
    output.write("//#\n")
    return output.getvalue().encode("utf-8")


def parametric_sources(submodule: pathlib.Path) -> list[Source]:
    tlsf_root = submodule / "tlsf"
    sources: list[Source] = []
    templates = sorted(tlsf_root.glob("**/parametric/*.tlsf"))
    csv_paths = {
        path.with_suffix(".tlsf")
        for path in tlsf_root.glob("**/parametric/*.csv")
    }
    orphan_csv = sorted(csv_paths - set(templates))
    if orphan_csv:
        raise CorpusError(f"{orphan_csv[0].with_suffix('.csv')}: CSV has no template")
    for template in templates:
        relative = template.relative_to(submodule).as_posix()
        # Upstream pandas sorting changes generation order only, not bytes.  A
        # stdlib DictReader therefore has equivalent expansion semantics.
        for row in parameter_rows(template):
            values = parameter_values(row)
            suffix = "_".join(value for _, value in values)
            name = f"{template.stem}_pb_{suffix}_pe_.tlsf"
            parameters = ",".join(f"{key}={value}" for key, value in values)
            origin = f"param:{relative}:{parameters}"
            sources.append(Source(name, origin, expand_template(template, row)))
    return sources


def direct_sources(submodule: pathlib.Path) -> list[Source]:
    tlsf_root = submodule / "tlsf"
    sources: list[Source] = []
    for path in sorted(tlsf_root.rglob("*.tlsf")):
        if "parametric" in path.relative_to(tlsf_root).parts:
            continue
        relative = path.relative_to(submodule).as_posix()
        sources.append(Source(path.name, f"direct:{relative}", path.read_bytes()))
    return sources


def source_catalog(submodule: pathlib.Path) -> tuple[list[Source], dict[str, bytes]]:
    if not (submodule / "tlsf").is_dir():
        raise CorpusError(f"{submodule}: tlsf checkout is missing; run init")
    sources = direct_sources(submodule) + parametric_sources(submodule)
    origins: dict[str, bytes] = {}
    for source in sources:
        previous = origins.get(source.origin)
        if previous is not None and previous != source.data:
            raise CorpusError(f"{source.origin}: duplicate origin produces different bytes")
        origins[source.origin] = source.data
    return sources, origins


def flat_alias(name: str) -> str | None:
    match = FLAT_ALIAS_RE.fullmatch(name)
    if match is None:
        return None
    return f"{match.group(1)}{match.group(2)}.tlsf"


def load_manifest(path: pathlib.Path) -> list[ManifestEntry]:
    if not path.is_file():
        raise CorpusError(f"{path}: manifest is missing")
    with path.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        if reader.fieldnames != MANIFEST_HEADER.split("\t"):
            raise CorpusError(f"{path}: expected header {MANIFEST_HEADER!r}")
        entries: list[ManifestEntry] = []
        for line_number, row in enumerate(reader, 2):
            instance = row["instance"]
            origin = row["origin"]
            digest = row["sha256"]
            if pathlib.PurePath(instance).name != instance or not instance.endswith(".tlsf"):
                raise CorpusError(f"{path}:{line_number}: invalid instance {instance!r}")
            if not (origin.startswith("direct:") or origin.startswith("param:")):
                raise CorpusError(f"{path}:{line_number}: invalid origin {origin!r}")
            if SHA256_RE.fullmatch(digest) is None:
                raise CorpusError(f"{path}:{line_number}: invalid SHA-256 {digest!r}")
            entries.append(ManifestEntry(instance, origin, digest))
    names = [entry.instance for entry in entries]
    if not names:
        raise CorpusError(f"{path}: manifest has no entries")
    if names != sorted(names):
        raise CorpusError(f"{path}: instances are not sorted")
    if len(names) != len(set(names)):
        raise CorpusError(f"{path}: duplicate instance")
    return entries


def materialize(
    out: pathlib.Path,
    submodule: pathlib.Path = SUBMODULE,
    manifest: pathlib.Path = MANIFEST,
    *,
    no_record: bool = False,
) -> int:
    entries = load_manifest(manifest)
    manifest_digest = sha256(manifest.read_bytes())
    _, origins = source_catalog(submodule)
    out = out.expanduser().resolve()
    out.mkdir(parents=True, exist_ok=True)

    produced: set[str] = set()
    missing_origins: list[ManifestEntry] = []
    for entry in entries:
        data = origins.get(entry.origin)
        if data is None:
            missing_origins.append(entry)
            continue
        target = out / entry.instance
        if target.is_symlink():
            raise CorpusError(f"{target}: refusing to overwrite a symbolic link")
        target.write_bytes(data)
        produced.add(entry.instance)

    expected = {entry.instance: entry for entry in entries}
    failures: list[str] = []
    for entry in missing_origins:
        failures.append(f"origin unavailable: {entry.instance}: {entry.origin}")
    actual_files = sorted(out.rglob("*.tlsf"))
    actual_names = {path.relative_to(out).as_posix() for path in actual_files}
    for instance in sorted((expected.keys() - produced) | (expected.keys() - actual_names)):
        failures.append(f"not produced: {instance}")
    for path in actual_files:
        relative = path.relative_to(out).as_posix()
        entry = expected.get(relative)
        if entry is None:
            failures.append(f"unexpected file: {relative}")
            continue
        actual = sha256(path.read_bytes())
        if actual != entry.sha256:
            failures.append(
                f"hash mismatch: {relative}: expected {entry.sha256}, got {actual}"
            )
    if failures:
        raise CorpusError("materialization verification failed:\n  " + "\n  ".join(failures))

    (out / ".acacia-tlsf-corpus").write_text(
        json.dumps({"entries": len(entries), "manifest_sha256": manifest_digest}) + "\n",
        encoding="utf-8",
    )
    if not no_record:
        (ROOT / ".acacia-tlsf-corpus-path").write_text(str(out) + "\n", encoding="utf-8")

    print(f"materialized and verified {len(entries)} files in {out}")
    return len(entries)


def write_manifest(
    reference: pathlib.Path,
    submodule: pathlib.Path = SUBMODULE,
    manifest: pathlib.Path = MANIFEST,
) -> int:
    reference_files = sorted(reference.glob("*.tlsf"), key=lambda path: path.name)
    if not reference_files:
        raise CorpusError(f"{reference}: no .tlsf reference files")
    sources, _ = source_catalog(submodule)

    candidates: dict[tuple[str, str], list[Candidate]] = {}
    for source in sources:
        kind = "param" if source.origin.startswith("param:") else "direct"
        priority = 1 if kind == "param" else 0
        candidates.setdefault((source.name, sha256(source.data)), []).append(
            Candidate(priority, kind, source)
        )
        alias = flat_alias(source.name)
        if alias is not None:
            candidates.setdefault((alias, sha256(source.data)), []).append(
                Candidate(2, "alias", source)
            )

    entries: list[ManifestEntry] = []
    counts = {"direct": 0, "param": 0, "alias": 0}
    unaccounted: list[str] = []
    for path in reference_files:
        digest = sha256(path.read_bytes())
        matches = candidates.get((path.name, digest), [])
        if not matches:
            unaccounted.append(path.name)
            continue
        chosen = min(
            matches,
            key=lambda candidate: (candidate.priority, candidate.source.origin),
        )
        entries.append(ManifestEntry(path.name, chosen.source.origin, digest))
        counts[chosen.kind] += 1
    if unaccounted:
        listing = "\n  ".join(unaccounted)
        raise CorpusError(
            f"{len(unaccounted)} reference files cannot be accounted for:\n  {listing}"
        )

    manifest.parent.mkdir(parents=True, exist_ok=True)
    manifest.write_text(
        MANIFEST_HEADER
        + "\n"
        + "".join(
            f"{entry.instance}\t{entry.origin}\t{entry.sha256}\n" for entry in entries
        ),
        encoding="utf-8",
    )
    print(
        f"wrote {len(entries)} entries to {manifest}: "
        f"direct={counts['direct']} param={counts['param']} aliases={counts['alias']}"
    )
    return len(entries)


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser(
        "init",
        help="initialize or verify the sparse, shallow, blobless submodule",
    )
    materialize_parser = subparsers.add_parser(
        "materialize",
        help="reconstruct and verify the flat corpus",
    )
    materialize_parser.add_argument("--out", required=True, type=pathlib.Path)
    materialize_parser.add_argument(
        "--no-record", action="store_true", help="do not record the corpus path in the repository"
    )
    manifest_parser = subparsers.add_parser(
        "write-manifest",
        help="account for a release corpus and write its SHA-256 manifest",
    )
    manifest_parser.add_argument("--reference", required=True, type=pathlib.Path)
    args = parser.parse_args()

    if args.command == "init":
        init_submodule()
    elif args.command == "materialize":
        materialize(args.out, no_record=args.no_record)
    elif args.command == "write-manifest":
        write_manifest(args.reference)
    else:
        raise AssertionError(f"unhandled command {args.command}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (CorpusError, OSError) as error:
        sys.exit(str(error))
