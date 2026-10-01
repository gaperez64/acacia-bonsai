#!/usr/bin/env python3
"""Pack a closed P4 campaign and print its publication and index commands."""

from __future__ import annotations

import argparse
import hashlib
import pathlib
import re
import shlex
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
EVIDENCE = ROOT / "scripts/acacia-evidence.py"
SAFE = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]*\Z")
REPO = re.compile(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+\Z")
HEX = re.compile(r"[0-9a-f]{64}\Z")


def digest_file(path):
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
            size += len(block)
    return digest.hexdigest(), size


def prepare(args, run=subprocess.run):
    if not SAFE.fullmatch(args.campaign) or not SAFE.fullmatch(args.release_tag):
        raise ValueError("campaign and release tag must be safe names")
    if not REPO.fullmatch(args.repo):
        raise ValueError("--repo must be OWNER/REPOSITORY")
    if not args.source_revision or not args.binary_sha256:
        raise ValueError("source revision and binary SHA-256 are required")
    if any(not HEX.fullmatch(value) for value in args.binary_sha256):
        raise ValueError("invalid binary SHA-256")
    if not args.supports.strip():
        raise ValueError("--supports is required")
    members = pathlib.Path(args.members)
    pack = [sys.executable, "-s", str(EVIDENCE), "pack", "--campaign", args.campaign,
            "--status", "closed", "--members", f"@{members}", "--out", str(args.out)]
    for revision in args.source_revision:
        pack += ["--source-revision", revision]
    for sha in args.binary_sha256:
        pack += ["--binary-sha256", sha]
    run(pack, check=True, cwd=ROOT)
    archive = args.out / f"{args.campaign}.tar.gz"
    sha, size = digest_file(archive)
    run([sys.executable, "-s", str(EVIDENCE), "verify", str(archive),
         "--sha256", sha, "--max-bytes", str(size)], check=True, cwd=ROOT)
    url = f"https://github.com/{args.repo}/releases/download/{args.release_tag}/{archive.name}"
    upload = ["gh", "release", "upload", args.release_tag, str(archive), "--repo", args.repo]
    index = [sys.executable, "-s", str(EVIDENCE), "index-add", "--campaign", args.campaign,
             "--status", "closed", "--asset-name", archive.name, "--url", url,
             "--sha256", sha, "--bytes", str(size), "--supports", args.supports,
             "--source-revisions", ",".join(sorted(set(args.source_revision))),
             "--binary-sha256s", ",".join(sorted(set(args.binary_sha256)))]
    return archive, sha, size, upload, index


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--campaign", required=True)
    parser.add_argument("--members", required=True, type=pathlib.Path,
                        help="newline list of repository-relative evidence paths")
    parser.add_argument("--out", required=True, type=pathlib.Path)
    parser.add_argument("--repo", required=True, help="OWNER/REPOSITORY")
    parser.add_argument("--release-tag", required=True)
    parser.add_argument("--supports", required=True, help="conclusion for the evidence index")
    parser.add_argument("--source-revision", action="append", default=[])
    parser.add_argument("--binary-sha256", action="append", default=[])
    args = parser.parse_args(argv)
    try:
        archive, sha, size, upload, index = prepare(args)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.error(str(error))
    print(f"Verified archive: {archive} ({size} bytes; SHA-256 {sha})")
    print("Upload to the existing evidence release, then add its index row:")
    print(shlex.join(upload))
    print(shlex.join(index))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
