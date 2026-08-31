#!/usr/bin/env python3
"""Recover exact family and parameter metadata for the SYNTCOMP 2026 set.

Filenames are not evidence.  `family_names.family_of` guesses a family from an
instance name, which is fine for grouping a panel but cannot say what the
parameters of an instance *are*: it cannot tell a two-parameter series from a
one-parameter one, and it reads `abcg_arbiter_10` as the same kind of fact as
`n=10`.  The TLSF release manifest records, for every file, the template and
parameter assignment it was generated from:

    param:tlsf/arbiters_zoo/parametric/abcg_arbiter.tlsf:n=10

That origin is exact, so wherever it exists this module uses it and marks the
row `parameter_confidence=exact`.  Only `direct:` origins, which carry no
parameter data at all, fall back to the filename heuristic, and those rows are
marked `heuristic` (or `none`) so that no downstream frontier analysis can
mistake a guess for a measurement.

The join is deliberately strict.  Every one of the official instances must
resolve through all four tables, or the module raises: a coverage frontier
computed over a silently incomplete corpus would be worse than none.
"""
from __future__ import annotations

import argparse
import csv
import json
import pathlib
import sys

from family_names import family_of

EXPECTED_INSTANCES = 1524

COLUMNS = [
    "logical_instance",
    "tlsf_file",
    "origin",
    "origin_kind",
    "family_key",
    "family_display",
    "parameter_confidence",
    "parameter_names",
    "parameter_values_json",
    "parameter_dimension",
    "parameters_numeric",
    "tlsf_bytes",
    "tlsf_lines",
    "inputs",
    "outputs",
    "semantics",
    "source_target",
    "effective_target",
    "source_sha256",
]


def read_list(path: pathlib.Path) -> list[str]:
    """Non-comment, non-blank entries of an instance list, in file order."""
    out: list[str] = []
    for raw in path.read_text().splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        out.append(line)
    return out


def read_tsv(path: pathlib.Path, key: str) -> dict[str, dict[str, str]]:
    """Index a headered TSV by `key`, rejecting duplicate or conflicting rows."""
    table: dict[str, dict[str, str]] = {}
    with open(path, newline="") as handle:
        for row in csv.DictReader(handle, delimiter="\t"):
            name = row[key]
            previous = table.get(name)
            if previous is not None and previous != row:
                raise ValueError(f"duplicate conflicting row for {name} in {path}")
            table[name] = row
    return table


def parse_scalar(text: str):
    """Integer if it is one, else a finite decimal, else the original string."""
    try:
        return int(text)
    except ValueError:
        pass
    try:
        value = float(text)
    except ValueError:
        return text
    if value != value or value in (float("inf"), float("-inf")):
        return text
    return value


def parse_origin(origin: str, logical_instance: str) -> dict:
    """Split a manifest origin into family identity and parameter assignment."""
    kind, _, rest = origin.partition(":")
    if kind == "param":
        template, _, raw_parameters = rest.partition(":")
        names: list[str] = []
        values: dict[str, object] = {}
        for item in raw_parameters.split(",") if raw_parameters else []:
            name, sep, text = item.partition("=")
            if not sep:
                raise ValueError(f"malformed parameter {item!r} in origin {origin!r}")
            if name in values:
                raise ValueError(f"repeated parameter {name!r} in origin {origin!r}")
            names.append(name)
            values[name] = parse_scalar(text)
        return {
            "origin_kind": "param",
            "family_key": "param:" + template,
            "family_display": pathlib.PurePosixPath(template).name.removesuffix(".tlsf"),
            "parameter_confidence": "exact",
            "parameter_names": names,
            "parameter_values": values,
            "parameters_numeric": all(
                isinstance(v, (int, float)) for v in values.values()
            ) and bool(values),
        }
    if kind == "direct":
        parent = str(pathlib.PurePosixPath(rest).parent)
        fallback = family_of(logical_instance)
        return {
            "origin_kind": "direct",
            "family_key": f"direct:{parent}:{fallback}",
            "family_display": fallback,
            # A direct file records no parameters.  Saying "heuristic" here
            # would overstate it: the family grouping is heuristic, the
            # parameters simply do not exist.
            "parameter_confidence": "none",
            "parameter_names": [],
            "parameter_values": {},
            "parameters_numeric": False,
        }
    raise ValueError(f"unknown origin kind {kind!r} in {origin!r}")


def load_overrides(path: pathlib.Path | None) -> dict[str, dict[str, str]]:
    if path is None or not path.is_file():
        return {}
    overrides = read_tsv(path, "logical_instance")
    for name, row in overrides.items():
        if not row.get("reason", "").strip():
            raise ValueError(f"override for {name} has no reason")
    return overrides


