#!/usr/bin/env python3
"""Classify paired coverage using source-map identities and observed verdicts only.

No solver is run. --series CAP:acacia=TSV and CAP:comparator=TSV are repeatable.
The existing closing join checks row schemas, caps and derived source digests.
"""
from __future__ import annotations

import argparse
import csv
from collections import Counter
import importlib.util
import json
import math
from pathlib import Path
import random
import re
import shlex
import sys

from benchlib import PhaseRecords, load_phase_records, phase_record_dir
from family_metadata import parse_origin, read_tsv


def load_tool(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


HERE = Path(__file__).resolve().parent
join = load_tool("classification_join", HERE / "tools/threeway-join.py")
frontier = load_tool("classification_frontier", HERE / "build-coverage-frontier.py")
write_tsv = frontier.write_tsv
SOLVED = join.SOLVED
UNKNOWN = "UNKNOWN"


def metadata(instances, mapping, manifest, conversion, corpus):
    """Use declared origins; never call the direct-file family-name heuristic."""
    result = {}
    for name in instances:
        tlsf = mapping[name]
        source, converted = manifest[tlsf], conversion[tlsf]
        path = (corpus / tlsf).resolve()
        if not path.is_relative_to(corpus.resolve()):
            raise ValueError(f"source escapes corpus: {tlsf}")
        if join.sha256_file(path) != source["sha256"] or \
                converted["source_sha256"] != source["sha256"]:
            raise ValueError(f"source digest differs: {name}")
        text = path.read_text()
        # PARAMETERS in expanded files may be gone; the param origin retains
        # their exact declared assignments. Direct files are inspected as text.
        clean = re.sub(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*.*?\*/', "", text, flags=re.S)
        if source["origin"].startswith("param:"):
            parsed = parse_origin(source["origin"], name)
            params = parsed["parameter_values"]
            declared = bool(params)
        elif source["origin"].startswith("direct:"):
            block = re.search(r"\bPARAMETERS\s*\{([^{}]*)\}", clean, re.S)
            params = dict(re.findall(r"([A-Za-z_]\w*)\s*=\s*([^;]+);", block[1])) \
                if block else {}
            declared = bool(block and block[1].strip())
        else:
            raise ValueError(f"unsupported manifest origin: {source['origin']}")
        result[name] = {
            "instance": name, "tlsf_file": tlsf, "origin": source["origin"],
            "source_sha256": source["sha256"], "declared_parameters": str(declared).lower(),
            "parameters_json": json.dumps(params, sort_keys=True),
            "parameter_dimension": len(params), "tlsf_bytes": path.stat().st_size,
            "inputs": int(converted["inputs"]), "outputs": int(converted["outputs"]),
            "semantics": converted["semantics"], "effective_target": converted["effective_target"],
        }
    return result


def observed_polarity(rows):
    verdicts = {row["result"] for row in rows if row["result"] in SOLVED}
    if len(verdicts) > 1:
        raise ValueError("opposing observed verdicts")
    return {"REALIZABLE": "REAL", "UNREALIZABLE": "UNREAL"}.get(
        next(iter(verdicts), ""), UNKNOWN)


def classify(instances, meta, acacia, comparator, cap, near_fraction=0.8):
    """Partition the entire manifest, retaining individual row provenance."""
    if set(acacia) != set(instances) or set(comparator) != set(instances):
        raise ValueError("row set differs from logical instance list")
    out = []
    for name in sorted(instances):
        a, b = acacia[name], comparator[name]
        for row in (a, b):
            # Converted-pair rows put the exact logical alias in tlsf_file.
            # Accept only that map key or its explicitly mapped TLSF value.
            if row["tlsf_file"] not in {name, meta[name]["tlsf_file"]}:
                raise ValueError(f"source-map mismatch: {name}")
            seconds = float(row["seconds"])
            if not math.isfinite(seconds) or seconds < 0 or \
                    (row["result"] in SOLVED and seconds > cap):
                raise ValueError(f"invalid wall time: {name}")
            if row["result"] in SOLVED and "exit_code" in row and row["exit_code"] != \
                    ("0" if row["result"] == "REALIZABLE" else "1"):
                raise ValueError(f"solved verdict/exit mismatch: {name}")
        pair = (a["result"] in SOLVED, b["result"] in SOLVED)
        kind = {(False, True): "ltlsynt-only", (True, False): "Acacia-only",
                (False, False): "both-unsolved", (True, True): "common-solved"}[pair]
        near = pair == (True, True) and max(float(a["seconds"]), float(b["seconds"])) \
            >= cap * near_fraction
        row = {**meta[name], "cap_s": cap, "set": kind,
               "near_cap": str(near).lower(), "polarity": observed_polarity((a, b))}
        for label, observation in (("acacia", a), ("comparator", b)):
            row.update({f"{label}_{key}": observation.get(key, "") for key in (
                "result", "seconds", "binary_sha256", "acacia_sha", "source_run",
                "source_run_sha256", "source_cap_s", "provenance")})
            row[f"{label}_provenance"] = observation.get("provenance", f"measured-{cap}s")
        out.append(row)
    return out


def structural_sample(rows, size, seed):
    """Round-robin log2(size)/log2(AP)/parameter strata, seeded within strata."""
    rng = random.Random(seed)
    strata = {}
    for row in sorted(rows, key=lambda item: item["instance"]):
        key = (int(row["tlsf_bytes"]).bit_length() - 1,
               (int(row["inputs"]) + int(row["outputs"])).bit_length() - 1,
               int(row["parameter_dimension"]))
        strata.setdefault(key, []).append(row)
    for group in strata.values():
        rng.shuffle(group)
    keys = sorted(strata)
    rng.shuffle(keys)
    chosen = []
    while keys and len(chosen) < size:
        for key in keys[:]:
            chosen.append({**strata[key].pop(), "sample_stratum": json.dumps(key),
                           "sample_seed": seed, "sample_rank": len(chosen) + 1})
            if not strata[key]:
                keys.remove(key)
            if len(chosen) == size:
                break
    return chosen


def stop_category(reason):
    """Map explicit recorded stop reasons, with an UNKNOWN fallback."""
    if reason in {"mp-class", "semantics", "exact_reduction"}:
        return "exact-reduction rejection"
    if reason.startswith("budget-") or reason in {"allocation", "aag-size", "publish-size",
                                                       "metadata-size"}:
        return "reduction resources"
    if reason in {"seed_window", "seed_polarity", "parameters"}:
        return "seed window"
    if reason.startswith(("schema", "bus_schema", "typed_alignment", "rank_shape")):
        return "schema fitting"
    if reason in {"policy_reconstruct", "mode_relation"}:
        return "U policy reconstruction"
    if reason in {"seed_check", "internal_check", "check", "checking"}:
        return "checking"
    if reason == "translation-acceptance-set-limit":
        return "translation size"
    if reason in {"translation-exception", "translation-time", "translation-size"}:
        return reason.replace("-", " ")
    return UNKNOWN


def phase_attribution(events):
    """Preserve first recorded decline and last completed stage; no counter guesses.

    Combined-arm summaries are emitted after the call, so their order is not
    a timeline. A decline in them is a route stop, not a portfolio failure.
    """
    visible = [e for e in events if e.get("phase") != "record_summary"]
    stops = []
    for event in visible:
        phase = event.get("phase", "")
        if phase == "budget_decline":
            reason = event.get("stage", "")
        else:
            reason = next((phase[len(prefix):] for prefix in
                           ("reduce_decline_", "lift_decline_", "decline_")
                           if phase.startswith(prefix)), "")
        if reason:
            stops.append((reason, stop_category(reason)))
    route = next((e["phase"].removeprefix("route_") for e in visible
                  if e.get("phase", "").startswith("route_")), UNKNOWN)
    return {"first_stop_reason": stops[0][0] if stops else UNKNOWN,
            "first_stop_category": stops[0][1] if stops else UNKNOWN,
            "last_observed_stage": visible[-1]["phase"] if visible else UNKNOWN,
            "recorded_route": route,
            "all_stop_reasons": json.dumps([reason for reason, _ in stops])}



def table_rows(path):
    with path.open() as stream:
        return list(csv.DictReader(stream, delimiter="\t"))


def requested_arms(flags):
    tokens = shlex.split(flags)
    for index, token in enumerate(tokens):
        if token == "--arms" and index + 1 < len(tokens):
            return tokens[index + 1].split(",")
        if token.startswith("--arms="):
            return token.split("=", 1)[1].split(",")
    return []


def evidence_path(root, value):
    path = (root / value).resolve()
    if not path.is_relative_to(root.resolve()):
        raise ValueError(f"evidence path escapes root: {value}")
    return path


def arm_attribution(records, arm, standalone=False):
    """An anonymous legacy producer is identifiable only in an explicit solo run."""
    producer = arm
    if standalone and not records.arm_processes(arm) and records.arm_processes("legacy"):
        producer = "legacy"
    processes = records.arm_processes(producer)
    events = [e for process in processes for e in process.events if e.get("arm") == producer]
    # File/PID order across producers cannot establish a first or last stage.
    detail = phase_attribution(events if len(processes) == 1 else [])
    terminal = [e for e in events if e.get("phase") == "budget_decline" or
                str(e.get("phase", "")).startswith(("reduce_decline_", "lift_decline_"))]
    stop = phase_attribution(terminal if len(processes) == 1 else [])
    detail.update(first_terminal_obstruction=stop["first_stop_category"],
                  terminal_reason=stop["first_stop_reason"], processes=len(processes),
                  records_complete=bool(processes) and all(p.finished for p in processes)
                  and records.dropped == 0,
                  record_paths=json.dumps([str(records.directory / p.name) for p in processes]),
                  phase_sequence=json.dumps([e.get("phase") for e in events]),
                  producer_arm=producer if processes else UNKNOWN)
    return detail


def load_evidence(manifest_path, mapping, selected, verdicts):
    """Read explicit row/phase roots; campaign labels are provenance data only.

    Invocation results do not override recorded per-arm obstructions.
    Manifest columns: campaign, root, rows, phase_root, arm, regime, comparability.
    Optional row_identity_map binds runner aliases; selection=all retains a full probe panel.
    Paths below root are archive-relative. No file discovery selects observations.
    """
    observations, hashes = [], {str(manifest_path): join.sha256_file(manifest_path)}
    for entry in table_rows(manifest_path):
        root = Path(entry["root"])
        if not root.is_absolute():
            root = manifest_path.parent / root
        path = evidence_path(root, entry["rows"])
        hashes[str(path)] = join.sha256_file(path)
        aliases = {}
        if entry.get("row_identity_map"):
            alias_path = Path(entry["row_identity_map"])
            if not alias_path.is_absolute():
                alias_path = manifest_path.parent / alias_path
            hashes[str(alias_path)] = join.sha256_file(alias_path)
            aliases = read_tsv(alias_path, "row_instance")
        seen = set()
        for row in table_rows(path):
            raw_name = row["instance"]
            alias = aliases.get(raw_name)
            if aliases and (not alias or alias["row_tlsf"] != row["tlsf_file"]):
                raise ValueError(f"auxiliary alias/source mismatch: {raw_name}")
            name = alias["instance"] if alias else raw_name
            identity = (name, row["cap_s"], row["run_index"])
            if identity in seen:
                raise ValueError(f"duplicate evidence row: {identity}")
            seen.add(identity)
            tlsf = alias["tlsf_file"] if alias else row["tlsf_file"]
            if name not in mapping or tlsf not in {name, mapping[name]}:
                raise ValueError(f"auxiliary source identity mismatch: {name}")
            cap, seconds = int(row["cap_s"]), float(row["seconds"])
            if cap <= 0 or not math.isfinite(seconds) or seconds < 0 or \
                    (row["result"] in SOLVED and seconds > cap):
                raise ValueError(f"invalid evidence wall time: {name}")
            if row["result"] in SOLVED and row["exit_code"] != \
                    ("0" if row["result"] == "REALIZABLE" else "1"):
                raise ValueError(f"evidence solved verdict/exit mismatch: {name}")
            observed_polarity([*verdicts.get(name, []), row])
            verdicts.setdefault(name, []).append({"result": row["result"]})
            if name not in selected and entry.get("selection") != "all":
                continue
            arms = requested_arms(row.get("flags", ""))
            if not arms and entry.get("arm"):
                arms = [entry["arm"]]
            if not arms:
                raise ValueError("evidence needs explicit requested arms")
            solo = len(arms) == 1
            records = load_phase_records(evidence_path(root, entry["phase_root"]) /
                phase_record_dir(Path(), row["solver_label"], cap, name, row["run_index"])) \
                if entry.get("phase_root") else PhaseRecords(root, ())
            snapshots, snapshot_paths = [], []
            if entry.get("worker_root"):
                directory = evidence_path(root, entry["worker_root"]) / \
                    row["solver_label"] / str(cap) / raw_name
                if not directory.resolve().is_relative_to(root.resolve()):
                    raise ValueError("worker records escape evidence root")
                for snapshot_path in sorted(directory.glob("*.json")):
                    snapshot = json.loads(snapshot_path.read_text())
                    if snapshot.get("instance") != raw_name:
                        raise ValueError("worker snapshot identity mismatch")
                    snapshots.append(snapshot)
                    snapshot_paths.append(str(snapshot_path))
                    hashes[str(snapshot_path)] = join.sha256_file(snapshot_path)
            for arm in arms:
                detail = arm_attribution(records, arm, standalone=solo)
                matching = [snapshot for snapshot in snapshots if ":".join(
                    snapshot.get(k, "") for k in
                    ("polarity", "transform", "requested_backend")) == arm]
                if detail["last_observed_stage"] == UNKNOWN and len(matching) == 1:
                    detail["last_observed_stage"] = matching[0].get("stage", UNKNOWN)
                for record in json.loads(detail["record_paths"]):
                    hashes[record] = join.sha256_file(Path(record))
                observations.append({"campaign": entry["campaign"], "instance": name,
                    "context": "standalone" if solo else "race", "arm": arm,
                    "evidence_cap_s": cap, "evidence_view": "measured archival",
                    "result": row["result"], "seconds": seconds,
                    **{key: row.get(key, "") for key in (
                        "binary_sha256", "acacia_sha", "preset", "flags", "run_index",
                        "memory_max", "memory_swap_max", "allowed_cpus", "cpu_quota")},
                    "regime": entry["regime"], "comparability": entry["comparability"],
                    "row_path": str(path), "row_instance": raw_name,
                    "resource_reason": row.get("resource_reason", ""),
                    "worker_snapshot_paths": json.dumps(snapshot_paths), **detail})
    return observations, hashes


def load_probes(manifest_path, mapping):
    """Bind translation-only diagnostics to captured worker formula bytes.

    Each manifest row explicitly links instance, worker_record, formula, diagnostic,
    arm, root, campaign, and regime. Snapshot stages never imply a terminal stop.
    """
    observations, hashes = [], {str(manifest_path): join.sha256_file(manifest_path)}
    for entry in table_rows(manifest_path):
        root = Path(entry["root"])
        if not root.is_absolute():
            root = manifest_path.parent / root
        name = entry["instance"]
        if name not in mapping:
            raise ValueError(f"unknown probe identity: {name}")
        paths = {key: evidence_path(root, entry[key]) for key in
                 ("worker_record", "formula", "diagnostic")}
        hashes.update({str(path): join.sha256_file(path) for path in paths.values()})
        worker = json.loads(paths["worker_record"].read_text())
        formula = paths["formula"].read_text().removesuffix("\n")
        arm = ":".join(worker.get(k, "") for k in
                       ("polarity", "transform", "requested_backend"))
        if worker.get("instance") != name or worker.get("worker_formula") != formula or \
                arm != entry["arm"]:
            raise ValueError(f"probe worker/formula binding mismatch: {name}")
        text = paths["diagnostic"].read_text()
        error = re.search(r"^translation_error=(.*)$", text, re.M)
        reason = ("translation-acceptance-set-limit" if error and
                  error[1].startswith("Too many acceptance sets used.") else
                  "translation-exception" if error else UNKNOWN)
        # The probe's parsed/simplified counts establish those stages only.
        counts = re.search(r"simplified promise_occurrences=(\d+) unique_promises=(\d+)", text)
        observations.append({"campaign": entry["campaign"], "instance": name,
            "arm": arm, "context": "standalone translation probe", "regime": entry["regime"],
            "first_terminal_obstruction": stop_category(reason), "terminal_reason": reason,
            "first_stop_category": stop_category(reason), "first_stop_reason": reason,
            "last_observed_stage": "translation exception" if reason != UNKNOWN else
                ("formula simplified" if counts else UNKNOWN),
            "unique_promises": counts[2] if counts else UNKNOWN,
            "translation_error": error[1] if error else UNKNOWN,
            "worker_snapshot_stage": worker.get("stage", UNKNOWN),
            **{key: str(path) for key, path in paths.items()},
            "limitation": "translation-only historical probe; no weakening attempt telemetry"})
    return observations, hashes


def standalone_boundary(missing, observations):
    """Historical within-cap successes are candidates, never matched race causation."""
    result = []
    for row in missing:
        solo = [e for e in observations if e["instance"] == row["instance"] and
                e["context"] == "standalone"]
        successes = [e for e in solo if e["result"] in SOLVED and
                     float(e["seconds"]) <= int(row["cap_s"]) and
                     int(e["evidence_cap_s"]) >= int(row["cap_s"])]
        failures = [e for e in solo if e["result"] not in SOLVED]
        result.append({"cap_s": row["cap_s"], "instance": row["instance"],
            "standalone_observations": len(solo), "standalone_nonsolves": len(failures),
            "within_cap_historical_solves": len(successes),
            "standalone_solving_arms": json.dumps(sorted({e["arm"] for e in successes})),
            "standalone_solve_sources": json.dumps([{k: e[k] for k in
                ("campaign", "arm", "row_path", "binary_sha256", "seconds", "regime")}
                for e in successes]),
            "pattern": "historical standalone solve / current race miss" if successes else
                ("standalone nonsolve observed" if failures else UNKNOWN),
            "race_only_interference_proven": UNKNOWN,
            "limitation": "unmatched binaries/runtime/routes; higher-cap rows are archival"})
    return result

def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("list", "tlsf-map", "manifest", "conversion", "corpus", "out"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    parser.add_argument("--series", action="append", required=True, metavar="CAP:ROLE=TSV")
    parser.add_argument("--evidence-manifest", type=Path)
    parser.add_argument("--probe-manifest", type=Path)
    parser.add_argument("--near-fraction", type=float, default=0.8)
    parser.add_argument("--sample-size", type=int, default=24)
    parser.add_argument("--seed", type=int, default=207)
    args = parser.parse_args(argv)
    if not 0 < args.near_fraction <= 1 or args.sample_size < 0:
        parser.error("near fraction must be in (0,1]; sample size must be nonnegative")
    instances = join.coverage.read_instance_list(args.list)
    if not instances or len(instances) != len(set(instances)):
        raise ValueError("empty list or duplicate logical identities")
    mapping = join.coverage.read_tlsf_map(args.tlsf_map)
    meta = metadata(instances, mapping, read_tsv(args.manifest, "instance"),
                    read_tsv(args.conversion, "instance"), args.corpus)
    series, inputs = {}, {}
    for value in args.series:
        label, path = join.split_assignment(value, "--series")
        cap_text, role = label.split(":")
        cap = int(cap_text)
        if cap <= 0 or role not in {"acacia", "comparator"} or (cap, role) in series:
            raise ValueError(f"invalid or duplicate series: {label}")
        loaded = join.load_series(label, path, cap, instances)
        series[cap, role] = loaded["rows"]
        inputs[label] = {k: v for k, v in loaded.items() if k != "rows"}
    # Opposite verdicts across caps are also a correctness conflict.
    for name in instances:
        observed_polarity([rows[name] for rows in series.values()])
    args.out.mkdir(parents=True, exist_ok=True)
    evidence_hashes = {}
    observations, probes = [], []
    verdicts = {name: [rows[name] for rows in series.values()] for name in instances}
    selected = {name for (cap, role), rows in series.items() if role == "acacia"
                for name in instances if rows[name]["result"] not in SOLVED and
                series[cap, "comparator"][name]["result"] in SOLVED}
    if args.evidence_manifest:
        observations, hashes = load_evidence(args.evidence_manifest, mapping, selected, verdicts)
        evidence_hashes.update(hashes)
        if observations:
            write_tsv(args.out / "missing-evidence-observations.tsv",
                      list(observations[0]), observations)
    if args.probe_manifest:
        probes, hashes = load_probes(args.probe_manifest, mapping)
        evidence_hashes.update(hashes)
        if probes:
            write_tsv(args.out / "translation-probe-observations.tsv", list(probes[0]), probes)
    boundary = []
    summary = []
    report = ["# Coverage classification", "",
              f"Near cap: either solved time >= {args.near_fraction:g} × cap.", "",
              "| Cap (s) | Acacia solved | Comparator solved | Comparator-only | "
              "Acacia-only | Both-unsolved | Common near cap |",
              "|---:|---:|---:|---:|---:|---:|---:|"]
    for cap in sorted({cap for cap, _ in series}):
        rows = classify(instances, meta, series[cap, "acacia"], series[cap, "comparator"],
                        cap, args.near_fraction)
        view = "derived" if any(inputs[f"{cap}:{role}"]["derived"]
                                    for role in ("acacia", "comparator")) else "measured"
        for row in rows:
            row["view"] = view
        counts = Counter(row["set"] for row in rows)
        report.append(f"| {cap} | {counts['common-solved'] + counts['Acacia-only']} | "
                      f"{counts['common-solved'] + counts['ltlsynt-only']} | "
                      f"{counts['ltlsynt-only']} | {counts['Acacia-only']} | "
                      f"{counts['both-unsolved']} | "
                      f"{sum(row['near_cap'] == 'true' for row in rows)} |")
        write_tsv(args.out / f"classification-{cap}s.tsv", list(rows[0]), rows)
        for kind in ("ltlsynt-only", "Acacia-only", "both-unsolved", "common-solved"):
            selected = [row for row in rows if row["set"] == kind and
                        (kind != "common-solved" or row["near_cap"] == "true")]
            filename = kind if kind != "common-solved" else "common-solved-near-cap"
            write_tsv(args.out / f"{filename}-{cap}s.tsv", list(rows[0]), selected)
        missing = [row for row in rows if row["set"] == "ltlsynt-only"]
        boundary.extend(standalone_boundary(missing, observations))
        splits = Counter((row["polarity"], row["declared_parameters"]) for row in missing)
        for polarity in ("REAL", "UNREAL", UNKNOWN):
            for declared in ("false", "true"):
                summary.append({"cap_s": cap, "polarity": polarity,
                                "declared_parameters": declared,
                                "missing": splits[polarity, declared]})
        sample = structural_sample([row for row in rows if row["set"] == "both-unsolved"],
                                   args.sample_size, args.seed)
        write_tsv(args.out / f"both-unsolved-sample-{cap}s.tsv",
                  [*rows[0], "sample_stratum", "sample_seed", "sample_rank"], sample)
    if boundary:
        write_tsv(args.out / "standalone-race-boundary.tsv", list(boundary[0]), boundary)
    write_tsv(args.out / "missing-split.tsv", list(summary[0]), summary)
    provenance = {"series": inputs, "near_fraction": args.near_fraction,
                  "sample_size": args.sample_size, "seed": args.seed,
                  "selection": ("round-robin floor(log2(bytes)), floor(log2(AP)), "
                                "parameter dimension"),
                  "evidence_inputs": evidence_hashes,
                  "arguments": argv if argv is not None else sys.argv[1:], "inputs": {str(path): join.sha256_file(path) for path in
                      (args.list, args.tlsf_map, args.manifest, args.conversion)}}
    (args.out / "classification-provenance.json").write_text(
        json.dumps(provenance, indent=2, sort_keys=True) + "\n")
    views = "; ".join(f"{cap} s " + ("derived" if any(inputs[f"{cap}:{role}"]["derived"]
                      for role in ("acacia", "comparator")) else "measured")
                      for cap in sorted({cap for cap, _ in series}))
    report.extend(["", f"Views: {views}.", "", "Polarity uses conclusive observations at each cap only. "
                   "Source hashes, source-map aliases and declared origins are checked. "
                   "Derived source hashes/rows are checked by the existing closing join.", "",
                   f"Structural sample: seed {args.seed}, up to {args.sample_size} per cap; "
                   "round-robin log2(bytes)/log2(AP)/parameter-dimension strata. "
                   "Logical IDs order ties before seeded shuffling; verdicts do not select cases.",
                   "", "Missing polarity/parameter counts: [TSV](missing-split.tsv). "
                   "Provenance: [JSON](classification-provenance.json).", ""])
    (args.out / "coverage-report.md").write_text("\n".join(report))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
