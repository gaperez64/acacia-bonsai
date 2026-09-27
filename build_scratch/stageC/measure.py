#!/usr/bin/env python3.13
"""Serial informal Stage C route sample; development data only."""
from __future__ import annotations

import csv
import hashlib
import json
import os
import pathlib
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRATCH = ROOT / "build_scratch/stageC/informal"
CORPUS = ROOT / "tlsf-corpus"
ELIGIBILITY = ROOT / "benchmarking/gr1-par2-20260923/campaign/eligibility-v3.tsv"
BUILD = pathlib.Path("/home/gperez/GIT-repos/tlsf-tools/build-SB-8b158d7")


def main() -> None:
    SCRATCH.mkdir(parents=True, exist_ok=True)
    (SCRATCH / "tmp").mkdir(exist_ok=True)
    prior = list(csv.DictReader(ELIGIBILITY.open(), delimiter="\t"))
    old = [row["id"][:-4] + ".tlsf" for row in prior
           if row["decision"] == "eligible"][:20]
    old_set = {row["id"][:-4] + ".tlsf" for row in prior
               if row["decision"] == "eligible"}
    others = []
    for path in sorted(CORPUS.glob("*.tlsf")):
        if path.name in old_set:
            continue
        if "PARAMETERS" in path.read_text(encoding="utf-8", errors="replace"):
            others.append(path.name)
        if len(others) == 20:
            break
    tracked_code = [ROOT / path for path in (
        "scripts/acacia-lift-portfolio.py", "scripts/acacia_lift/runner.py",
        "scripts/acacia_lift/lifting/source.py",
        "scripts/acacia_lift/lifting/provenance.py",
        "scripts/acacia_lift/lifting/schema.py",
        "scripts/acacia_lift/lifting/proof.py",
        "scripts/acacia_lift/lifting/settings.py")]
    manifest = {"tool_build": str(BUILD),
                "code_sha256": {str(path.relative_to(ROOT)):
                                hashlib.sha256(path.read_bytes()).hexdigest()
                                for path in tracked_code},
                "tool_sha256": {name: hashlib.sha256((BUILD / name).read_bytes()).hexdigest()
                                for name in ("tlsfsolve", "tlsfcertcheck", "tlsf2tlsf")},
                "old72": old, "other": others,
                "rule": "serial, one route per input, outer cap 60 s, lift slice 59 s"}
    (SCRATCH / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    rows = []
    for group, names in (("old72", old), ("other", others)):
        for ordinal, name in enumerate(names):
            source = CORPUS / name
            record = SCRATCH / f"{group}-{ordinal:02d}.route.json"
            command = [sys.executable, str(ROOT / "scripts/acacia-lift-portfolio.py"),
                       "--cap", "60", "--lift-budget-seconds", "59",
                       "--route-record", str(record), "--", sys.executable,
                       "-c", "print('UNKNOWN'); raise SystemExit(2)",
                       "-T", str(source)]
            environment = {**os.environ, "PYTHONPATH": str(ROOT / "scripts"),
                           "ACACIA_TLSF_TOOLS_BUILD": str(BUILD),
                           "TMPDIR": str(SCRATCH / "tmp")}
            started = time.monotonic()
            try:
                completed = subprocess.run(command, cwd=ROOT, env=environment,
                                           stdout=subprocess.PIPE,
                                           stderr=subprocess.PIPE, text=True,
                                           timeout=61, check=False)
                outcome = completed.stdout.strip().splitlines()[-1:]
                verdict = outcome[0] if outcome else "UNKNOWN"
                returncode = completed.returncode
            except subprocess.TimeoutExpired:
                verdict, returncode = "TIMEOUT", 124
            route = json.loads(record.read_text()) if record.is_file() else {}
            evidence_path = pathlib.Path(str(record) + ".lift-evidence.json")
            evidence = json.loads(evidence_path.read_text()) if evidence_path.is_file() else {}
            row = {"group": group, "ordinal": ordinal, "input": name,
                   "verdict": verdict, "exit_code": returncode,
                   "route": (evidence.get("route", "attempted-declined")
                             if verdict in {"REALIZABLE", "UNREALIZABLE"} and
                             route.get("winner") == "lifting" and
                             evidence.get("target_verified") is True
                             else "attempted-declined"),
                   "winner": route.get("winner", ""),
                   "elapsed_s": round(time.monotonic() - started, 6),
                   "lift_elapsed_s": route.get("lift_elapsed", ""),
                   "last_stage": route.get("stage_censoring", {}).get("last_stage", ""),
                   "provenance_format_version": evidence.get("provenance_format_version", ""),
                   "lifting_failure": evidence.get("lifting_failure", {}),
                   "stages": evidence.get("stages", {}),
                   "seeds": evidence.get("seeds", []),
                   "predicate_arities": evidence.get("predicate_arities", {})}
            row["move_source"] = evidence.get("move_source", "")
            for stage in ("target_reduction", "seed_discovery", "seed_solves",
                          "schema", "certificate_export", "policy_export",
                          "policy_check", "region_check", "target_proof",
                          "exact_reduction", "target_solve", "target_check", "direct"):
                item = row["stages"].get(stage, {})
                row[f"{stage}_s"] = item.get("elapsed_s", "")
            rows.append(row)
            (SCRATCH / "results.json").write_text(json.dumps(rows, indent=2) + "\n")
            print(f"{len(rows):02d}/40 {group} {name} {verdict} {row['route']} "
                  f"{row['elapsed_s']:.2f}s", flush=True)
    with (SCRATCH / "results.tsv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, delimiter="\t", fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


if __name__ == "__main__":
    main()
