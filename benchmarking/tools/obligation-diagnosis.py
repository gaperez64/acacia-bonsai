#!/usr/bin/env python3
"""Offline obligation evidence joins, structural captures and censored stage tables.

This tool does not run synthesis or time invocations. The driver supplies measured rows.
"""
from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path
import subprocess

SOLVED = {"REALIZABLE", "UNREALIZABLE"}


def read_rows(path):
    with Path(path).open() as stream:
        return list(csv.DictReader(stream, delimiter="," if Path(path).suffix == ".csv" else "\t"))


def write_rows(path, rows, fields=None):
    rows = list(rows)
    fields = fields or list(rows[0])
    with Path(path).open("w") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def records(directory):
    result = []
    for path in sorted(Path(directory).glob("*.jsonl")):
        for index, line in enumerate(path.read_text().splitlines(), 1):
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                result.append({"event": "invalid_record", "evidence": f"{path}:{index}"})
                continue
            row["evidence"] = f"{path}:{index}"
            result.append(row)
    return result


def invocation_dir(phases, label, cap, instance, run_index=None):
    base = Path(phases) / label / str(cap)
    matches = list(base.glob(instance + "-*"))
    if run_index is not None:
        matches = [p for p in matches if p.name == f"{instance}-{run_index}"]
    if len(matches) != 1:
        return None
    return matches[0]


def summarize(rows):
    def events(name):
        return sorted((r for r in rows if r.get("event") == name),
                      key=lambda r: r.get("mono_ns", 0))

    memory = events("helper_memory_share")
    origin = events("invocation_start")
    spawn = events("worker_spawn")
    anchor = origin[0]["mono_ns"] if origin else None
    proxy = spawn[0]["mono_ns"] if spawn else None
    published = events("obligation_eligibility_published")
    observed = events("obligation_eligibility")
    start = events("obligation_start")
    completion = events("obligation_completion")
    winner = events("parent_winner")
    helper_pid = start[0].get("worker_pid") if start else None
    helper_terminals = [r for r in events("parent_terminal")
                        if r.get("worker_pid") == helper_pid]
    calls = [r for r in rows if r.get("stage") == "obligation_call"
             and r.get("worker_pid") == helper_pid]
    entry = next((r for r in calls if r.get("event") == "stage_entry"), None)
    returned = next((r for r in calls if r.get("event") == "stage_completion"), None)
    stopped = next((r for r in calls if r.get("event") == "stage_stopped"), None)
    terminal = helper_terminals[0] if helper_terminals else None
    eligible = observed[0] if observed else None
    bound = completion[0] if completion else terminal

    def offset(row):
        return (row["mono_ns"] - anchor) / 1e9 if row and anchor is not None else ""

    def duration(stage):
        return next((r.get("wall_ns", 0) / 1e9 for r in rows
                     if r.get("event") == "stage_completion" and r.get("stage") == stage
                     and r.get("worker_pid") == helper_pid), "")

    return {
        "invocation_start_ns": anchor if anchor is not None else "",
        "start_missing_reason": "" if anchor is not None else "no invocation anchor in old binary",
        "eligibility_published_s": offset(published[0] if published else None),
        "eligibility_observed_s": offset(eligible),
        "eligibility_from_first_spawn_proxy_s": ((eligible["mono_ns"] - proxy) / 1e9
                                                 if eligible and proxy is not None else ""),
        "helper_started": True if start else "unknown", "helper_fork_s": offset(start[0] if start else None),
        "preparation_s": duration("obligation_preparation"),
        "simplification_s": duration("simplification"),
        "call_entry_s": offset(entry), "call_return_s": offset(returned),
        "call_completed_s": (returned["mono_ns"] - entry["mono_ns"]) / 1e9
                            if entry and returned else "",
        "call_censored_elapsed_to_terminal_s": (bound["mono_ns"] - entry["mono_ns"]) / 1e9
                                       if entry and not returned and bound else "",
        "call_status": "completed" if returned else "stopped" if stopped else
                       "censored" if entry else "unknown",
        "check_status": "unknown: bind contract to source revision",
        "helper_outcome": completion[0].get("outcome", "") if completion else "unknown",
        "helper_reason": completion[0].get("reason", "") if completion else "unknown",
        "helper_memory_policy": memory[0].get("policy", "unknown") if memory else "unknown",
        "helper_memory_workers": memory[0].get("workers", "") if memory else "",
        "helper_rss_share_bytes": memory[0].get("rss_share_bytes", "") if memory else "",
        "helper_wall_at_completion_s": completion[0]["wall_ns"] / 1e9
                                       if completion and "wall_ns" in completion[0] else "",
        "terminal_stage": terminal.get("stage", "") if terminal else "unknown",
        "accepted_backend": winner[0].get("effective_backend", "") if winner else "unknown",
        "accepted_s": offset(winner[0] if winner else None),
        "telemetry_status": "incomplete" if any(r.get("event") == "invalid_record" or
                                                 r.get("dropped_records", 0) or
                                                 r.get("failed_records", 0) or
                                                 r.get("incomplete_packet", False)
                                                 for r in rows) else "no reported drops",
        "winner_evidence": winner[0]["evidence"] if winner else "",
        "helper_evidence": completion[0]["evidence"] if completion else "",
    }


