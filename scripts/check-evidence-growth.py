#!/usr/bin/env python3
"""Reject newly reachable generated evidence blobs in a Git commit range."""

import argparse
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parent.parent
RAW_SUFFIXES = (".tsv", ".csv", ".raw", ".log", ".jsonl", ".tar", ".gz",
                ".zst", ".zip")
SOURCE_SUFFIXES = (".py", ".sh", ".bash", ".c", ".h", ".cc", ".hh", ".cpp",
                   ".hpp", ".cxx", ".hxx", ".rs", ".go", ".js", ".jsx",
                   ".ts", ".tsx", ".java", ".kt", ".swift", ".rb", ".pl",
                   ".lua", ".m", ".mm", ".php", ".r", ".jl", ".toml",
                   ".yaml", ".yml", ".cmake", ".mk", ".nix")
PRESENTATION_SUFFIXES = (".md", ".txt", ".json", ".png", ".pdf", ".svg")
INDIVIDUAL_LIMIT = 256 * 1024
PRESENTATION_LIMIT = 2 * 1024 * 1024


def git(*args, input_text=None):
    return subprocess.run(["git", *args], cwd=ROOT, input=input_text, text=True,
                          capture_output=True, check=True).stdout


def allowlisted_paths(path):
    lines = path.read_text().splitlines()
    if not lines or lines[0] != "path\treason":
        raise ValueError("invalid allowlist header")
    allowed = set()
    for line in lines[1:]:
        name, reason = line.split("\t", 1)
        if not name.startswith("benchmarking/") or not reason.strip():
            raise ValueError("allowlist needs a benchmarking path and reason")
        allowed.add(name)
    return allowed


def generated(path):
    return path.startswith("benchmarking/") and (
        path.startswith("benchmarking/plots/") or
        any(word in path.lower() for word in ("campaign", "evidence", "report", "summary",
                                              "proof", "thermal", "fixture")))


def raw(path):
    if not path.startswith("benchmarking/"):
        return False
    lower = path.lower()
    if lower.endswith(RAW_SUFFIXES):
        return True
    # A source or config file can contain evidence words in its name or parents.
    if lower.endswith(SOURCE_SUFFIXES):
        return False
    parts = lower.split("/")
    contract_json = parts[-1] in ("schema.json", "config.json", "configuration.json") or \
        parts[-1].endswith((".schema.json", ".config.json"))
    raw_json = lower.endswith(".json") and not contract_json and (
        any(part.startswith("raw-") for part in parts[1:-1]) or
        (any(part == "campaign" or part.startswith("campaign-") for part in parts[1:-1])
         and any("trace" in part or "diagnostics" in part for part in parts[1:])))
    return (raw_json or
            any(word in lower for word in ("thermal-sample", "proof-bundle")) or
            ("thermal" in lower and not lower.endswith(".md")) or
            (("/proof/" in lower or "/proofs/" in lower) and not lower.endswith(".md")))


def sizes(oids):
    if not oids:
        return {}
    result = git("cat-file", "--batch-check=%(objectname) %(objecttype) %(objectsize)",
                 input_text="\n".join(sorted(oids)) + "\n")
    return {oid: int(size) for oid, kind, size in (line.split() for line in result.splitlines())
            if kind == "blob"}


def default_range():
    for ref in ("refs/heads/master", "refs/remotes/origin/master"):
        if subprocess.run(["git", "rev-parse", "--verify", ref], cwd=ROOT,
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0:
            base = git("merge-base", ref, "HEAD").strip()
            return base, "HEAD"
    raise ValueError("no master ref; pass --range base..head")


def changed_blob_paths(commits):
    paths = {}
    for commit in commits:
        # --no-renames presents a rename as delete/add, preserving the new path.
        output = git("diff-tree", "-m", "--root", "--no-commit-id", "--no-renames",
                     "--raw", "-r", "-z", commit)
        fields_and_paths = output.split("\0")
        for metadata, path in zip(fields_and_paths[0::2], fields_and_paths[1::2]):
            if not metadata.startswith(":"):
                continue
            fields = metadata.split()
            if len(fields) < 5:
                continue
            oid = fields[3]
            if set(oid) != {"0"}:
                paths.setdefault(oid, set()).add(path)
    return paths


def baseline_inventory(base, allowlist):
    output = git("ls-tree", "-r", "-l", "-z", base, "--", "benchmarking")
    total = 0
    raw_count = 0
    large_count = 0
    for line in output.split("\0"):
        if "\t" not in line:
            continue
        metadata, path = line.split("\t", 1)
        fields = metadata.split()
        if len(fields) == 4 and fields[1] == "blob" and path not in allowlist:
            size = int(fields[3])
            raw_count += raw(path)
            large_count += generated(path) and size > INDIVIDUAL_LIMIT
            if generated(path) and path.lower().endswith(PRESENTATION_SUFFIXES):
                total += size
    return total, raw_count, large_count


def check(args):
    allowed = allowlisted_paths(Path(args.allowlist))
    if args.range:
        if args.range.count("..") != 1 or "..." in args.range:
            raise ValueError("range must be base..head")
        base, head = args.range.split("..")
        if not base or not head:
            raise ValueError("range must be base..head")
    else:
        base, head = default_range()
    base = git("rev-parse", "--verify", f"{base}^{{commit}}").strip()
    head = git("rev-parse", "--verify", f"{head}^{{commit}}").strip()
    commits = git("rev-list", "--reverse", f"{base}..{head}").splitlines()
    paths = changed_blob_paths(commits)
    blob_sizes = sizes(paths)
    baseline_bytes, baseline_raw, baseline_large = baseline_inventory(base, allowed)
    violations = []
    presentation = {}
    for oid, names in sorted(paths.items()):
        if oid not in blob_sizes:
            continue
        size = blob_sizes[oid]
        for name in sorted(names):
            if name in allowed or not name.startswith("benchmarking/"):
                continue
            if raw(name):
                violations.append(f"raw evidence: {name} ({size} bytes)")
            elif generated(name) and size > INDIVIDUAL_LIMIT:
                violations.append(f"generated blob over 256 KiB: {name} ({size} bytes)")
            if generated(name) and name.lower().endswith(PRESENTATION_SUFFIXES):
                presentation[oid] = size
    added_bytes = sum(presentation.values())
    if added_bytes > PRESENTATION_LIMIT:
        violations.append(f"new generated presentation exceeds 2 MiB: {added_bytes} bytes")
    print(f"baseline generated presentation: {baseline_bytes} bytes (reported, not gated)")
    print(f"baseline raw paths: {baseline_raw}; generated paths over 256 KiB: {baseline_large}")
    print(f"new generated presentation: {added_bytes} bytes across {len(presentation)} blobs")
    print(f"changed blob OIDs examined: {len(blob_sizes)}")
    for violation in violations:
        print(f"FAIL {violation}")
    if violations:
        raise ValueError(f"{len(violations)} new evidence growth violation(s)")
    print("PASS no new evidence growth violations")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--range", help="commit range base..head; defaults to merge-base with master")
    parser.add_argument("--allowlist", default=str(ROOT / "benchmarking/evidence-growth-allowlist.tsv"))
    args = parser.parse_args()
    try:
        check(args)
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        parser.exit(1, f"error: {exc}\n")


if __name__ == "__main__":
    main()
