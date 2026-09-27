#!/usr/bin/env python3
"""Create and serially verify the sealed, seeded full TLSF corpus."""

from __future__ import annotations

import argparse
import csv
import hashlib
import importlib.util
import json
import pathlib

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]


def load(name: str):
    path = HERE.parent / name
    spec = importlib.util.spec_from_file_location(name.replace("-", "_"), path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seed", type=int, default=20260924)
    parser.add_argument("--corpus", type=pathlib.Path, default=ROOT / "tlsf-corpus-obf")
    parser.add_argument("--build", type=pathlib.Path,
                        default=pathlib.Path("/home/gperez/GIT-repos/tlsf-tools/build-SB-8b158d7"))
    parser.add_argument("--timeout", type=float, default=20)
    args = parser.parse_args()
    obfuscator = load("obfuscate-tlsf.py")
    verifier = load("verify-obfuscation.py")
    sources = verifier.sources()
    if len(sources) != 1524:
        raise ValueError("expected 1,524 original TLSF sources")
    args.corpus.mkdir(parents=True, exist_ok=True)
    if any(args.corpus.iterdir()):
        raise ValueError("obfuscated corpus directory must be empty")
    out = HERE / "generic-selection"
    out.mkdir(exist_ok=True)
    (out / "master-seed.txt").write_text(f"{args.seed}\n", encoding="utf-8")
    mapping_path = out / "sealed-mapping.jsonl"
    list_path = out / "all-obfuscated.list"
    map_path = out / "tlsf-sources-obfuscated.tsv"
    verify_path = out / "obfuscation-verification.tsv"
    with (mapping_path.open("w", encoding="utf-8") as mapping_stream,
          list_path.open("w", encoding="utf-8") as list_stream,
          map_path.open("w", newline="", encoding="utf-8") as map_stream,
          verify_path.open("w", newline="", encoding="utf-8") as verify_stream):
        map_writer = csv.DictWriter(map_stream, fieldnames=("instance", "tlsf"), delimiter="\t")
        verify_writer = csv.DictWriter(verify_stream, fieldnames=("original_id", "obfuscated_id", "status"), delimiter="\t")
        map_writer.writeheader()
        verify_writer.writeheader()
        seen: set[str] = set()
        for index, (original_id, source) in enumerate(sources, 1):
            item_seed = int.from_bytes(hashlib.sha256(f"{args.seed}:{index}".encode()).digest()[:8], "big")
            target, sidecar = obfuscator.write_obfuscated(source, args.corpus, item_seed)
            obfuscated_id = target.with_suffix(".ltl").name
            if obfuscated_id in seen:
                raise ValueError(f"obfuscated ID collision: {obfuscated_id}")
            seen.add(obfuscated_id)
            detail = json.loads(sidecar.read_text(encoding="utf-8"))
            sidecar.unlink()
            if not verifier.check_pair(source, target, args.build, args.timeout):
                raise ValueError(f"lowered LTL mismatch: {original_id} -> {obfuscated_id}")
            mapping_stream.write(json.dumps({"original_id": original_id,
                                             "obfuscated_id": obfuscated_id,
                                             "tlsf": target.name, **detail}, sort_keys=True) + "\n")
            list_stream.write(obfuscated_id + "\n")
            map_writer.writerow({"instance": obfuscated_id, "tlsf": target.name})
            verify_writer.writerow({"original_id": original_id,
                                    "obfuscated_id": obfuscated_id, "status": "equal"})
            for stream in (mapping_stream, list_stream, map_stream, verify_stream):
                stream.flush()
            if index % 100 == 0:
                print(f"verified {index}/1524", flush=True)
    digest = hashlib.sha256(mapping_path.read_bytes()).hexdigest()
    (out / "sealed-mapping.sha256").write_text(f"{digest}  sealed-mapping.jsonl\n", encoding="utf-8")
    print(f"verified 1524/1524; sealed mapping sha256 {digest}")


if __name__ == "__main__":
    main()