def select_outcome(at17, at60, fallback):
    if at60 is not None:
        return at60, "PMC5 measured 60s"
    if at17 is not None and at17["result"] in SOLVED:
        return at17, "PMC5 completed 17s observation carried to 60s; no new measurement"
    return fallback, "PMC4 historical fallback; no PMC5 row"


def audit(args):
    mapping = {r["instance"]: r["tlsf"] for r in read_rows(args.tlsf_map)}
    listing = [x for x in args.list.read_text().splitlines() if x and not x.startswith("#")]
    args.out.mkdir(parents=True, exist_ok=True)
    captures = args.out / "captures"
    captures.mkdir(exist_ok=True)
    old = set(args.old_list.read_text().split())
    rows = []
    for instance in listing:
        try:
            process = subprocess.run([str(args.tool.resolve()), "--tlsf",
                                      str(args.corpus / mapping[instance])],
                                     capture_output=True, text=True, timeout=10, check=True)
            capture = json.loads(process.stdout)
            (captures / (instance + ".json")).write_text(process.stdout)
            row = {"instance": instance, "eligible": str(capture["eligible"]).lower(),
                   "old_eligible": str(instance in old).lower(), "reason": "",
                   "dag_nodes": capture["dag_nodes"], "ap_count": capture["ap_count"],
                   "environment_count": len(capture["inputs"]),
                   "controlled_count": len(capture["outputs"])}
        except (subprocess.SubprocessError, ValueError, KeyError) as error:
            row = {"instance": instance, "eligible": "unknown", "old_eligible":
                   str(instance in old).lower(), "reason": str(error), "dag_nodes": "",
                   "ap_count": "", "environment_count": "", "controlled_count": ""}
        rows.append(row)
    write_rows(args.out / "eligibility.tsv", rows)
    changed = [r for r in rows if r["eligible"] != r["old_eligible"]]
    write_rows(args.out / "eligibility-changes.tsv", changed, list(rows[0]))
    print(json.dumps({"inputs": len(rows), "eligible": sum(r["eligible"] == "true" for r in rows),
                      "changes_or_unknown": len(changed)}))


