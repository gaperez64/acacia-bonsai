#!/usr/bin/env python3
"""Import exact LTL/partition pairs into the shared SYNTCOMP corpus.

The pool key covers both files, so identical official names with different
conversions remain distinct while byte-identical pairs from any year are
stored once.  A suite's ``sources.tsv`` retains its logical official names.
"""

from __future__ import annotations

import argparse
import hashlib
import pathlib
import shutil
import sys

from suite_paths import SOURCE_MAP_HEADER, load_source_map


PAIR_DOMAIN = b"acacia-syntcomp-pair-v1\0"
INDEX_HEADER = "pair_sha256\tformula_sha256\tpartition_sha256"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def pair_digest(ltl: bytes, part: bytes) -> str:
    digest = hashlib.sha256()
    digest.update(PAIR_DOMAIN)
    digest.update(hashlib.sha256(ltl).digest())
    digest.update(hashlib.sha256(part).digest())
    return digest.hexdigest()


def parse_suite(raw: str) -> tuple[str, pathlib.Path]:
    if "=" not in raw:
        raise argparse.ArgumentTypeError("suite must be NAME=CORPUS_DIR")
    name, corpus = raw.split("=", 1)
    if (
        not name
        or name in {".", ".."}
        or pathlib.PurePath(name).name != name
        or not corpus
    ):
        raise argparse.ArgumentTypeError("suite must be NAME=CORPUS_DIR")
    return name, pathlib.Path(corpus)


def corpus_pairs(corpus: pathlib.Path) -> list[tuple[str, pathlib.Path, pathlib.Path]]:
    ltl_files = sorted(corpus.glob("*.ltl"), key=lambda path: path.name)
    part_stems = {path.stem for path in corpus.glob("*.part")}
    ltl_stems = {path.stem for path in ltl_files}
    missing_parts = sorted(ltl_stems - part_stems)
    orphan_parts = sorted(part_stems - ltl_stems)
    if missing_parts:
        raise FileNotFoundError(f"{corpus}: missing .part for {missing_parts[:10]}")
    if orphan_parts:
        raise FileNotFoundError(f"{corpus}: orphan .part for {orphan_parts[:10]}")
    if not ltl_files:
        raise ValueError(f"{corpus}: no .ltl/.part pairs")
    return [(path.name, path, path.with_suffix(".part")) for path in ltl_files]


def copy_exact(source: pathlib.Path, target: pathlib.Path) -> None:
    if target.exists():
        if target.read_bytes() != source.read_bytes():
            raise ValueError(f"hash collision or corrupt pool member: {target}")
        return
    shutil.copyfile(source, target)


def import_suite(
    name: str,
    corpus: pathlib.Path,
    pool: pathlib.Path,
    maps_root: pathlib.Path,
) -> int:
    pool.mkdir(parents=True, exist_ok=True)
    rows: list[tuple[str, str]] = []
    for instance, ltl_path, part_path in corpus_pairs(corpus):
        ltl = ltl_path.read_bytes()
        part = part_path.read_bytes()
        digest = pair_digest(ltl, part)
        copy_exact(ltl_path, pool / f"{digest}.ltl")
        copy_exact(part_path, pool / f"{digest}.part")
        rows.append((instance, f"{pool.name}/{digest}.ltl"))

    map_path = maps_root / name / "sources.tsv"
    map_path.parent.mkdir(parents=True, exist_ok=True)
    map_path.write_text(
        SOURCE_MAP_HEADER + "\n" + "".join(f"{instance}\t{source}\n" for instance, source in rows),
        encoding="utf-8",
    )
    return len(rows)


def pool_rows(pool: pathlib.Path) -> list[tuple[str, str, str]]:
    rows: list[tuple[str, str, str]] = []
    ltl_stems = {path.stem for path in pool.glob("*.ltl")}
    part_stems = {path.stem for path in pool.glob("*.part")}
    if ltl_stems != part_stems:
        raise ValueError(
            f"{pool}: unmatched pool members: "
            f"ltl-only={sorted(ltl_stems - part_stems)[:10]} "
            f"part-only={sorted(part_stems - ltl_stems)[:10]}"
        )
    for stem in sorted(ltl_stems):
        ltl = (pool / f"{stem}.ltl").read_bytes()
        part = (pool / f"{stem}.part").read_bytes()
        actual = pair_digest(ltl, part)
        if actual != stem:
            raise ValueError(f"{pool}/{stem}: pair digest is {actual}")
        rows.append((stem, sha256(ltl), sha256(part)))
    return rows


def write_index(pool: pathlib.Path) -> None:
    rows = pool_rows(pool)
    (pool / "index.tsv").write_text(
        INDEX_HEADER + "\n" + "".join("\t".join(row) + "\n" for row in rows),
        encoding="utf-8",
    )


def validate(pool: pathlib.Path, maps_root: pathlib.Path) -> tuple[int, int]:
    rows = pool_rows(pool)
    expected_index = INDEX_HEADER + "\n" + "".join(
        "\t".join(row) + "\n" for row in rows
    )
    index_path = pool / "index.tsv"
    if not index_path.is_file() or index_path.read_text(encoding="utf-8") != expected_index:
        raise ValueError(f"{index_path}: missing or stale; rerun without --check")

    pool_sources = {(pool / f"{row[0]}.ltl").resolve() for row in rows}
    mapped_sources: set[pathlib.Path] = set()
    logical_count = 0
    map_paths = sorted(maps_root.glob("syntcomp*/sources.tsv"))
    if not map_paths:
        raise ValueError(f"{maps_root}: no SYNTCOMP source maps")
    for map_path in map_paths:
        sources = load_source_map(map_path, ltl_root=pool.parent)
        logical_count += len(sources)
        for instance, source in sources.items():
            if source.parent != pool.resolve():
                raise ValueError(f"{map_path}: {instance} points outside {pool}")
            if source not in pool_sources:
                raise FileNotFoundError(f"{map_path}: {instance} points to missing {source}")
            mapped_sources.add(source)
    unused = pool_sources - mapped_sources
    if unused:
        raise ValueError(f"{pool}: {len(unused)} unreferenced pairs")
    return len(rows), logical_count


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pool", required=True, type=pathlib.Path)
    parser.add_argument("--maps-root", required=True, type=pathlib.Path)
    parser.add_argument("--suite", action="append", default=[], type=parse_suite)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    if args.check and args.suite:
        parser.error("--check does not accept --suite")
    if not args.check and not args.suite:
        parser.error("provide at least one --suite or use --check")

    if not args.check:
        seen: set[str] = set()
        for name, corpus in args.suite:
            if name in seen:
                parser.error(f"duplicate suite {name!r}")
            seen.add(name)
            count = import_suite(name, corpus, args.pool, args.maps_root)
            print(f"{name}: mapped {count} logical instances")
        write_index(args.pool)

    pairs, logical = validate(args.pool, args.maps_root)
    print(f"validated {pairs} distinct pairs for {logical} logical instances")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, ValueError) as error:
        sys.exit(str(error))
