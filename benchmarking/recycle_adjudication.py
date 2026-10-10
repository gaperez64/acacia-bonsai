"""Evidence-bound driver decisions for otherwise strict recycling comparisons."""

import csv
import hashlib
import json
from pathlib import Path
import re

from benchlib import phase_record_dir

COLUMNS = ("series", "instance", "finding_class", "mismatch", "decision", "rationale",
           "evidence")
OUTCOME_FIELDS = ("result", "exit_code", "timed_out", "resource_reason")
CLASSES = {"telemetry-dropped": "records", "same-verdict-winner-variation": "decision"}


def sha256_file(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_adjudications(path):
    path = Path(path)
    with path.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        if reader.fieldnames != list(COLUMNS):
            raise ValueError(f"{path}: invalid adjudication columns")
        rows = list(reader)
    found = {}
    for row in rows:
        if None in row or any(not isinstance(row[key], str) or not row[key].strip()
                              for key in COLUMNS):
            raise ValueError(f"{path}: incomplete adjudication row")
        if row["finding_class"] not in CLASSES:
            raise ValueError(f"{path}: invalid finding class")
        key = (row["series"], row["instance"], row["mismatch"])
        if key in found:
            raise ValueError(f"{path}: duplicate adjudication")
        evidence = json.loads(row["evidence"])
        if not isinstance(evidence, list) or not evidence:
            raise ValueError(f"{path}: missing adjudication evidence")
        for item in evidence:
            if not isinstance(item, dict) or set(item) != {"path", "sha256"} or \
                    not isinstance(item["path"], str) or not item["path"].strip() or \
                    not isinstance(item["sha256"], str) or \
                    not re.fullmatch(r"[0-9a-f]{64}", item["sha256"]):
                raise ValueError(f"{path}: invalid evidence binding")
        found[key] = {**row, "evidence": evidence}
    return found


def paired_evidence(short_run, long_run, short, long, short_records, long_records):
    required = {}
    for name in short.keys() & long.keys():
        paths = [Path(short_run), Path(long_run)]
        for rows, root in ((short, short_records), (long, long_records)):
            if root is not None:
                row = rows[name]
                directory = phase_record_dir(root, row["solver_label"], row["cap_s"],
                                             name, row["run_index"])
                paths.extend(sorted(directory.glob("*.jsonl")))
        required[name] = {p.resolve() for p in paths}
    return required


def adjudicate(path, series, failures, short, long, *, required_evidence=None):
    """Remove only exact, hash-verified findings whose recorded outcomes agree."""
    if path is None:
        return failures, []
    rows = load_adjudications(path)
    remaining, accepted = [], []
    verified = set()
    for failure in failures:
        name = failure["instance"]
        row = rows.get((series, name, failure["message"]))
        one, two = short.get(name, {}), long.get(name, {})
        identical = all(field in one and field in two and one[field] == two[field]
                        for field in OUTCOME_FIELDS)
        same_series = one.get("solver_label") == two.get("solver_label") == series
        if row is None or not identical or not same_series or \
                failure["kind"] != CLASSES[row["finding_class"]]:
            remaining.append(failure)
            continue
        category = ("transport-completeness" if row["finding_class"] == "telemetry-dropped"
                    else "decision")
        differences = failure.get("differences", [])
        if not differences or any(d.get("category") != category for d in differences):
            remaining.append(failure)
            continue
        if row["finding_class"] == "same-verdict-winner-variation" and \
                one["result"] not in {"REALIZABLE", "UNREALIZABLE"}:
            remaining.append(failure)
            continue
        cited = {((Path(path).resolve().parent / item["path"])
                  if not Path(item["path"]).is_absolute() else Path(item["path"])).resolve()
                 for item in row["evidence"]}
        missing = (required_evidence or {}).get(name, set()) - cited
        if missing:
            raise ValueError(f"{path}: missing original comparison evidence: "
                             + ", ".join(str(p) for p in sorted(missing)))
        for item in row["evidence"]:
            source = Path(item["path"])
            if not source.is_absolute():
                source = Path(path).resolve().parent / source
            binding = (source.resolve(), item["sha256"])
            if binding not in verified:
                if sha256_file(source) != item["sha256"]:
                    raise ValueError(f"{source}: adjudication evidence SHA-256 mismatch")
                verified.add(binding)
        accepted.append({**failure, **row,
                         "outcome": {field: one[field] for field in OUTCOME_FIELDS}})
    return remaining, accepted


def section(rows):
    lines = ["## Adjudicated recycling mismatches", ""]
    if not rows:
        return "\n".join([*lines, "None.", ""])
    for row in rows:
        lines += [f"- {row['series']} / {row['instance']} [{row['finding_class']}]: "
                  f"{row['mismatch']}",
                  f"  Decision: {row['decision']}. Rationale: {row['rationale']}", ""]
    return "\n".join(lines)