def panel(args):
    root = args.restore
    trust = root / "obligation-trust20261013/build_scratch/sweep-close/evidence-input/_bm-logs.issues"
    closing = root / "postmem20261012-closing/build_scratch/cov/wt-pm-p0/_bm-logs.pm-final/join-60"
    p2 = root / "postmem20261011-p2/_bm-logs.postmem/p2-oblig"
    at17 = {r["instance"]: r for r in read_rows(trust / "i248-s17-obl/pmc5.tsv")}
    at60 = {r["instance"]: r for r in read_rows(trust / "i248-s60-obl/pmc5.tsv")}
    pmc4 = {r["instance"]: r for r in read_rows(closing / "candidate-60s-scored.csv")}
    reference = {r["instance"]: r for r in read_rows(closing / "ltlsynt216-60s-scored.csv")}
    old_incumbent = {r["instance"]: r for r in read_rows(closing / "incumbent-60s-scored.csv")}
    gains = {name for name, row in pmc4.items() if row["result"] in SOLVED
             and old_incumbent[name]["result"] not in SOLVED}
    old17 = {r["instance"]: r for r in read_rows(trust / "i248-s17-obl/pmc4.tsv")}
    eligibles = [r for r in read_rows(args.eligibility)
                 if r["eligible"] == "true" or r["old_eligible"] == "true"]
    missing_current = {r["instance"] for r in eligibles if r["instance"] not in at17}
    historical_phases = {}
    if missing_current:
        csv.field_size_limit(16 * 1024 * 1024)
        with (closing / "candidate-attribution.tsv").open() as stream:
            for row in csv.DictReader((line for line in stream if not line.startswith("#")),
                                      delimiter="\t"):
                if row["instance"] in missing_current:
                    historical_phases[row["instance"]] = row
    route_rows = read_rows(p2 / "rows.tsv")
    result = []
    for audit_row in eligibles:
        instance = audit_row["instance"]
        chosen, provenance = select_outcome(at17.get(instance), at60.get(instance), pmc4[instance])
        measured_cap = chosen.get("cap_s", chosen.get("source_cap_s"))
        phase_dir = invocation_dir(trust / f"i248-s{measured_cap}-obl/phases", "pmc5",
                                   measured_cap, instance, chosen.get("run_index"))
        historical = historical_phases.get(instance)
        if phase_dir is None and historical:
            original = historical["phase_directory"]
            suffix = original.split("/acacia-bonsai/", 1)[-1]
            phase_dir = root / "postmem20261012-closing" / suffix
        phase = summarize(records(phase_dir)) if phase_dir else summarize([])
        if historical:
            phase["telemetry_status"] = historical["delivery"] + ";" + historical["producer"]
        solved = chosen["result"] in SOLVED
        reasons = []
        if not solved:
            reasons.append("ltlsynt_only" if reference[instance]["result"] in SOLVED else
                           "eligible_both_unsolved")
        if solved and phase["accepted_backend"] not in {"obligation", "unknown"}:
            reasons.append("ordinary_accepted")
            reasons.append("helper_reported_completed_candidate" if phase["helper_outcome"] in
                           {"completed_REAL", "completed_UNREAL"} else
                           "ordinary_only_observed; obligation capability censored")
        ordinary = [r for r in route_rows if r["instance"] == instance and
                    r["config"].startswith("obl0-") and r["result"] in SOLVED]
        obligation = [r for r in route_rows if r["instance"] == instance and
                      r["config"].startswith("obl1-") and r["result"] not in SOLVED]
        if ordinary and obligation:
            reasons.append("archived_ordinary_route_decides_where_obligation_route_stops")
        if instance in gains:
            reasons.append("historical_obligation_gain_preservation")
        if solved and instance in old17 and old17[instance]["result"] not in SOLVED:
            reasons.append("PMC5_regained_at_17s_preservation")
        row = {"instance": instance, "role": ";".join(reasons) or "obligation_control_candidate",
               "eligible": audit_row["eligible"], "eligibility_reason": audit_row["reason"],
               "old_eligible": audit_row["old_eligible"], "result": chosen["result"], "seconds": chosen["seconds"],
               "source_cap_s": measured_cap, "source_revision": chosen.get("acacia_sha", "188cef33"),
               "binary_sha256": chosen.get("binary_sha256", historical["binary_sha256"]
                                            if historical else "see PMC4 archive"),
               "outcome_provenance": provenance, "ltlsynt_result": reference[instance]["result"],
               "ltlsynt_seconds": reference[instance]["seconds"],
               "ltlsynt_provenance": reference[instance]["provenance"],
               "pmc4_result": pmc4[instance]["result"], "phase_directory": str(phase_dir or ""),
               "dag_nodes": audit_row["dag_nodes"], "ap_count": audit_row["ap_count"],
               "environment_count": audit_row["environment_count"],
               "controlled_count": audit_row["controlled_count"], **phase}
        result.append(row)
    args.out.mkdir(parents=True, exist_ok=True)
    for row in result:
        if row["outcome_provenance"].startswith("PMC5"):
            row["check_status"] = "absent: PMC5 trusted exact decision mode"
        else:
            row["check_status"] = "PMC4 REAL check grouped in synthesis; not separable in archive"
    write_rows(args.out / "eligible-outcomes.tsv", result)
    targets = [r for r in result if r["role"] != "obligation_control_candidate"]
    controls = sorted((r for r in result if r["role"] == "obligation_control_candidate"
                       and r["eligible"] == "true"),
                      key=lambda r: r["instance"])[:5]
    for row in controls:
        row["role"] = "ordinary_obligation_control"
    write_rows(args.out / "panel.tsv", targets + controls)
    for name, rows in [("panel", targets + controls),
                       ("residuals", [r for r in result if r["result"] not in SOLVED]),
                       ("ltlsynt-only", [r for r in result if r["role"].startswith("ltlsynt_only")]),
                       ("ordinary-winners", [r for r in result if "ordinary_accepted" in r["role"]]),
                       ("ordinary-only-observed", [r for r in result
                                                   if "ordinary_only_observed" in r["role"]]),
                       ("eligible", result)]:
        (args.out / f"{name}.list").write_text("".join(r["instance"] + "\n" for r in rows))
    residual_panel = [r for r in result if r["result"] not in SOLVED or
                      "historical_obligation_gain_preservation" in r["role"] or
                      "PMC5_regained_at_17s_preservation" in r["role"]] + controls
    (args.out / "residual-panel.list").write_text(
        "".join(r["instance"] + "\n" for r in residual_panel))
    print(json.dumps({"eligible": len(result), "panel": len(targets) + len(controls),
                      "ltlsynt_only": sum(r["role"].startswith("ltlsynt_only") for r in result),
                      "ordinary_winners": sum("ordinary_accepted" in r["role"] for r in result),
                      "controls": [r["instance"] for r in controls]}))


