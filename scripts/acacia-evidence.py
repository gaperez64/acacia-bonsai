#!/usr/bin/env python3
"""Pack, verify, retrieve, and index immutable campaign evidence."""

import argparse
import csv
import fcntl
import fnmatch
import gzip
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import tarfile
import urllib.parse
import urllib.request
from datetime import datetime, timezone


ROOT = Path(__file__).resolve().parent.parent
INDEX_COLUMNS = ("campaign_id", "status", "asset_name", "url", "sha256", "bytes",
                 "supports", "source_revisions", "binary_sha256s")
STATUS = ("closed", "stopped", "partial")
HEX64 = re.compile(r"[0-9a-f]{64}\Z")
SAFE_ID = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]*\Z")
TOOL_VERSION = "1"
MAX_ARCHIVE_MEMBER = 16 * 1024 * 1024 * 1024
MAX_METADATA = 16 * 1024 * 1024
ORIGINAL_ID = re.compile(r"\boriginal[ _-]*id\b", re.IGNORECASE)
OBFUSCATED_ID = re.compile(r"\bobfuscated[ _-]*id\b", re.IGNORECASE)
ORIGINAL_KEY = re.compile(r'"original[ _-]*id"\s*:', re.IGNORECASE)
OBFUSCATED_KEY = re.compile(r'"obfuscated[ _-]*id"\s*:', re.IGNORECASE)


def fail(message):
    raise ValueError(message)


def digest_file(path):
    h = hashlib.sha256()
    size = 0
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
            size += len(chunk)
    return h.hexdigest(), size


def safe_name(name):
    if (not name or "\\" in name or any(ord(char) < 32 for char in name)
            or name.startswith("/")):
        fail(f"unsafe archive path: {name!r}")
    parts = PurePosixPath(name).parts
    if any(part in (".", "..", "") for part in name.split("/")) or parts[0] == "..":
        fail(f"unsafe archive path: {name!r}")
    if name.endswith("/") or ":" in parts[0]:
        fail(f"unsafe archive path: {name!r}")
    return name


def has_mapping_columns(path):
    original_key = False
    obfuscated_key = False
    with path.open("r", encoding="utf-8", errors="replace") as stream:
        while line := stream.readline(64 * 1024):
            if ORIGINAL_ID.search(line) and OBFUSCATED_ID.search(line):
                return True
            original_key |= bool(ORIGINAL_KEY.search(line))
            obfuscated_key |= bool(OBFUSCATED_KEY.search(line))
            if original_key and obfuscated_key:
                return True
    return False


def checked_source(name, patterns):
    safe_name(name)
    if name in ("MANIFEST.tsv", "CAMPAIGN.json"):
        fail(f"reserved member name: {name}")
    if any(fnmatch.fnmatchcase(name.lower(), pattern) or
           fnmatch.fnmatchcase(PurePosixPath(name).name.lower(), pattern)
           for pattern in patterns):
        fail(f"do-not-publish pattern matches: {name}")
    path = ROOT / name
    current = ROOT
    for part in PurePosixPath(name).parts:
        current /= part
        if current.is_symlink():
            fail(f"symlink in member path: {name}")
    if not path.is_file() or not path.resolve().is_relative_to(ROOT):
        fail(f"member must be a regular repository file: {name}")
    if has_mapping_columns(path):
        fail(f"do-not-publish obfuscation mapping content: {name}")
    return path


def load_patterns():
    path = ROOT / "benchmarking/evidence-do-not-publish.txt"
    return [line.strip().lower() for line in path.read_text().splitlines()
            if line.strip() and not line.startswith("#")]


def tar_entry(name, size):
    item = tarfile.TarInfo(name)
    item.size = size
    item.mtime = 0
    item.uid = item.gid = 0
    item.uname = item.gname = ""
    item.mode = 0o644
    return item


def json_bytes(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True) + "\n").encode()


def member_names(spec):
    # A newline list can be supplied with @file; otherwise comma-separated paths.
    if spec.startswith("@"):
        return [line.strip() for line in Path(spec[1:]).read_text().splitlines() if line.strip()]
    return [part.strip() for part in spec.split(",") if part.strip()]


