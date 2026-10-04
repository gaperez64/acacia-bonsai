#!/usr/bin/env python3
"""Verify sealed corpus; fetch inputs with scripts/acacia-evidence.py fetch --campaign ID --dest DIR."""
from __future__ import annotations
import argparse
import csv
import hashlib
import pathlib


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus", required=True, type=pathlib.Path)
    parser.add_argument("--manifest", required=True, type=pathlib.Path)
    parser.add_argument("--list", required=True, type=pathlib.Path)
    parser.add_argument("--tlsf-map", required=True, type=pathlib.Path)
    args = parser.parse_args()
    try:
        expected = {}
        for line in args.manifest.read_text().splitlines():
            digest, sep, name = line.partition("  ")
            if not sep or len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest) or pathlib.Path(name).name != name or name in expected:
                raise ValueError(f"invalid manifest row {line!r}")
            expected[name] = digest
        ids = [s for line in args.list.read_text().splitlines() if (s := line.strip()) and not s.startswith("#")]
        with args.tlsf_map.open(newline="") as stream:
            rows = list(csv.DictReader(stream, delimiter="\t"))
        mapping = {row["instance"]: row["tlsf"] for row in rows}
        if len(expected) != 1524 or len(ids) != 1524 or len(set(ids)) != 1524 or len(rows) != 1524 or len(mapping) != 1524 or set(mapping) != set(ids) or set(mapping.values()) != set(expected):
            raise ValueError("manifest/list/map must form one-to-one 1,524-file sealed corpus")
        actual = {p.name for p in args.corpus.iterdir()}
        if actual != set(expected):
            raise ValueError(f"corpus differs from manifest: missing={len(set(expected)-actual)}, extra={len(actual-set(expected))}")
        bad = [name for name, digest in expected.items()
               if hashlib.sha256((args.corpus / name).read_bytes()).hexdigest() != digest]
        if bad:
            raise ValueError(f"{len(bad)} SHA-256 mismatches: {', '.join(bad[:5])}")
        print(f"verified {len(expected)} sealed TLSF files; manifest SHA-256 {hashlib.sha256(args.manifest.read_bytes()).hexdigest()}")
    except (OSError, ValueError, KeyError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
