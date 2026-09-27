#!/usr/bin/env python3
"""Join sealed results with an owner-supplied name mapping.

The historical three-way join cannot be replayed from public archives: its
sealed mapping was deliberately excluded. For a future campaign, supply the
mapping and its SHA-256 via --sealed-map and --sealed-map-sha256. Fetched
observation archives can be located with scripts/acacia-evidence.py.
"""
from __future__ import annotations
import argparse
import csv
import hashlib
import json
import math
import pathlib
import re
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SOLVED = {"REALIZABLE", "UNREALIZABLE"}
RESULTS = SOLVED | {"TIMEOUT", "UNKNOWN", "RESOURCE_LIMIT", "ERROR", "SYFCO-FAIL"}
FIELDS = ("instance", "result", "seconds", "exit")


def is_signal_crash(exit_code: str) -> bool:
    """run-subset stores run.returncode, possibly via systemd-run's 128+signal encoding."""
    code = int(exit_code)
    return code < 0 or code >= 128


def load_csv(path: pathlib.Path, ids: set[str], cap: int) -> dict[str, dict[str, str]]:
    rows = {}
    with path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != list(FIELDS):
            raise ValueError(f"{path}: expected four-column cactus CSV")
        for row in reader:
            name = row["instance"]
            try:
                seconds = float(row["seconds"])
            except ValueError:
                raise ValueError(f"{path}: invalid seconds for {name}") from None
            if (name in rows or row["result"] not in RESULTS or
                    not math.isfinite(seconds) or seconds < 0 or
                    (row["result"] in SOLVED and seconds > cap)):
                raise ValueError(f"{path}: invalid or duplicate row {name}")
            rows[name] = row
    if set(rows) != ids:
        raise ValueError(f"{path}: missing {len(ids-set(rows))}, extra {len(set(rows)-ids)} IDs")
    return rows


def write_csv(path: pathlib.Path, rows: dict, ids: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=FIELDS)
        writer.writeheader()
        writer.writerows(rows[name] for name in ids)


