"""Resolve logical benchmark-suite names to their stored LTL files."""

from __future__ import annotations

import pathlib


SOURCE_MAP_HEADER = "instance\tsource"


def load_source_map(
    path: pathlib.Path, *, ltl_root: pathlib.Path | None = None
) -> dict[str, pathlib.Path]:
    """Load ``sources.tsv`` and return absolute paths keyed by logical name."""
    path = pathlib.Path(path)
    lines = path.read_text(encoding="utf-8").splitlines()
    if not lines or lines[0] != SOURCE_MAP_HEADER:
        raise ValueError(f"{path}: expected {SOURCE_MAP_HEADER!r} header")
    if ltl_root is None:
        # tests/suites/{tests,benchmarks}/SUITE/sources.tsv -> tests/ltl
        try:
            ltl_root = path.resolve().parents[3] / "ltl"
        except IndexError as error:
            raise ValueError(f"cannot infer tests/ltl from {path}") from error
    ltl_root = pathlib.Path(ltl_root).resolve()

    sources: dict[str, pathlib.Path] = {}
    for line_number, raw in enumerate(lines[1:], 2):
        if not raw or raw.startswith("#"):
            continue
        fields = raw.split("\t")
        if len(fields) != 2:
            raise ValueError(f"{path}:{line_number}: expected two tab-separated fields")
        instance, source = fields
        if pathlib.PurePosixPath(instance).name != instance or not instance.endswith(".ltl"):
            raise ValueError(f"{path}:{line_number}: invalid instance {instance!r}")
        source_path = pathlib.PurePosixPath(source)
        if source_path.is_absolute() or ".." in source_path.parts or not source.endswith(".ltl"):
            raise ValueError(f"{path}:{line_number}: invalid source {source!r}")
        if instance in sources:
            raise ValueError(f"{path}:{line_number}: duplicate instance {instance!r}")
        sources[instance] = (ltl_root / pathlib.Path(*source_path.parts)).resolve()
    return sources


def resolve_instance(source_map: pathlib.Path, instance: str) -> pathlib.Path:
    """Resolve one logical ``.ltl`` basename through a suite source map."""
    try:
        path = load_source_map(source_map)[instance]
    except KeyError as error:
        raise KeyError(f"{source_map}: no source for {instance!r}") from error
    if not path.is_file():
        raise FileNotFoundError(path)
    part = path.with_suffix(".part")
    if not part.is_file():
        raise FileNotFoundError(part)
    return path