def build(
    list_path: pathlib.Path,
    tlsf_sources: pathlib.Path,
    manifest: pathlib.Path,
    conversion: pathlib.Path,
    corpus: pathlib.Path,
    overrides_path: pathlib.Path | None = None,
    expected: int = EXPECTED_INSTANCES,
) -> list[dict]:
    instances = read_list(list_path)
    if len(set(instances)) != len(instances):
        duplicates = sorted({i for i in instances if instances.count(i) > 1})
        raise ValueError(f"duplicate logical instances in {list_path}: {duplicates}")
    if expected is not None and len(instances) != expected:
        raise ValueError(
            f"expected {expected} logical instances in {list_path}, found {len(instances)}"
        )

    sources = read_tsv(tlsf_sources, "instance")
    manifest_rows = read_tsv(manifest, "instance")
    conversion_rows = read_tsv(conversion, "instance")
    overrides = load_overrides(overrides_path)

    rows: list[dict] = []
    for logical in instances:
        source = sources.get(logical)
        if source is None:
            raise ValueError(f"{logical} has no TLSF mapping in {tlsf_sources}")
        tlsf_file = source["tlsf"]

        entry = manifest_rows.get(tlsf_file)
        if entry is None:
            raise ValueError(f"{tlsf_file} has no manifest entry in {manifest}")
        converted = conversion_rows.get(tlsf_file)
        if converted is None:
            raise ValueError(f"{tlsf_file} has no conversion metadata in {conversion}")

        parsed = parse_origin(entry["origin"], logical)

        override = overrides.get(logical)
        if override:
            parsed["family_key"] = override.get("family_key") or parsed["family_key"]
            names = override.get("parameter_names", "")
            if names:
                parsed["parameter_names"] = names.split(",")
                parsed["parameter_values"] = json.loads(
                    override["parameter_values_json"]
                )
                parsed["parameters_numeric"] = all(
                    isinstance(v, (int, float))
                    for v in parsed["parameter_values"].values()
                )
                parsed["parameter_confidence"] = "override"

        path = corpus / tlsf_file
        if path.is_file():
            data = path.read_bytes()
            tlsf_bytes = len(data)
            tlsf_lines = data.count(b"\n") + (0 if data.endswith(b"\n") or not data else 1)
        else:
            tlsf_bytes = ""
            tlsf_lines = ""

        rows.append(
            {
                "logical_instance": logical,
                "tlsf_file": tlsf_file,
                "origin": entry["origin"],
                "origin_kind": parsed["origin_kind"],
                "family_key": parsed["family_key"],
                "family_display": parsed["family_display"],
                "parameter_confidence": parsed["parameter_confidence"],
                "parameter_names": ",".join(parsed["parameter_names"]),
                "parameter_values_json": json.dumps(
                    parsed["parameter_values"], sort_keys=False
                ),
                "parameter_dimension": len(parsed["parameter_names"]),
                "parameters_numeric": "true" if parsed["parameters_numeric"] else "false",
                "tlsf_bytes": tlsf_bytes,
                "tlsf_lines": tlsf_lines,
                "inputs": converted.get("inputs", ""),
                "outputs": converted.get("outputs", ""),
                "semantics": converted.get("semantics", ""),
                "source_target": converted.get("source_target", ""),
                "effective_target": converted.get("effective_target", ""),
                "source_sha256": entry.get("sha256", ""),
            }
        )
    return rows


def orderable_families(rows: list[dict]) -> set[str]:
    """Families whose points admit a componentwise order.

    Requires identical parameter names in identical order across the family and
    numeric values throughout; anything else is reported as unordered rather
    than forced into a total order.
    """
    by_family: dict[str, list[dict]] = {}
    for row in rows:
        by_family.setdefault(row["family_key"], []).append(row)
    ordered = set()
    for key, members in by_family.items():
        if any(m["parameter_confidence"] not in ("exact", "override") for m in members):
            continue
        if any(m["parameters_numeric"] != "true" for m in members):
            continue
        names = {m["parameter_names"] for m in members}
        if len(names) != 1 or not next(iter(names)):
            continue
        ordered.add(key)
    return ordered


def main(argv=None) -> int:
    root = pathlib.Path(__file__).resolve().parents[1]
    suite = root / "tests" / "suites" / "benchmarks"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", type=pathlib.Path,
                        default=suite / "syntcomp26" / "all.list")
    parser.add_argument("--tlsf-sources", type=pathlib.Path,
                        default=suite / "syntcomp26" / "tlsf-sources.tsv")
    parser.add_argument("--manifest", type=pathlib.Path,
                        default=suite / "tlsf-manifest.tsv")
    parser.add_argument("--conversion", type=pathlib.Path,
                        default=suite / "syntcomp26" / "conversion.tsv")
    parser.add_argument("--corpus", type=pathlib.Path, default=root / "tlsf-corpus")
    parser.add_argument("--overrides", type=pathlib.Path,
                        default=root / "benchmarking" / "syntcomp26-family-overrides.tsv")
    parser.add_argument("--expected", type=int, default=EXPECTED_INSTANCES)
    parser.add_argument("--output", type=pathlib.Path,
                        default=root / "benchmarking" / "syntcomp26-family-instances.tsv")
    args = parser.parse_args(argv)

    rows = build(args.list, args.tlsf_sources, args.manifest, args.conversion,
                 args.corpus, args.overrides, args.expected)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with open(args.output, "w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=COLUMNS, delimiter="\t",
                                lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)

    ordered = orderable_families(rows)
    families = {r["family_key"] for r in rows}
    exact = sum(1 for r in rows if r["parameter_confidence"] == "exact")
    print(f"wrote {args.output} ({len(rows)} instances)")
    print(f"  families: {len(families)}  formally orderable: {len(ordered)}")
    print(f"  exact parameter origins: {exact}  direct: {len(rows) - exact}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