def expected_status(original_map: dict[str, str], source_dir: pathlib.Path,
                    exceptions: pathlib.Path) -> dict[str, str]:
    corrected = {}
    with exceptions.open(newline="") as stream:
        for row in csv.DictReader(stream, delimiter="\t"):
            corrected[row["instance"]] = row["corrected_status"].upper()
    result = {}
    for original, tlsf in original_map.items():
        if tlsf in corrected:
            result[original] = corrected[tlsf]
            continue
        source = (source_dir / tlsf).read_text(errors="replace")
        matches = re.findall(r"(?im)^\s*//\s*STATUS\s*:\s*(realizable|unrealizable)\b", source)
        if matches:
            verdicts = {s.upper() for s in matches}
            if len(verdicts) == 1:
                result[original] = verdicts.pop()
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--acacia60", required=True, type=pathlib.Path)
    parser.add_argument("--acacia17", required=True, type=pathlib.Path)
    parser.add_argument("--ltlsynt60", required=True, type=pathlib.Path)
    parser.add_argument("--ltlsynt17", required=True, type=pathlib.Path)
    parser.add_argument("--tacas60", required=True, type=pathlib.Path)
    parser.add_argument("--tacas17", required=True, type=pathlib.Path)
    parser.add_argument("--sealed-map", required=True, type=pathlib.Path)
    parser.add_argument("--sealed-map-sha256", required=True,
                        help="owner-supplied SHA-256 of the sealed map")
    parser.add_argument("--obfuscated-list", required=True, type=pathlib.Path)
    parser.add_argument("--original-map", required=True, type=pathlib.Path)
    parser.add_argument("--original-corpus", required=True, type=pathlib.Path)
    parser.add_argument("--status-exceptions", required=True, type=pathlib.Path)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    args = parser.parse_args()
    try:
        # This check MUST precede even the first read of the sealed name mapping.
        absent = [str(path) for path in (args.acacia60, args.acacia17) if not path.is_file()]
        if absent:
            raise ValueError("sealed join refused: both Acacia obfuscated CSVs must exist first; missing " + ", ".join(absent))
        raw_map = args.sealed_map.read_bytes()
        if not re.fullmatch(r"[0-9a-f]{64}", args.sealed_map_sha256):
            raise ValueError("--sealed-map-sha256 must be 64 lowercase hex digits")
        if hashlib.sha256(raw_map).hexdigest() != args.sealed_map_sha256:
            raise ValueError("sealed mapping SHA-256 mismatch")
        pairs = [json.loads(line) for line in raw_map.splitlines() if line]
        obf_ids = [s for line in args.obfuscated_list.read_text().splitlines() if (s := line.strip()) and not s.startswith("#")]
        to_obf = {row["original_id"]: row["obfuscated_id"] for row in pairs}
        if len(pairs) != 1524 or len(to_obf) != 1524 or len(obf_ids) != 1524 or len(set(obf_ids)) != 1524 or set(to_obf.values()) != set(obf_ids):
            raise ValueError("sealed mapping and obfuscated list disagree")
        originals = set(to_obf)
        with args.original_map.open(newline="") as stream:
            original_rows = list(csv.DictReader(stream, delimiter="\t"))
        original_map = {row["instance"]: row["tlsf"] for row in original_rows}
        if len(original_rows) != 1524 or set(original_map) != originals:
            raise ValueError("original TLSF map disagrees with sealed map")
        expected = expected_status(original_map, args.original_corpus, args.status_exceptions)
        for cap in (60, 17):
            acacia = load_csv(getattr(args, f"acacia{cap}"), set(obf_ids), cap)
            ltlsynt_original = load_csv(getattr(args, f"ltlsynt{cap}"), originals, cap)
            tacas_original = load_csv(getattr(args, f"tacas{cap}"), originals, cap)
            ltlsynt = {to_obf[name]: {**row, "instance": to_obf[name]} for name, row in ltlsynt_original.items()}
            tacas_raw = {to_obf[name]: {**row, "instance": to_obf[name]} for name, row in tacas_original.items()}
            tacas = {name: dict(row) for name, row in tacas_raw.items()}
            audit = []
            for original, obf in to_obf.items():
                row = tacas[obf]
                verdict = row["result"]
                if verdict not in SOLVED:
                    continue
                conflicting = [tool for tool, other in (("Acacia", acacia[obf]), ("ltlsynt", ltlsynt[obf]))
                               if other["result"] in SOLVED and other["result"] != verdict]
                expected_conflict = expected.get(original) not in (None, verdict)
                exit_code = row["exit"].strip()
                unexpected_exit = exit_code != ("0" if verdict == "REALIZABLE" else "1")
                # run-subset.py writes run.returncode. Direct subprocess signals
                # are negative; systemd-run may encode them as 128 + signal.
                signal_crash = is_signal_crash(exit_code)
                if conflicting or expected_conflict or unexpected_exit:
                    audit.append({"original_id": original, "obfuscated_id": obf,
                                  "tacas_verdict": verdict, "exit": exit_code,
                                  "contradicts_tools": ",".join(conflicting),
                                  "expected_status": expected.get(original, ""),
                                  "contradicts_expected": str(expected_conflict).lower(),
                                  "unexpected_exit": str(unexpected_exit).lower(),
                                  "signal_crash": str(signal_crash).lower()})
                    # A suspect verdict must not earn solved credit in the report.
                    row["result"] = "ERROR"
                    row["seconds"] = str(cap)
            out = args.out / f"{cap}s"
            out.mkdir(parents=True, exist_ok=True)
            write_csv(out / "ltlsynt-obfuscated.csv", ltlsynt, obf_ids)
            write_csv(out / "TACAS23-raw-obfuscated.csv", tacas_raw, obf_ids)
            write_csv(out / "TACAS23-audited-obfuscated.csv", tacas, obf_ids)
            with (out / "TACAS23-audit.tsv").open("w", newline="") as stream:
                columns = ("original_id", "obfuscated_id", "tacas_verdict", "exit",
                           "contradicts_tools", "expected_status", "contradicts_expected",
                           "unexpected_exit", "signal_crash")
                writer = csv.DictWriter(stream, fieldnames=columns, delimiter="\t")
                writer.writeheader()
                writer.writerows(audit)
            series = {"ltlsynt": ltlsynt, "TACAS23 audited": tacas, "Acacia": acacia}
            with (out / "three-way-table.md").open("w") as stream:
                stream.write("| Tool | Solved | PAR-2 total (s) | Unique solves |\n|---|---:|---:|---:|\n")
                for label, rows in series.items():
                    solved = [row for row in rows.values() if row["result"] in SOLVED]
                    score = sum(float(row["seconds"]) for row in solved) + 2*cap*(1524-len(solved))
                    unique = sum(row["result"] in SOLVED and all(other[name]["result"] not in SOLVED
                                 for other_label, other in series.items() if other_label != label)
                                 for name, row in rows.items())
                    stream.write(f"| {label} | {len(solved)} | {score:.6f} | {unique} |\n")
                stream.write(f"\nTACAS23 audit flagged {len(audit)} solved rows; "
                             f"unexpected_exit={sum(r['unexpected_exit']=='true' for r in audit)}, "
                             f"signal_crash={sum(r['signal_crash']=='true' for r in audit)}. "
                             "Flagged verdicts count as ERROR.\n")
                if cap == 60:
                    stream.write("ltlsynt 60 s was derived by censoring archived 120 s observations.\n")
            command = [sys.executable, "-s", str(ROOT / "benchmarking" / "cactus-report.py"),
                       "--csv", f"ltlsynt={out / 'ltlsynt-obfuscated.csv'}",
                       "--csv", f"TACAS23 audited={out / 'TACAS23-audited-obfuscated.csv'}",
                       "--csv", f"Acacia={getattr(args, f'acacia{cap}')}",
                       "--title", f"SYNTCOMP26 obfuscated three-way ({cap} s, 8 GiB)",
                       "--timeout", str(cap), "--out-prefix", str(out / "three-way"),
                       "--markdown", str(out / "three-way-par2.md")]
            subprocess.run(command, check=True)
            print(f"{cap}s: wrote three-way table and cactus report; TACAS23 suspicious={len(audit)}, "
                  f"unexpected_exit={sum(r['unexpected_exit']=='true' for r in audit)}, "
                  f"signal_crash={sum(r['signal_crash']=='true' for r in audit)}")
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