def pack(args):
    if not SAFE_ID.fullmatch(args.campaign) or args.campaign in (".", ".."):
        fail("campaign ID must be a safe filename")
    names = member_names(args.members)
    if not names or len(names) != len(set(names)):
        fail("members must be nonempty and unique")
    patterns = load_patterns()
    members = [(name, checked_source(name, patterns)) for name in sorted(names)]
    records = []
    for name, path in members:
        sha, size = digest_file(path)
        if size > MAX_ARCHIVE_MEMBER:
            fail(f"member exceeds size bound: {name}")
        records.append((name, size, sha))
    if sum(size for _, size, _ in records) > MAX_ARCHIVE_MEMBER:
        fail("campaign exceeds declared size bound")
    revisions = args.source_revision or [subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()]
    for sha in args.binary_sha256:
        if not HEX64.fullmatch(sha):
            fail(f"invalid binary SHA-256: {sha}")
    epoch = int(os.environ.get("SOURCE_DATE_EPOCH", "0"))
    created = datetime.fromtimestamp(epoch, timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    campaign = json_bytes({"id": args.campaign, "status": args.status,
                           "source_revisions": sorted(set(revisions)),
                           "binary_sha256s": sorted(set(args.binary_sha256)),
                           "created_utc": created, "tool_version": TOOL_VERSION,
                           "declared_bytes": sum(size for _, size, _ in records)})
    campaign_record = ("CAMPAIGN.json", len(campaign), hashlib.sha256(campaign).hexdigest())
    manifest = b"path\tbytes\tsha256\n" + b"".join(
        f"{name}\t{size}\t{sha}\n".encode() for name, size, sha in
        sorted([*records, campaign_record]))
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    archive = out / f"{args.campaign}.tar.gz"
    if archive.exists():
        fail(f"archive already exists: {archive}")
    try:
        with archive.open("xb") as raw, gzip.GzipFile(fileobj=raw, mode="wb", mtime=0,
                                                        filename="") as zipped:
            with tarfile.open(fileobj=zipped, mode="w", format=tarfile.USTAR_FORMAT) as tar:
                tar.addfile(tar_entry("CAMPAIGN.json", len(campaign)), io.BytesIO(campaign))
                tar.addfile(tar_entry("MANIFEST.tsv", len(manifest)), io.BytesIO(manifest))
                for (name, path), (_, size, sha) in zip(members, records):
                    # Refuse races which change a source after its manifest was prepared.
                    if digest_file(path) != (sha, size):
                        fail(f"member changed while packing: {name}")
                    with path.open("rb") as stream:
                        tar.addfile(tar_entry(name, size), stream)
                    if digest_file(path) != (sha, size):
                        fail(f"member changed while packing: {name}")
        sha, size = digest_file(archive)
        print(f"{sha}\t{size}\t{archive}")
    except BaseException:
        archive.unlink(missing_ok=True)
        raise


def parse_manifest(data):
    try:
        lines = data.decode("utf-8").splitlines()
        if not lines or lines[0] != "path\tbytes\tsha256":
            fail("invalid manifest header")
        rows = {}
        for line in lines[1:]:
            name, size_text, sha = line.split("\t")
            safe_name(name)
            if name in rows or name == "MANIFEST.tsv":
                fail("duplicate or reserved manifest path")
            size = int(size_text)
            if size < 0 or size > MAX_ARCHIVE_MEMBER or not HEX64.fullmatch(sha):
                fail("invalid manifest entry")
            rows[name] = (size, sha)
        return rows
    except (UnicodeError, ValueError) as exc:
        fail(f"invalid manifest: {exc}")


def verify_archive(path, expected_sha=None, max_bytes=None, extract_to=None):
    outer_sha, archive_bytes = digest_file(path)
    if expected_sha and outer_sha != expected_sha:
        fail("outer SHA-256 mismatch")
    if max_bytes is not None and archive_bytes > max_bytes:
        fail("archive exceeds index bytes bound")
    campaign = None
    campaign_digest = None
    campaign_size = None
    manifest = None
    found = set()
    total = 0
    with tarfile.open(path, mode="r|gz") as tar:
        for item in tar:
            safe_name(item.name)
            if not item.isfile() or item.name in found:
                fail(f"link, device, directory, or duplicate member: {item.name}")
            found.add(item.name)
            if item.size < 0 or item.size > MAX_ARCHIVE_MEMBER:
                fail("member exceeds size bound")
            if campaign is None and item.name != "CAMPAIGN.json":
                fail("CAMPAIGN.json must be first")
            if campaign is not None and manifest is None and item.name != "MANIFEST.tsv":
                fail("MANIFEST.tsv must be second")
            if item.name in ("CAMPAIGN.json", "MANIFEST.tsv") and item.size > MAX_METADATA:
                fail("metadata exceeds size bound")
            stream = tar.extractfile(item)
            if stream is None:
                fail("unreadable member")
            if item.name == "CAMPAIGN.json":
                data = stream.read(item.size)
                campaign = json.loads(data)
                if not isinstance(campaign, dict):
                    fail("invalid campaign metadata")
                campaign_digest = hashlib.sha256(data).hexdigest()
                campaign_size = item.size
                declared = campaign.get("declared_bytes")
                if not isinstance(declared, int) or declared < 0 or declared > MAX_ARCHIVE_MEMBER:
                    fail("invalid declared bytes bound")
                if campaign.get("status") not in STATUS or not isinstance(campaign.get("id"), str):
                    fail("invalid campaign metadata")
                if extract_to is not None:
                    with (extract_to / item.name).open("xb") as output:
                        output.write(data)
            elif item.name == "MANIFEST.tsv":
                data = stream.read(item.size)
                manifest = parse_manifest(data)
                if manifest.get("CAMPAIGN.json") != (campaign_size, campaign_digest):
                    fail("campaign metadata differs from manifest")
                if sum(size for name, (size, _) in manifest.items()
                       if name != "CAMPAIGN.json") != declared:
                    fail("manifest differs from declared bytes bound")
                if extract_to is not None:
                    with (extract_to / item.name).open("xb") as output:
                        output.write(data)
            else:
                if item.name not in manifest or item.size != manifest[item.name][0]:
                    fail(f"member absent from manifest or wrong size: {item.name}")
                total += item.size
                if total > declared:
                    fail("declared extraction size exceeded")
                h = hashlib.sha256()
                output = None
                if extract_to is not None:
                    target = extract_to / item.name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    output = target.open("xb")
                try:
                    remaining = item.size
                    while remaining:
                        chunk = stream.read(min(1024 * 1024, remaining))
                        if not chunk:
                            fail(f"truncated member: {item.name}")
                        h.update(chunk)
                        if output:
                            output.write(chunk)
                        remaining -= len(chunk)
                finally:
                    if output:
                        output.close()
                if h.hexdigest() != manifest[item.name][1]:
                    fail(f"member SHA-256 mismatch: {item.name}")
    if campaign is None or manifest is None or found != set(manifest) | {"MANIFEST.tsv"}:
        fail("archive member set differs from manifest")
    print(f"{outer_sha}\t{archive_bytes}\t{len(manifest)} members verified")
    return campaign


def read_index(path):
    with path.open(newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        if tuple(reader.fieldnames or ()) != INDEX_COLUMNS:
            fail("invalid evidence index header")
        rows = list(reader)
    if any(None in row or None in row.values() for row in rows):
        fail("invalid evidence index row")
    if len({row["campaign_id"] for row in rows}) != len(rows):
        fail("duplicate campaign ID in index")
    return rows


def canonical_list(values, *, binary=False, required=False):
    items = [item.strip() for value in values for item in value.split(",")]
    if any(not item or "\n" in item or "\r" in item or "\t" in item for item in items):
        if any(value for value in values):
            fail("invalid comma-separated provenance list")
        items = []
    if required and not items:
        fail("source revisions must be nonempty")
    if binary and any(not HEX64.fullmatch(item) for item in items):
        fail("invalid binary SHA-256 list")
    return ",".join(sorted(set(items)))


def index_add(args):
    path = Path(args.index)
    revisions = canonical_list([args.source_revisions], required=True)
    binary_hashes = canonical_list([args.binary_sha256s], binary=True)
    values = (args.campaign, args.status, args.asset_name, args.url, args.sha256,
              str(args.bytes), args.supports, revisions, binary_hashes)
    if any("\n" in value or "\r" in value or "\t" in value for value in values):
        fail("index fields must be one-line TSV values")
    if not HEX64.fullmatch(args.sha256) or args.bytes < 0:
        fail("invalid archive digest or size")
    safe_name(args.asset_name)
    if "/" in args.asset_name or not args.asset_name.endswith(".tar.gz"):
        fail("asset name must be a .tar.gz filename")
    if not args.supports.strip():
        fail("supports and source revisions must be nonempty")
    if urllib.parse.urlparse(args.url).scheme not in ("https", "http", "file"):
        fail("URL must use https, http, or file")
    lock_path = path.with_name(path.name + ".lock")
    with lock_path.open("a+") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        rows = read_index(path)
        if not SAFE_ID.fullmatch(args.campaign) or args.campaign in (".", ".."):
            fail("invalid campaign ID")
        if any(row["campaign_id"] == args.campaign for row in rows):
            fail("campaign ID already exists; amendments require a new ID")
        if args.amends:
            if not any(row["campaign_id"] == args.amends for row in rows):
                fail("amended campaign ID is absent")
            values = (*values[:6], values[6] + f"; amends {args.amends}", *values[7:])
        with path.open("a", newline="") as stream:
            csv.writer(stream, delimiter="\t", lineterminator="\n").writerow(values)


def fetch(args):
    rows = read_index(Path(args.index))
    matches = [row for row in rows if row["campaign_id"] == args.campaign]
    if len(matches) != 1:
        fail("campaign ID not found in index")
    row = matches[0]
    bound = int(row["bytes"])
    if bound < 0 or not HEX64.fullmatch(row["sha256"]):
        fail("invalid index digest or bytes bound")
    dest = Path(args.dest)
    if dest.is_symlink() or (dest.exists() and (not dest.is_dir() or any(dest.iterdir()))):
        fail("destination must be a new or empty directory")
    dest.mkdir(parents=True, exist_ok=True)
    archive = dest.parent / f".{args.campaign}.download"
    if archive.exists():
        fail("download staging path already exists")
    try:
        if args.from_file:
            source = Path(args.from_file).open("rb")
        else:
            parsed = urllib.parse.urlparse(row["url"])
            if parsed.scheme not in ("https", "http", "file"):
                fail("index URL scheme is not supported")
            source = urllib.request.urlopen(row["url"], timeout=30)  # noqa: S310
        with source, archive.open("xb") as output:
            size = 0
            while True:
                chunk = source.read(1024 * 1024)
                if not chunk:
                    break
                size += len(chunk)
                if size > bound:
                    fail("download exceeds index bytes bound")
                output.write(chunk)
        metadata = verify_archive(archive, row["sha256"], bound)
        if metadata["id"] != args.campaign or metadata["status"] != row["status"]:
            fail("archive campaign identity or status differs from index")
        if (not isinstance(metadata.get("source_revisions"), list) or
                not isinstance(metadata.get("binary_sha256s"), list) or
                any(not isinstance(value, str) for value in metadata["source_revisions"] +
                    metadata["binary_sha256s"])):
            fail("invalid archive provenance lists")
        if (canonical_list(metadata["source_revisions"], required=True) !=
                canonical_list([row["source_revisions"]], required=True) or
                canonical_list(metadata["binary_sha256s"], binary=True) !=
                canonical_list([row["binary_sha256s"]], binary=True)):
            fail("archive provenance differs from index")
        verify_archive(archive, row["sha256"], bound, extract_to=dest)
    finally:
        archive.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("pack")
    p.add_argument("--campaign", required=True)
    p.add_argument("--status", required=True, choices=STATUS)
    p.add_argument("--members", required=True, help="comma-separated repo paths or @newline-list")
    p.add_argument("--out", required=True)
    p.add_argument("--source-revision", action="append", default=[])
    p.add_argument("--binary-sha256", action="append", default=[])
    p.set_defaults(func=pack)
    p = sub.add_parser("verify")
    p.add_argument("archive", type=Path)
    p.add_argument("--sha256")
    p.add_argument("--max-bytes", type=int)
    p.set_defaults(func=lambda a: verify_archive(a.archive, a.sha256, a.max_bytes))
    p = sub.add_parser("fetch")
    p.add_argument("--index", default=str(ROOT / "benchmarking/evidence-index.tsv"))
    p.add_argument("--campaign", required=True)
    p.add_argument("--dest", required=True)
    p.add_argument("--from-file")
    p.set_defaults(func=fetch)
    p = sub.add_parser("index-add")
    p.add_argument("--index", default=str(ROOT / "benchmarking/evidence-index.tsv"))
    p.add_argument("--campaign", required=True)
    p.add_argument("--status", required=True, choices=STATUS)
    p.add_argument("--asset-name", required=True)
    p.add_argument("--url", required=True)
    p.add_argument("--sha256", required=True)
    p.add_argument("--bytes", required=True, type=int)
    p.add_argument("--supports", required=True)
    p.add_argument("--source-revisions", required=True)
    p.add_argument("--binary-sha256s", required=True)
    p.add_argument("--amends")
    p.set_defaults(func=index_add)
    args = parser.parse_args()
    try:
        args.func(args)
    except (ValueError, OSError, tarfile.TarError, json.JSONDecodeError) as exc:
        parser.exit(1, f"error: {exc}\n")


if __name__ == "__main__":
    main()
