#!/usr/bin/env python3
"""Convert TLSF files to .ltl/.part pairs with Acacia's linked frontend.

The converter executable is the ``tlsf-frontend-inspect`` helper built from
the same ``src/tlsf_frontend.cc`` implementation used by ``acacia-bonsai -T``.
No SyFCo or standalone tlsf-tools executable is involved.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import pathlib
import subprocess


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def selected_sources(
    source: pathlib.Path, selection: pathlib.Path | None
) -> list[pathlib.Path]:
    if selection is None:
        return sorted(source.glob("*.tlsf"), key=lambda path: path.name)

    names: list[str] = []
    for raw in selection.read_text(encoding="utf-8").splitlines():
        name = raw.split("#", 1)[0].strip()
        if not name:
            continue
        if pathlib.Path(name).name != name:
            raise ValueError(f"selection entries must be basenames: {name!r}")
        names.append(name if name.endswith(".tlsf") else f"{name}.tlsf")
    if len(names) != len(set(names)):
        raise ValueError("selection contains duplicate entries")
    paths = [source / name for name in names]
    missing = [path.name for path in paths if not path.is_file()]
    if missing:
        raise FileNotFoundError(f"selection contains missing TLSF files: {missing[:10]}")
    return paths


def inspect(
    executable: pathlib.Path, source: pathlib.Path, timeout: float
) -> tuple[str, ...]:
    result = subprocess.run(
        [str(executable), str(source)],
        check=True,
        capture_output=True,
        timeout=timeout,
    )
    fields = result.stdout.split(b"\0")
    if len(fields) != 7 or fields[-1]:
        raise RuntimeError(f"malformed native inspector output for {source.name}")
    return tuple(field.decode("utf-8") for field in fields[:-1])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--native-inspect", required=True, type=pathlib.Path)
    parser.add_argument("--selection", type=pathlib.Path)
    parser.add_argument("--list-output", type=pathlib.Path)
    parser.add_argument("--stage-timeout", type=float, default=120.0)
    args = parser.parse_args()

    if args.stage_timeout <= 0:
        parser.error("--stage-timeout must be positive")
    if not args.native_inspect.is_file():
        parser.error(f"native inspector not found: {args.native_inspect}")
    sources = selected_sources(args.source, args.selection)
    if not sources:
        parser.error(f"no TLSF files selected under {args.source}")

    args.output.mkdir(parents=True, exist_ok=True)
    rows: list[dict[str, object]] = []
    failures: list[tuple[str, str]] = []
    converted_names: list[str] = []
    for index, source in enumerate(sources, 1):
        ltl_path = args.output / f"{source.stem}.ltl"
        part_path = args.output / f"{source.stem}.part"
        try:
            formula, inputs, outputs, semantics, source_target, effective_target = inspect(
                args.native_inspect, source, args.stage_timeout
            )
            input_names = [name for name in inputs.split(",") if name]
            output_names = [name for name in outputs.split(",") if name]
            formula_bytes = (formula.rstrip() + "\n").encode("utf-8")
            input_line = ".inputs" + (f" {' '.join(input_names)}" if input_names else "")
            output_line = ".outputs" + (f" {' '.join(output_names)}" if output_names else "")
            part_bytes = f"{input_line}\n{output_line}\n".encode("utf-8")
            ltl_path.write_bytes(formula_bytes)
            part_path.write_bytes(part_bytes)
            converted_names.append(ltl_path.name)
            rows.append(
                {
                    "instance": source.name,
                    "source_sha256": sha256(source.read_bytes()),
                    "formula_sha256": sha256(formula_bytes),
                    "part_sha256": sha256(part_bytes),
                    "inputs": len(input_names),
                    "outputs": len(output_names),
                    "semantics": semantics,
                    "source_target": source_target,
                    "effective_target": effective_target,
                }
            )
        except (OSError, RuntimeError, UnicodeError, subprocess.SubprocessError) as error:
            ltl_path.unlink(missing_ok=True)
            part_path.unlink(missing_ok=True)
            failures.append((source.name, str(error).replace("\n", " ")))
        if index % 100 == 0 or index == len(sources):
            print(
                f"converted={len(converted_names)}/{len(sources)} "
                f"failed={len(failures)}",
                flush=True,
            )

    fields = [
        "instance",
        "source_sha256",
        "formula_sha256",
        "part_sha256",
        "inputs",
        "outputs",
        "semantics",
        "source_target",
        "effective_target",
    ]
    with (args.output / "conversion.tsv").open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(
            stream, fieldnames=fields, dialect="excel-tab", lineterminator="\n"
        )
        writer.writeheader()
        writer.writerows(rows)
    with (args.output / "skipped.tsv").open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream, dialect="excel-tab", lineterminator="\n")
        writer.writerow(["instance", "reason"])
        writer.writerows(failures)

    if args.list_output:
        args.list_output.parent.mkdir(parents=True, exist_ok=True)
        args.list_output.write_text(
            "# Generated by benchmarking/convert-tlsf-corpus-native.py; do not edit by hand.\n"
            f"# source={args.source} selected={len(sources)} converted={len(converted_names)} "
            f"skipped={len(failures)}\n"
            + "".join(f"{name}\n" for name in converted_names),
            encoding="utf-8",
        )
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
