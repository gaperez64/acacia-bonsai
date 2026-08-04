#!/usr/bin/env python3
"""Convert a directory of TLSF files into benchmark .ltl/.part pairs."""

from __future__ import annotations

import argparse
import csv
import pathlib
import subprocess


def failure_reason(stderr: str, stdout: str) -> str:
    text = f"{stderr}\n{stdout}".strip()
    if "strong next" in text.lower() or "x[!]" in text.lower():
        return "strong next is unsupported by SyFCo's ltlxba printer"
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    return lines[-1] if lines else "SyFCo conversion failed without a diagnostic"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--syfco", default="syfco")
    args = parser.parse_args()

    sources = sorted(args.source.glob("*.tlsf"), key=lambda path: path.name)
    if not sources:
        raise SystemExit(f"no .tlsf files found under {args.source}")
    args.output.mkdir(parents=True, exist_ok=True)

    skipped: list[dict[str, str]] = []
    converted = 0
    for source in sources:
        ltl = args.output / f"{source.stem}.ltl"
        part = args.output / f"{source.stem}.part"
        result = subprocess.run(
            [
                args.syfco,
                "--format",
                "ltlxba",
                "--mode",
                "fully",
                "--part-file",
                str(part),
                str(source),
            ],
            capture_output=True,
            text=True,
        )
        if result.returncode == 0:
            ltl.write_text(result.stdout.rstrip() + "\n")
            converted += 1
            continue

        ltl.unlink(missing_ok=True)
        part.unlink(missing_ok=True)
        skipped.append(
            {
                "instance": source.name,
                "reason": failure_reason(result.stderr, result.stdout),
            }
        )

    skipped_path = args.output / "skipped.tsv"
    with skipped_path.open("w", newline="") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=["instance", "reason"],
            delimiter="\t",
            lineterminator="\n",
        )
        writer.writeheader()
        writer.writerows(skipped)

    print(
        f"source={len(sources)} converted={converted} skipped={len(skipped)} "
        f"output={args.output}"
    )
    print(f"wrote skip reasons to {skipped_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
