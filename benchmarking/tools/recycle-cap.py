#!/usr/bin/env python3
"""Derive a long-cap series from a finished short-cap leg plus targeted reruns.

This implements the owner's P4 decision (optimize-20260927/decisions.md, "The
60 s series recycles the 17 s run").  It never runs a solver.  Four steps:

  plan      classify every short-cap row as recycle or rerun; write the rerun list
  sample    draw a seeded, stratified, disclosed sample of recycled rows to rerun
  validate  compare the sampled rows' long-cap reruns with their short-cap rows
  merge     write the derived long-cap series, every row labelled with provenance

Classification of a short-cap row (C = short cap, m = --deadline-margin):
Terminal MEMOUT, SYFCO-FAIL and reviewed deterministic ERROR take priority over
the near-cap margin because their causes are cap-independent.

  TIMEOUT or timed_out                          rerun    timeout
  REALIZABLE / UNREALIZABLE                     recycle  conclusive
  non-conclusive with seconds >= C - m          rerun    deadline-near-cap
  non-conclusive, a recorded decline stage
    matches --deadline-stage-regex              rerun    deadline-stage
  CRASH by SIGKILL or SIGTERM                   rerun    external-kill
  UNKNOWN whose phase records are missing,
    torn or dropped (with --records)            rerun    decline-unverifiable
  ERROR in --deterministic-error-list            recycle  deterministic-error
  other ERROR / CRASH                            rerun    error-unverified/crash
  MEMOUT (same memory regime required)          recycle  memout-same-scope
  SYFCO-FAIL (untimed conversion failed)        recycle  syfco-fail
  UNKNOWN with a complete, non-deadline
    recorded decline                            recycle  cap-independent-decline
  UNKNOWN without such evidence                 rerun    decline-unverifiable

Why timing and not stage names: phase records carry a decline's stage but not
its tlsf-tools status, and a deadline expiry is recorded under whichever stage
was running when the clock check fired (see benchlib's phase-record notes).
The actual producers are src/native_support.cc (budget_decline with a stage),
src/native_param_lift_arm.cc (lift_decline_<stage>) and src/native_gr1_arm.cc
(reduce_decline_<stage>); tlsf-tools src/lib/gr1_lift.cc and
src/lib/gr1_reduction.cc provide the underlying stage names and deadline
status. In particular, a reduction deadline can retain its current work-stage
name, so a non-deadline spelling alone does not certify cap independence.
The only deadline tied to the cap is the invocation's absolute deadline, which
the runner arms at launch; every deadline-caused exit, including a fixed
phase budget cut short by it, therefore ends at or after C less the launch
overhead.  A non-conclusive row that ended more than m seconds before the cap
cannot have been stopped by it.  Stage names are still checked, so a future
explicit deadline stage is honoured.  Rows ending just under the cap for other
reasons are rerun; that errs towards measuring.

Validation normally compares outcome, and per arm every non-volatile
phase-record field (decision, decline stage, work counts), with
check-cap-independence.py.  Explicit --outcome-only validation compares only
outcome and failure class when per-arm records are unavailable.  Any mismatch
invalidates recycling for the whole series unless an explicit driver adjudication
exactly matches the finding, verifies every evidence hash, and preserves the
recorded outcome. Unadjudicated failures require a full long-cap rerun.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import importlib.util
import json
import math
import os
import pathlib
import random
import re
import shlex
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "benchmarking"))
from benchlib import load_phase_records, phase_record_dir  # noqa: E402
from recycle_adjudication import adjudicate, paired_evidence, section  # noqa: E402


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


coverage = _load("syntcomp26_coverage", ROOT / "benchmarking/run-syntcomp26-coverage.py")
capcheck = _load("check_cap_independence", ROOT / "benchmarking/tools/check-cap-independence.py")
campaign = _load("p4_campaign_for_recycle", ROOT / "benchmarking/tools/campaign-orchestrate.py")

SOLVED = frozenset({"REALIZABLE", "UNREALIZABLE"})
IDENTITY_FIELDS = ("solver_label", "acacia_sha", "binary_sha256", "preset", "flags",
                   "memory_max", "memory_swap_max", "allowed_cpus", "cpu_quota",
                   "collect_rusage")
CLASSIFICATION_COLUMNS = ["instance", "class", "reason", "result", "seconds", "exit_code",
                          "resource_reason", "decline_stages"]
EXTERNAL_KILL_SIGNALS = {"signal:9", "signal:15"}


class RecycleError(Exception):
    """An input that cannot support a derived series."""


def sha256_file(path) -> str:
    digest = hashlib.sha256()
    with pathlib.Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def phase_records_digest(root, rows):
    digest = hashlib.sha256()
    for row in rows:
        directory = phase_record_dir(root, row["solver_label"], row["cap_s"],
                                     row["instance"], row["run_index"])
        paths = sorted(directory.glob("*.jsonl"))
        digest.update(json.dumps([row["instance"], len(paths)]).encode() + b"\n")
        for path in paths:
            digest.update(json.dumps([str(path.relative_to(root)), sha256_file(path)]).encode()
                          + b"\n")
    return digest.hexdigest()


def write_tsv(path, columns, rows):
    coverage.atomic_write_tsv(pathlib.Path(path), list(columns), rows)
    if set(coverage.OUTPUT_COLUMNS) <= set(columns):
        coverage.write_memory_sidecar(path, rows)


def read_tsv(path):
    with pathlib.Path(path).open(encoding="utf-8", newline="") as stream:
        return list(csv.DictReader(stream, delimiter="\t"))


def write_json(path, value):
    pathlib.Path(path).parent.mkdir(parents=True, exist_ok=True)
    pathlib.Path(path).write_text(json.dumps(value, indent=2, sort_keys=True) + "\n",
                                  encoding="utf-8")


def load_leg(path, *, cap=None, instances=None):
    """One uniform-cap leg: validated rows keyed by instance, in file order."""
    path = pathlib.Path(path)
    rows = coverage.load_output(path, allow_missing_memory=True)
    if not rows:
        raise RecycleError(f"{path}: no rows")
    if "provenance" in rows[0]:
        raise RecycleError(f"{path}: is already a derived series")
    keyed = {}
    for row in rows:
        if row["instance"] in keyed:
            raise RecycleError(f"{path}: duplicate instance {row['instance']}")
        try:
            seconds = float(row["seconds"])
            row_cap = int(row["cap_s"])
        except ValueError:
            raise RecycleError(f"{path}: invalid time or cap for {row['instance']}") from None
        if not math.isfinite(seconds) or seconds < 0 or row_cap <= 0 or \
                (row["result"] in SOLVED and seconds > row_cap):
            raise RecycleError(f"{path}: invalid measured time for {row['instance']}")
        if row["timed_out"] not in {"true", "false"} or not re.fullmatch(
                r"[0-9a-f]{64}", row["binary_sha256"]):
            raise RecycleError(f"{path}: invalid observation identity for {row['instance']}")
        keyed[row["instance"]] = row
    caps = {row["cap_s"] for row in rows}
    if len(caps) != 1 or (cap is not None and caps != {str(cap)}):
        raise RecycleError(f"{path}: expected one cap {cap}, found {sorted(caps)}")
    for field in IDENTITY_FIELDS:
        if len({row[field] for row in rows}) != 1:
            raise RecycleError(f"{path}: mixed provenance field {field}")
    conflicts = path.with_name(f"{path.stem}-conflicts.tsv")
    if conflicts.exists() and coverage.load_conflicts(conflicts):
        raise RecycleError(f"{conflicts}: unresolved verdict conflicts")
    if instances is not None and set(instances) != set(keyed):
        missing, extra = sorted(set(instances) - set(keyed)), sorted(set(keyed) - set(instances))
        raise RecycleError(f"{path}: instance set differs; missing {missing}; extra {extra}")
    return keyed


def arms_from_flags(flags):
    """Recognize both required-argument forms accepted by getopt_long."""
    tokens = shlex.split(flags)
    values = []
    for index, token in enumerate(tokens):
        if token == "--arms":
            if index + 1 >= len(tokens) or tokens[index + 1].startswith("--"):
                raise RecycleError("--arms has no argument")
            values.append(tokens[index + 1])
        elif token.startswith("--arms="):
            values.append(token.partition("=")[2])
    if len(values) > 1 or (values and not all(values[0].split(","))):
        raise RecycleError("invalid or repeated --arms in observation flags")
    return values[0].split(",") if values else []


def leg_identity(args, short):
    first = next(iter(short.values()))
    flagged_arms = arms_from_flags(first["flags"])
    explicit = getattr(args, "leg_kind", None)
    source = "--leg-kind" if explicit else ""
    manifest_path = getattr(args, "manifest", None)
    manifest_sha = ""
    manifest_arms = []
    if manifest_path:
        frozen = json.loads(pathlib.Path(manifest_path).read_text(encoding="utf-8"))
        if frozen.get("comparison"):
            try:
                rebuilt, _ = campaign.read_comparison(
                    pathlib.Path(frozen["comparison"]), frozen["cap_s"],
                    selected=list(frozen["series"]),
                    subset_list=frozen["inputs"]["list"]["path"])
            except (campaign.CampaignError, OSError, KeyError, TypeError,
                    ValueError) as error:
                raise RecycleError(f"{manifest_path}: cannot verify campaign inputs: {error}") \
                    from error
            if rebuilt != frozen:
                raise RecycleError(f"{manifest_path}: campaign manifest differs from input content")
            try:
                campaign.verify_observation_labels(
                    frozen["series"][first["solver_label"]], short.values(),
                    tlsf_map=frozen["inputs"]["tlsf_map"]["path"],
                    corpus=frozen["corpus"],
                    status_exceptions=frozen["inputs"]["status_exceptions"]["path"])
            except campaign.CampaignError as error:
                raise RecycleError(f"{manifest_path}: {error}") from error
        entry = frozen.get("series", {}).get(first["solver_label"])
        if not isinstance(entry, dict) or entry.get("kind") not in {"arm", "race", "legacy"}:
            raise RecycleError(f"{manifest_path}: missing explicit leg kind for series")
        listed = frozen["inputs"]["list"]
        if sha256_file(listed["path"]) != listed["sha256"]:
            raise RecycleError(f"{manifest_path}: selected list changed")
        selected = coverage.read_instance_list(pathlib.Path(listed["path"]))
        if len(selected) != len(short) or set(selected) != set(short):
            raise RecycleError(f"{manifest_path}: selected panel differs from short leg")
        if frozen.get("cap_s") != int(first["cap_s"]) or \
                entry.get("binary_sha256") != first["binary_sha256"] or \
                entry.get("source_revision") != first["acacia_sha"]:
            raise RecycleError(f"{manifest_path}: short leg identity differs from manifest")
        if frozen.get("comparison"):
            native = entry["kind"] in {"arm", "race"}
            expected_flags = campaign.native_flags(entry) if native else entry["flags"]
            expected_preset = entry["preset"] if native else entry["tool"]
            if first["flags"] != expected_flags or first["preset"] != expected_preset or \
                    first["memory_max"] != frozen["memory_max"] or \
                    first["memory_swap_max"] != frozen["memory_swap_max"]:
                raise RecycleError(f"{manifest_path}: short treatment differs from manifest")
        expected_run = pathlib.Path(manifest_path).parent / \
            f"{first['solver_label']}-{first['cap_s']}s.tsv"
        if pathlib.Path(args.short).resolve() != expected_run.resolve():
            raise RecycleError(f"{manifest_path}: short leg path differs from manifest run")
        manifest_kind = "legacy" if entry["kind"] == "legacy" else "native"
        if explicit and explicit != manifest_kind:
            raise RecycleError("--leg-kind conflicts with manifest kind")
        explicit, source = manifest_kind, str(manifest_path)
        manifest_arms = entry.get("arms", [])
        manifest_sha = sha256_file(manifest_path)
    kind = explicit or ("native" if flagged_arms else None)
    if kind is None:
        raise RecycleError("leg kind is unknown; supply --manifest or --leg-kind legacy")
    if kind == "legacy" and (flagged_arms or manifest_arms):
        raise RecycleError("legacy leg conflicts with native arms")
    if kind == "native" and manifest_arms and flagged_arms and flagged_arms != manifest_arms:
        raise RecycleError("manifest arms differ from observation flags")
    if kind == "native" and not (flagged_arms or manifest_arms):
        raise RecycleError("native leg has no explicit arms")
    return kind, manifest_arms or flagged_arms, source or "observation --arms", manifest_sha


def classify_row(row, *, cap, margin, records=None, stage_pattern=None,
                 deterministic_errors=frozenset()):
    """Return (class, reason, decline_stages) for one short-cap row."""
    result = row["result"]
    stages = ""
    if result == "TIMEOUT" or row["timed_out"] == "true":
        return "rerun", "timeout", stages
    if result in SOLVED:
        return "recycle", "conclusive", stages
    if result == "MEMOUT":
        return "recycle", "memout-same-scope", stages
    if result == "SYFCO-FAIL":
        return "recycle", "syfco-fail", stages
    if result == "ERROR" and row["instance"] in deterministic_errors:
        return "recycle", "deterministic-error", stages
    declines = []
    unverifiable = "phase records were not supplied" if records is None else None
    if records is not None:
        try:
            found = load_phase_records(records)
        except (OSError, ValueError) as error:
            unverifiable = str(error)
        else:
            if not found.processes:
                unverifiable = f"no phase records in {records}"
            elif found.dropped:
                unverifiable = f"{found.dropped} dropped phase records in {records}"
            elif result == "UNKNOWN" and (not found.arms() or any(
                    not process.finished for process in found.processes)):
                unverifiable = f"no complete arm records in {records}"
            declines = found.declines()
            if result == "UNKNOWN" and not declines:
                unverifiable = f"no recorded decline in {records}"
            if result == "UNKNOWN" and any(not stage for _, _, stage in declines):
                unverifiable = f"decline stage missing in {records}"
    stages = ";".join(f"{arm}:{kind}:{stage}" for arm, kind, stage in declines)
    if float(row["seconds"]) >= cap - margin:
        return "rerun", "deadline-near-cap", stages
    if any(re.search("deadline", stage, re.IGNORECASE) or
           (stage_pattern is not None and stage_pattern.search(stage))
           for _, _, stage in declines):
        return "rerun", "deadline-stage", stages
    if result == "CRASH" and row["resource_reason"] in EXTERNAL_KILL_SIGNALS:
        return "rerun", "external-kill", stages
    if result == "CRASH":
        return "rerun", "crash", stages
    if result == "ERROR" and row["instance"] not in deterministic_errors:
        return "rerun", "error-unverified", stages
    if result == "UNKNOWN" and unverifiable is not None:
        return "rerun", "decline-unverifiable", stages
    reason = {"UNKNOWN": "cap-independent-decline"}.get(result)
    if reason is None:
        raise RecycleError(f"{row['instance']}: unclassifiable result {result!r}")
    return "recycle", reason, stages


def classify_plan_row(row, *, cap, margin, records, stage_pattern,
                      deterministic_errors, leg_kind, arms):
    kind, reason, stages = classify_row(
        row, cap=cap, margin=margin, records=records,
        stage_pattern=stage_pattern, deterministic_errors=deterministic_errors)
    if kind == "recycle" and row["result"] == "UNKNOWN" and leg_kind == "native":
        try:
            found = load_phase_records(records)
        except (OSError, ValueError):
            return "rerun", "decline-unverifiable", stages
        expected = set(arms)
        if not found.arms() or not set(found.arms()) <= expected or \
                not any(found.arm_finished(arm) for arm in expected):
            return "rerun", "decline-unverifiable", stages
    return kind, reason, stages


def plan(args):
    instances = (coverage.read_instance_list(args.list) if args.list else None)
    short = load_leg(args.short, instances=instances)
    first = next(iter(short.values()))
    leg_kind, arms, kind_source, source_manifest_sha = leg_identity(args, short)
    cap = int(first["cap_s"])
    if args.long_cap <= cap:
        raise RecycleError("--long-cap must exceed the short leg's cap")
    if not 0 <= args.deadline_margin < cap:
        raise RecycleError("--deadline-margin must lie in [0, short cap)")
    if (first["memory_max"], first["memory_swap_max"]) != (args.memory_max, args.memory_swap_max):
        raise RecycleError(
            f"short leg ran under MemoryMax={first['memory_max']} "
            f"MemorySwapMax={first['memory_swap_max']}, not the long leg's "
            f"{args.memory_max}/{args.memory_swap_max}; nothing can be recycled")
    pattern = re.compile(args.deadline_stage_regex) if args.deadline_stage_regex else None
    deterministic_errors = (set(coverage.read_instance_list(args.deterministic_error_list))
                            if args.deterministic_error_list else set())
    if not deterministic_errors <= set(short):
        raise RecycleError("deterministic error list contains an instance outside the leg")
    if any(short[name]["result"] != "ERROR" for name in deterministic_errors):
        raise RecycleError("deterministic error list contains a non-ERROR row")
    order = instances or list(short)
    rows, counts = [], {}
    for name in order:
        row = short[name]
        records = (phase_record_dir(args.records, row["solver_label"], row["cap_s"], name,
                                    row["run_index"]) if args.records else None)
        kind, reason, stages = classify_plan_row(
            row, cap=cap, margin=args.deadline_margin, records=records,
            stage_pattern=pattern, deterministic_errors=deterministic_errors,
            leg_kind=leg_kind, arms=arms)
        counts[f"{kind}:{reason}"] = counts.get(f"{kind}:{reason}", 0) + 1
        rows.append({"instance": name, "class": kind, "reason": reason,
                     "result": row["result"], "seconds": row["seconds"],
                     "exit_code": row["exit_code"], "resource_reason": row["resource_reason"],
                     "decline_stages": stages})
    prefix = pathlib.Path(args.out_prefix)
    classification = prefix.with_name(prefix.name + "-classification.tsv")
    rerun = prefix.with_name(prefix.name + "-rerun.list")
    write_tsv(classification, CLASSIFICATION_COLUMNS, rows)
    rerun.write_text("".join(f"{row['instance']}\n" for row in rows if row["class"] == "rerun"),
                     encoding="utf-8")
    empty_long = prefix.with_name(prefix.name + "-empty-long.tsv")
    empty_created = not any(row["class"] == "rerun" for row in rows)
    if empty_created:
        has_route = "winner" in first
        write_tsv(empty_long, coverage.OUTPUT_COLUMNS +
                  (coverage.ROUTE_COLUMNS if has_route else []), [])
    manifest = {
        "kind": "recycle-plan", "series": first["solver_label"],
        "leg_kind": leg_kind, "arms": arms, "leg_kind_source": kind_source,
        "leg_kind_verification": ("campaign specification and input bytes verified"
                                  if source_manifest_sha and args.manifest and
                                  json.loads(pathlib.Path(args.manifest).read_text()).get("comparison")
                                  else "declared or inferred from observation flags; unverified"),
        "source_manifest_sha256": source_manifest_sha,
        "short_run": str(pathlib.Path(args.short)), "short_run_sha256": sha256_file(args.short),
        "short_cap_s": cap, "long_cap_s": args.long_cap,
        "records": str(args.records) if args.records else "",
        "records_sha256": phase_records_digest(args.records, short.values())
        if args.records else "",
        "deadline_margin_s": args.deadline_margin,
        "deadline_stage_regex": args.deadline_stage_regex or "",
        "deterministic_error_list": str(args.deterministic_error_list)
        if args.deterministic_error_list else "",
        "deterministic_error_list_sha256": sha256_file(args.deterministic_error_list)
        if args.deterministic_error_list else "",
        "memory_max": args.memory_max, "memory_swap_max": args.memory_swap_max,
        "identity": {field: first[field] for field in IDENTITY_FIELDS},
        "classification": str(classification),
        "classification_sha256": sha256_file(classification),
        "rerun_list": str(rerun), "rerun_list_sha256": sha256_file(rerun),
        "empty_long": str(empty_long) if empty_created else "",
        "empty_long_sha256": sha256_file(empty_long) if empty_created else "",
        "counts": dict(sorted(counts.items())),
        "rows": len(rows),
        "rerun": sum(row["class"] == "rerun" for row in rows),
        "verification": ("source TSV, phase-record bytes and classifications are checked "
                         "on reuse; solver outcomes, source revision and any reviewed "
                         "deterministic-error claim are unverified declarations"),
    }
    plan_path = prefix.with_name(prefix.name + "-plan.json")
    write_json(plan_path, manifest)
    print(f"series {manifest['series']}: {manifest['rows']} rows, "
          f"{manifest['rerun']} to rerun at {args.long_cap} s, "
          f"{manifest['rows'] - manifest['rerun']} recycled")
    for key, value in manifest["counts"].items():
        print(f"  {key}: {value}")
    print(f"wrote {classification}\nwrote {rerun}\nwrote {plan_path}")
    return 0


def load_plan(path, *, check_records=True):
    """Read a plan and prove its inputs have not changed since it was made.

    Outcome-only replay trusts the frozen classification and skips phase-record
    checks; its sampled outcomes are checked separately by validate.
    """
    manifest = json.loads(pathlib.Path(path).read_text(encoding="utf-8"))
    if manifest.get("kind") != "recycle-plan":
        raise RecycleError(f"{path}: not a recycle plan")
    if manifest.get("leg_kind") not in {"native", "legacy"}:
        raise RecycleError(f"{path}: missing explicit leg kind; replan")
    source = manifest.get("leg_kind_source", "")
    if manifest.get("source_manifest_sha256") and sha256_file(source) != \
            manifest["source_manifest_sha256"]:
        raise RecycleError(f"{source}: leg manifest changed since planning")
    for key in ("short_run", "classification", "rerun_list"):
        if sha256_file(manifest[key]) != manifest[f"{key}_sha256"]:
            raise RecycleError(f"{manifest[key]}: changed since the plan was made")
    if manifest.get("deterministic_error_list") and sha256_file(
            manifest["deterministic_error_list"]) != manifest["deterministic_error_list_sha256"]:
        raise RecycleError("deterministic error list changed since the plan was made")
    if check_records and manifest.get("records") and phase_records_digest(
            pathlib.Path(manifest["records"]),
            load_leg(manifest["short_run"]).values()) != manifest["records_sha256"]:
        raise RecycleError("phase records changed since the plan was made")
    if manifest.get("empty_long") and sha256_file(manifest["empty_long"]) != \
            manifest["empty_long_sha256"]:
        raise RecycleError("empty long-cap placeholder changed since the plan was made")
    short = load_leg(manifest["short_run"])
    first = next(iter(short.values()))
    if manifest["series"] != first["solver_label"] or \
            manifest["short_cap_s"] != int(first["cap_s"]) or \
            manifest["long_cap_s"] <= manifest["short_cap_s"] or \
            manifest["memory_max"] != first["memory_max"] or \
            manifest["memory_swap_max"] != first["memory_swap_max"] or \
            manifest["identity"] != {field: first[field] for field in IDENTITY_FIELDS}:
        raise RecycleError(f"{path}: plan identity or cap differs from source observations")
    if manifest.get("source_manifest_sha256"):
        kind, arms, _, _ = leg_identity(argparse.Namespace(
            manifest=pathlib.Path(source), short=pathlib.Path(manifest["short_run"])), short)
        if kind != manifest["leg_kind"] or arms != manifest["arms"]:
            raise RecycleError(f"{path}: plan leg identity differs from source manifest")
    elif manifest["leg_kind"] == "native" and \
            arms_from_flags(first["flags"]) != manifest["arms"]:
        raise RecycleError(f"{path}: plan arms differ from source observation flags")
    elif manifest["leg_kind"] == "legacy" and arms_from_flags(first["flags"]):
        raise RecycleError(f"{path}: legacy plan conflicts with source observation arms")
    deterministic = set(coverage.read_instance_list(pathlib.Path(
        manifest["deterministic_error_list"]))) if manifest.get("deterministic_error_list") else set()
    pattern = re.compile(manifest["deadline_stage_regex"]) \
        if manifest.get("deadline_stage_regex") else None
    classified = read_tsv(manifest["classification"])
    if len(classified) != len(short) or \
            {row["instance"] for row in classified} != set(short):
        raise RecycleError(f"{path}: classification panel differs from source observations")
    counts = {}
    for item in classified:
        row = short[item["instance"]]
        if check_records:
            records = phase_record_dir(manifest["records"], row["solver_label"], row["cap_s"],
                                       row["instance"], row["run_index"]) \
                if manifest.get("records") else None
            kind, reason, stages = classify_plan_row(
                row, cap=manifest["short_cap_s"], margin=manifest["deadline_margin_s"],
                records=records, stage_pattern=pattern, deterministic_errors=deterministic,
                leg_kind=manifest["leg_kind"], arms=manifest["arms"])
        else:
            kind, reason, stages = item["class"], item["reason"], item["decline_stages"]
            if kind not in {"recycle", "rerun"} or not reason:
                raise RecycleError(f"{path}: invalid classification for {row['instance']}")
        expected = {"instance": row["instance"], "class": kind, "reason": reason,
                    "result": row["result"], "seconds": row["seconds"],
                    "exit_code": row["exit_code"], "resource_reason": row["resource_reason"],
                    "decline_stages": stages}
        if item != expected:
            raise RecycleError(f"{path}: classification differs from source row {row['instance']}")
        counts[f"{kind}:{reason}"] = counts.get(f"{kind}:{reason}", 0) + 1
    rerun = [item["instance"] for item in classified if item["class"] == "rerun"]
    if coverage.read_instance_list(pathlib.Path(manifest["rerun_list"])) != rerun or \
            manifest["counts"] != dict(sorted(counts.items())) or \
            manifest["rows"] != len(short) or manifest["rerun"] != len(rerun):
        raise RecycleError(f"{path}: rerun list or counts differ from classifications")
    manifest["plan_sha256"] = sha256_file(path)
    return manifest


def allocate(strata: dict[str, list[str]], size: int) -> dict[str, int]:
    """Largest-remainder allocation with at least one row per nonempty stratum."""
    populated = {name: len(items) for name, items in strata.items() if items}
    total = sum(populated.values())
    if size >= total:
        return dict(populated)
    if size < len(populated):
        raise RecycleError(f"sample size {size} cannot cover {len(populated)} recycled strata")
    allocation = dict.fromkeys(populated, 1)
    spare = size - len(populated)
    quotas = {name: spare * count / total for name, count in populated.items()}
    for name in populated:
        allocation[name] += min(int(math.floor(quotas[name])), populated[name] - 1)
    remaining = size - sum(allocation.values())
    ranked = sorted(populated, key=lambda name: (-(quotas[name] - math.floor(quotas[name])), name))
    while remaining > 0:
        progressed = False
        for name in ranked:
            if remaining and allocation[name] < populated[name]:
                allocation[name] += 1
                remaining -= 1
                progressed = True
        if not progressed:
            break
    return allocation


def sample(args):
    manifest = load_plan(args.plan)
    rows = read_tsv(manifest["classification"])
    strata: dict[str, list[str]] = {}
    for row in rows:
        if row["class"] == "recycle":
            strata.setdefault(row["reason"], []).append(row["instance"])
    if not strata:
        raise RecycleError("plan has no recycled rows to sample")
    allocation = allocate(strata, args.size)
    rng = random.Random(args.seed)
    chosen = set()
    for name in sorted(allocation):
        chosen.update(rng.sample(strata[name], allocation[name]))
    order = [row["instance"] for row in rows if row["instance"] in chosen]
    prefix = pathlib.Path(args.out_prefix)
    listing = prefix.with_name(prefix.name + "-sample.list")
    listing.write_text("".join(f"{name}\n" for name in order), encoding="utf-8")
    reasons = {row["instance"]: row["reason"] for row in rows}
    table = prefix.with_name(prefix.name + "-sample.tsv")
    write_tsv(table, ["instance", "stratum"],
              [{"instance": name, "stratum": reasons[name]} for name in order])
    disclosure = {
        "kind": "recycle-sample", "series": manifest["series"], "seed": args.seed,
        "requested_size": args.size, "size": len(order),
        "procedure": "strata are the recycle reasons; largest-remainder allocation with "
                     "one row per nonempty stratum; random.Random(seed).sample per "
                     "stratum in sorted stratum order over list-ordered candidates",
        "strata_available": {name: len(items) for name, items in sorted(strata.items())},
        "strata_sampled": dict(sorted(allocation.items())),
        "plan": str(args.plan), "plan_sha256": manifest["plan_sha256"],
        "sample_list": str(listing), "sample_list_sha256": sha256_file(listing),
        "verification": "seed and selection are reproducible; seed choice is unverified",
    }
    path = prefix.with_name(prefix.name + "-sample.json")
    write_json(path, disclosure)
    print(f"sample of {len(order)} recycled rows, seed {args.seed}: "
          + ", ".join(f"{name}={count}" for name, count in sorted(allocation.items())))
    print(f"wrote {listing}\nwrote {table}\nwrote {path}")
    return 0


def check_same_treatment(manifest, long_rows, path):
    first = next(iter(long_rows.values()))
    for field in IDENTITY_FIELDS:
        if first[field] != manifest["identity"][field]:
            raise RecycleError(f"{path}: {field} differs from the short leg "
                               f"({first[field]!r} != {manifest['identity'][field]!r}); "
                               "a rerun must be the same treatment")


def evaluate_validation(args):
    outcome_only = getattr(args, "outcome_only", False)
    reason = getattr(args, "reason", None)
    if outcome_only:
        if not isinstance(reason, str) or not reason.strip():
            raise RecycleError("--outcome-only requires a nonempty --reason")
        if args.long_records is not None:
            raise RecycleError("--outcome-only cannot be combined with --long-records")
        reason = reason.strip()
    elif reason is not None:
        raise RecycleError("--reason requires --outcome-only")
    manifest = load_plan(args.plan, check_records=not outcome_only)
    disclosure = json.loads(pathlib.Path(args.sample).read_text(encoding="utf-8"))
    if disclosure.get("plan_sha256") != manifest["plan_sha256"]:
        raise RecycleError(f"{args.sample}: drawn from a different plan")
    if disclosure.get("kind") != "recycle-sample" or \
            pathlib.Path(disclosure.get("plan", "")).resolve() != pathlib.Path(args.plan).resolve():
        raise RecycleError(f"{args.sample}: sample plan path or kind differs")
    if sha256_file(disclosure["sample_list"]) != disclosure["sample_list_sha256"]:
        raise RecycleError(f"{disclosure['sample_list']}: changed since sampling")
    chosen = coverage.read_instance_list(pathlib.Path(disclosure["sample_list"]))
    classification = read_tsv(manifest["classification"])
    strata = {}
    for row in classification:
        if row["class"] == "recycle":
            strata.setdefault(row["reason"], []).append(row["instance"])
    allocation = allocate(strata, disclosure["requested_size"])
    rng = random.Random(disclosure["seed"])
    drawn = set()
    for name in sorted(allocation):
        drawn.update(rng.sample(strata[name], allocation[name]))
    expected_chosen = [row["instance"] for row in classification if row["instance"] in drawn]
    if chosen != expected_chosen or disclosure.get("size") != len(chosen) or \
            disclosure.get("series") != manifest["series"] or \
            disclosure.get("strata_available") != {name: len(items) for name, items in sorted(strata.items())} or \
            disclosure.get("strata_sampled") != dict(sorted(allocation.items())):
        raise RecycleError(f"{args.sample}: disclosed sample differs from seeded selection")
    short = load_leg(manifest["short_run"])
    long = load_leg(args.long, cap=manifest["long_cap_s"])
    check_same_treatment(manifest, long, args.long)
    native = manifest["leg_kind"] == "native"
    if not outcome_only and native and not manifest["records"]:
        raise RecycleError("native validation requires short phase records; replan with --records")
    if not outcome_only and (native or manifest["records"]) and not args.long_records:
        raise RecycleError("native validation requires --long-records")
    short_records = (pathlib.Path(manifest["records"])
                     if manifest["records"] and not outcome_only else None)
    missing = [name for name in chosen if name not in long]
    extra = [name for name in long if name not in chosen]
    sampled_short = {name: short[name] for name in chosen}
    sampled_long = {name: long[name] for name in chosen if name in long}
    checked, failures, notes = capcheck.compare(
        sampled_short, sampled_long, short_records,
        args.long_records if short_records is not None else None,
        arms=None, max_long_seconds=None, subset=True, allow_race_truncation=True)
    if native and not outcome_only:
        for name in chosen:
            if name not in sampled_long:
                continue
            try:
                one = capcheck.comparison_records(phase_record_dir(
                    short_records, short[name]["solver_label"], short[name]["cap_s"],
                    name, short[name]["run_index"]))
                two = capcheck.comparison_records(phase_record_dir(
                    args.long_records, long[name]["solver_label"], long[name]["cap_s"],
                    name, long[name]["run_index"]))
            except (OSError, ValueError) as error:
                if not any(f["instance"] == name and f["kind"] == "records" for f in failures):
                    failures.append({"instance": name, "arm": None, "kind": "records",
                                     "message": f"{name}: {error}"})
                continue
            expected_arms = set(manifest["arms"])
            if not (one.arms() and two.arms() and
                    capcheck.solver_arms(one) <= expected_arms and
                    capcheck.solver_arms(two) <= expected_arms and
                    any(one.arm_finished(arm) and two.arm_finished(arm) and
                        not any(p.dropped for p in one.arm_processes(arm) + two.arm_processes(arm))
                        for arm in expected_arms)):
                failures.append({"instance": name, "arm": None, "kind": "records",
                                 "message": f"{name}: no shared completed arm records for "
                                            "expected arms; decision and work not validated"})
    failures += [{"instance": name, "arm": None, "kind": "missing",
                  "message": f"{name}: sampled row not rerun at {manifest['long_cap_s']} s"}
                 for name in missing]
    failures += [{"instance": name, "arm": None, "kind": "panel",
                  "message": f"{name}: long-cap row is outside the disclosed sample"}
                 for name in extra]
    adjudications = getattr(args, "adjudications", None)
    try:
        failures, adjudicated = adjudicate(
            adjudications, manifest["series"], failures, sampled_short, sampled_long,
            required_evidence=paired_evidence(manifest["short_run"], args.long,
                                              sampled_short, sampled_long,
                                              short_records, args.long_records))
    except (OSError, ValueError) as error:
        raise RecycleError(f"adjudication rejected: {error}") from error
    arms = {failure["arm"] for failure in failures if failure["arm"]}
    if not outcome_only and any(failure["arm"] is None for failure in failures):
        arms.update(manifest["arms"] or [manifest["series"]])
    arms = sorted(arms)
    verdict = {
        "kind": "recycle-validation", "series": manifest["series"],
        "status": "fail" if failures else "pass",
        "plan_sha256": manifest["plan_sha256"],
        "sample": str(args.sample), "sample_sha256": sha256_file(args.sample),
        "long_run": str(pathlib.Path(args.long)), "long_run_sha256": sha256_file(args.long),
        "long_records": str(args.long_records) if args.long_records else "",
        "long_records_sha256": phase_records_digest(args.long_records, long.values())
        if args.long_records else "",
        "checked": checked, "sample_size": len(chosen),
        "failures": failures, "notes": notes, "mismatched_arms": arms,
        "adjudications": str(pathlib.Path(adjudications).resolve()) if adjudications else "",
        "adjudications_sha256": sha256_file(adjudications) if adjudications else "",
        "adjudicated_mismatches": adjudicated,
        "verification": ("outcome-only" if outcome_only else
                         "sample and phase records replayed; solver outcomes unverified"),
    }
    if outcome_only:
        verdict["reason"] = reason
        verdict["limitation"] = (
            "Per-arm decisions, decline stages and work were not compared: " + reason)
    return verdict, disclosure


def validate(args):
    verdict, disclosure = evaluate_validation(args)
    failures, notes = verdict["failures"], verdict["notes"]
    write_json(args.out, verdict)
    print(f"series {verdict['series']}: compared {verdict['checked']}/{verdict['sample_size']} sampled rows "
          f"(seed {disclosure['seed']})")
    for note in notes:
        print(f"note: {note}")
    for failure in failures:
        print(f"MISMATCH [{failure['kind']}] {failure['message']}")
    print(section(verdict["adjudicated_mismatches"]))
    if failures:
        arms = verdict["mismatched_arms"]
        where = (f"; phase records differ on arm(s) {', '.join(arms)}" if arms else "")
        print(f"RECYCLING INVALID for series {verdict['series']}{where}: rerun the full "
              f"long-cap leg")
        return 1
    print(f"recycling validated for series {verdict['series']}")
    return 0


def merge(args):
    verdict = (json.loads(pathlib.Path(args.validation).read_text(encoding="utf-8"))
               if args.validation is not None else None)
    outcome_only = verdict is not None and verdict.get("verification") == "outcome-only"
    manifest = load_plan(args.plan, check_records=not outcome_only)
    rows = read_tsv(manifest["classification"])
    expected_reruns = {entry["instance"] for entry in rows if entry["class"] == "rerun"}
    recycled_count = len(rows) - len(expected_reruns)
    if recycled_count:
        if args.validation is None:
            raise RecycleError("a validation verdict is required when rows are recycled")
        if verdict.get("kind") != "recycle-validation" or verdict.get("status") != "pass":
            raise RecycleError(f"{args.validation}: recycling was not validated; "
                               "the series needs a full long-cap leg")
        if verdict["plan_sha256"] != manifest["plan_sha256"]:
            raise RecycleError(f"{args.validation}: validated a different plan")
        # The sample and production reruns are intentionally disjoint runs.
        if sha256_file(verdict["long_run"]) != verdict["long_run_sha256"]:
            raise RecycleError(f"{verdict['long_run']}: validation sample changed")
        if manifest["leg_kind"] == "native" and not outcome_only:
            if not verdict.get("long_records") or not verdict.get("long_records_sha256"):
                raise RecycleError(f"{args.validation}: missing native validation records")
            if phase_records_digest(pathlib.Path(verdict["long_records"]),
                                    load_leg(verdict["long_run"]).values()) != \
                    verdict["long_records_sha256"]:
                raise RecycleError(f"{verdict['long_records']}: validation records changed")
        if verdict["sample_size"] < 1 or verdict["checked"] != verdict["sample_size"]:
            raise RecycleError(f"{args.validation}: incomplete validation sample")
        if not verdict.get("sample"):
            raise RecycleError(f"{args.validation}: missing sample for validation replay")
        replay, _ = evaluate_validation(argparse.Namespace(
            plan=args.plan, sample=pathlib.Path(verdict["sample"]),
            long=pathlib.Path(verdict["long_run"]),
            long_records=pathlib.Path(verdict["long_records"])
            if verdict.get("long_records") else None,
            outcome_only=outcome_only, reason=verdict.get("reason"),
            adjudications=pathlib.Path(verdict["adjudications"])
            if verdict.get("adjudications") else None))
        if replay != verdict:
            raise RecycleError(f"{args.validation}: validation verdict differs from replay")
    elif args.validation is not None:
        raise RecycleError("no rows are recycled, so --validation must be omitted")
    short = load_leg(manifest["short_run"])
    if expected_reruns:
        long = load_leg(args.long, cap=manifest["long_cap_s"])
        check_same_treatment(manifest, long, args.long)
    else:
        long_rows = coverage.load_output(args.long)
        long = {row["instance"]: row for row in long_rows}
    if set(long) != expected_reruns:
        missing = sorted(expected_reruns - set(long))
        extra = sorted(set(long) - expected_reruns)
        raise RecycleError(f"{args.long}: rerun panel differs; missing {missing}, extra {extra}")
    output = pathlib.Path(args.output)
    if output.resolve() in {pathlib.Path(p).resolve() for p in
                            (manifest["short_run"], args.long, manifest["classification"])}:
        raise RecycleError("output must differ from every input")
    base = output.parent.resolve()
    sources = {}
    for kind, path, cap in (("recycled", manifest["short_run"], manifest["short_cap_s"]),
                            ("measured", args.long, manifest["long_cap_s"])):
        sources[kind] = {"provenance": f"{kind}-{cap}s",
                         "source_run": os.path.relpath(pathlib.Path(path).resolve(), base),
                         "source_run_sha256": sha256_file(path), "source_cap_s": str(cap)}
    has_route = "winner" in next(iter(short.values()))
    if long and has_route != ("winner" in next(iter(long.values()))):
        raise RecycleError("short and long legs disagree on route columns")
    columns = coverage.OUTPUT_COLUMNS + (coverage.ROUTE_COLUMNS if has_route else []) \
        + coverage.DERIVED_COLUMNS
    derived = []
    for entry in rows:
        name = entry["instance"]
        if entry["class"] == "rerun":
            if name not in long:
                raise RecycleError(f"{name}: on the rerun list but absent from {args.long}")
            row = dict(long[name], **sources["measured"])
        else:
            row = dict(short[name], **sources["recycled"])
            row["cap_s"] = str(manifest["long_cap_s"])
        derived.append({column: row.get(column, "")
                        for column in columns + coverage.MEMORY_COLUMNS})
    write_tsv(output, columns, derived)
    summary = coverage.write_summary(output, manifest["series"],
                                     [row["instance"] for row in derived], derived,
                                     manifest["long_cap_s"])
    recycled = sum(row["provenance"].startswith("recycled") for row in derived)
    validation_mode = ("outcome-only" if outcome_only else "phase-records") if verdict else ""
    statement = (f"derived {manifest['long_cap_s']} s series: {recycled} rows recycled from "
                 f"the {manifest['short_cap_s']} s run, {len(derived) - recycled} measured at "
                 f"{manifest['long_cap_s']} s; not a single {manifest['long_cap_s']} s run")
    write_json(output.with_name(f"{output.stem}-provenance.json"), {
        "kind": "recycle-merge", "statement": statement, "series": manifest["series"],
        "plan_sha256": manifest["plan_sha256"],
        "validation": str(pathlib.Path(args.validation).resolve()) if args.validation else "",
        "validation_sha256": sha256_file(args.validation) if args.validation else "",
        "adjudicated_mismatches": verdict.get("adjudicated_mismatches", []) if verdict else [],
        "validation_mode": validation_mode,
        "limitation": verdict.get("limitation", "") if verdict else "",
        "row_validation_modes": {
            row["instance"]: validation_mode for row in derived
            if row["provenance"].startswith("recycled-")
        },
        "sources": sources, "rows": len(derived), "recycled_rows": recycled,
        "measured_rows": len(derived) - recycled,
        "verification": "source rows and validation replayed; solver outcomes unverified",
        "extra_long_rows_not_used": sorted(set(long) - {row["instance"] for row in rows
                                                       if row["class"] == "rerun"}),
    })
    print(statement)
    print(f"wrote {output}\nwrote {summary}")
    return 0


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("plan", help="classify a short-cap leg; write the rerun list")
    p.add_argument("--short", required=True, type=pathlib.Path, help="short-cap coverage TSV")
    p.add_argument("--records", type=pathlib.Path,
                   help="the short leg's --phase-records-dir; declines without usable "
                        "records are then rerun")
    p.add_argument("--manifest", type=pathlib.Path,
                   help="campaign manifest identifying the short leg and selected panel")
    p.add_argument("--leg-kind", choices=("native", "legacy"),
                   help="explicit leg kind when no campaign manifest is available")
    p.add_argument("--deterministic-error-list", type=pathlib.Path,
                   help="reviewed ERROR instance IDs whose cause is deterministic; "
                        "other ERRORs are rerun")
    p.add_argument("--list", type=pathlib.Path, help="require exactly these instances")
    p.add_argument("--long-cap", required=True, type=int)
    p.add_argument("--deadline-margin", type=float, default=1.0,
                   help="non-conclusive rows ending within this many seconds of the short "
                        "cap are treated as deadline-bound (default 1.0)")
    p.add_argument("--deadline-stage-regex", default="deadline",
                   help="decline stages matching this are deadline-bound (default: deadline)")
    p.add_argument("--memory-max", default="8G", help="the long leg's MemoryMax")
    p.add_argument("--memory-swap-max", default="0", help="the long leg's MemorySwapMax")
    p.add_argument("--out-prefix", required=True, type=pathlib.Path)
    p.set_defaults(func=plan)
    p = sub.add_parser("sample", help="seeded stratified sample of recycled rows")
    p.add_argument("--plan", required=True, type=pathlib.Path)
    p.add_argument("--seed", required=True, type=int)
    p.add_argument("--size", required=True, type=int)
    p.add_argument("--out-prefix", required=True, type=pathlib.Path)
    p.set_defaults(func=sample)
    p = sub.add_parser("validate", help="compare the sample's long-cap reruns")
    p.add_argument("--plan", required=True, type=pathlib.Path)
    p.add_argument("--sample", required=True, type=pathlib.Path, help="the -sample.json file")
    p.add_argument("--long", required=True, type=pathlib.Path, help="long-cap coverage TSV")
    p.add_argument("--long-records", type=pathlib.Path)
    p.add_argument("--adjudications", type=pathlib.Path,
                   help="driver decisions bound to exact mismatches and SHA-256 evidence")
    p.add_argument("--outcome-only", action="store_true",
                   help="compare sampled outcomes and failure classes without phase records")
    p.add_argument("--reason", help="why per-arm validation is unavailable; required with "
                   "--outcome-only")
    p.add_argument("--out", required=True, type=pathlib.Path, help="verdict JSON")
    p.set_defaults(func=validate)
    p = sub.add_parser("merge", help="write the derived long-cap series")
    p.add_argument("--plan", required=True, type=pathlib.Path)
    p.add_argument("--long", required=True, type=pathlib.Path)
    p.add_argument("--validation", type=pathlib.Path,
                   help="required if any rows are recycled")
    p.add_argument("--output", required=True, type=pathlib.Path)
    p.set_defaults(func=merge)
    return parser


def main(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)
    if getattr(args, "size", 1) < 1:
        parser.error("--size must be positive")
    try:
        return args.func(args)
    except (RecycleError, coverage.CoverageError, OSError, KeyError,
            json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