def stages(args):
    output = []
    for path in args.runs:
        for run in read_rows(path):
            cap = run.get("cap_s", "")
            directory = invocation_dir(args.phases, run["solver_label"], cap, run["instance"],
                                       run.get("run_index"))
            output.append({"instance": run["instance"], "solver_label": run["solver_label"],
                           "cap_s": cap, "result": run["result"], "seconds": run["seconds"],
                           "phase_directory": str(directory or ""),
                           **summarize(records(directory) if directory else [])})
    write_rows(args.out, output)


def compare(args):
    import spot
    result = []
    for name in args.list.read_text().split():
        capture = json.loads((args.captures / (name + ".json")).read_text())
        objective = spot.formula(capture["objective"])
        raw = spot.formula(capture["normalized_formula"])
        row = {"instance": name, "objective_equal": "unknown", "components": "",
               "mapping": "", "raw_equal": "unknown", "ltlsynt_api_output_set": "unknown",
               "arm_api_outputs": json.dumps(capture["outputs"]),
               "registration_policy_comparison": "unknown", "component_objectives": "",
               "evidence": "no archived trace"}
        trace_path = args.traces / (name + ".obl1-byp1.err")
        if not trace_path.exists():
            result.append(row)
            continue
        trace = trace_path.read_text()
        new = [line.removeprefix("new formula: ") for line in trace.splitlines()
               if line.startswith("new formula: ")]
        components = [line for line in trace.splitlines() if line.startswith("there are ")]
        mappings = [line.strip() for line in trace.splitlines() if " := " in line]
        working = [line.removeprefix("working on subformula ") for line in trace.splitlines()
                   if line.startswith("working on subformula ")]
        # The fresh reference driver binds the exact original supplied formula too.
        original_path = args.traces / (name + ".normalized.ltl")
        original = spot.formula(original_path.read_text()) if original_path.exists() else None
        ltlsynt = spot.formula(new[-1]) if new else original
        single = components == ["there are 1 subformulas"]
        if ltlsynt is not None and single:
            row["objective_equal"] = objective == ltlsynt
            row["raw_equal"] = raw == ltlsynt
            removed = {line.split(" := ", 1)[0] for line in mappings}
            # Single-component ltlsynt drops globally mapped outputs from the API vector.
            row["ltlsynt_api_output_set"] = json.dumps(sorted(ap for ap in capture["outputs"]
                                                              if ap not in removed))
            if objective == ltlsynt and capture["effective_timing"] == "Mealy":
                row["registration_policy_comparison"] = "same policy: traversal inputs before outputs; actual cross-process indices unobserved"
        row.update(components=";".join(components), mapping=";".join(mappings),
                   component_objectives=json.dumps(working), evidence=str(trace_path))
        result.append(row)
    write_rows(args.out, result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    command = sub.add_parser("audit")
    for name in ("tool", "tlsf-map", "corpus", "list", "old-list", "out"):
        command.add_argument("--" + name, type=Path, required=True)
    command.set_defaults(action=audit)
    command = sub.add_parser("panel")
    for name in ("restore", "eligibility", "out"):
        command.add_argument("--" + name, type=Path, required=True)
    command.set_defaults(action=panel)
    command = sub.add_parser("stages")
    command.add_argument("--runs", type=Path, nargs="+", required=True)
    command.add_argument("--phases", type=Path, required=True)
    command.add_argument("--out", type=Path, required=True)
    command.set_defaults(action=stages)
    command = sub.add_parser("compare")
    for name in ("list", "captures", "traces", "out"):
        command.add_argument("--" + name, type=Path, required=True)
    command.set_defaults(action=compare)
    args = parser.parse_args()
    args.action(args)


if __name__ == "__main__":
    main()
