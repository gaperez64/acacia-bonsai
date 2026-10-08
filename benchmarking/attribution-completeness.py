#!/usr/bin/env python3
"""Audit delivery and producer completeness of coverage-runner phase records.

Read-only inputs; no solves, timing runs, or cap-reuse eligibility claims. Each TSV
row is joined by the runner's label/cap/instance/run_index directory convention.
"""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import csv
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import stat
import sys


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


ROOT = Path(__file__).resolve().parents[1]
WORKER = load_module("attribution_worker_phases", ROOT / "benchmarking/summarize-worker-phases.py")
DIAG = load_module("attribution_diag_phases", ROOT / "benchmarking/summarize-diag-phases.py")
CONFIG = load_module("attribution_config", ROOT / "scripts/acacia-config.py")
PARENT_EVENTS = {"worker_spawn", "worker_spec", "parent_terminal", "parent_winner",
                 "stage_censored"}
CONTEXT = ("requested_backend", "effective_backend", "original_polarity", "proof_polarity",
           "route", "stage", "reason", "r_prepass", "equivariance")
SOLVED = {"REALIZABLE": 0, "UNREALIZABLE": 1}
BOOLEAN_CONTEXT = ("r_prepass", "equivariance")
SPEC_FIELDS = ("kind", "requested_polarity", "translation", "transform", "provider")
# Global reader memory bound, independent of campaign results or solver thresholds.
MAX_RECORD_FILE_BYTES = 64 * 1024 * 1024


def location(record):
    return f"{record['_path']}:{record['_line']}"


def project(record):
    fields = ("event", "worker", "worker_pid", "seq", *CONTEXT, "exit_code", "signal",
              "telemetry", "dropped_records", "mono_ns", "kind", "requested_polarity",
              "translation", "transform", "provider")
    return {**{field: record[field] for field in fields if field in record},
            "evidence": location(record)}


def nonnegative_int(value):
    return type(value) is int and value >= 0


def phase_directory(root, row):
    def safe(value):
        return re.sub(r"[^A-Za-z0-9_.-]", "_", value)

    return root / safe(row["solver_label"]) / row["cap_s"] / (
        f"{safe(row['instance'])}-{row['run_index']}")


def flag_value(flags, name, default=None):
    value = default
    for index, flag in enumerate(flags):
        if flag == name:
            if index + 1 == len(flags):
                raise ValueError(f"{name} in campaign flags has no value")
            value = flags[index + 1]
        elif flag.startswith(name + "="):
            value = flag.split("=", 1)[1]
    return value


def expected_configuration(row):
    flags = shlex.split(row.get("flags", ""))
    options, presets = CONFIG.load_registry()
    values = None
    if row.get("preset") in presets["presets"] or row.get("preset") in presets.get("aliases", {}):
        values = CONFIG.normalize_preset(options, presets, row["preset"])
    context = {"r_prepass": True,
               "equivariance": values["enable_equivariant_solver"] if values else None}
    for field in BOOLEAN_CONTEXT:
        value = flag_value(flags, "--" + field.replace("_", "-"))
        if value is not None:
            if value not in ("on", "off"):
                raise ValueError(f"invalid --{field.replace('_', '-')}={value!r}")
            context[field] = value == "on"
    arms = flag_value(flags, "--arms", values["default_arms"] if values else None)
    if arms is None or arms == "":
        return None, context
    inventory = []
    for arm in arms.split(","):
        parts = arm.strip().split(":")
        if len(parts) not in (3, 4):
            raise ValueError(f"invalid arm specification {arm!r}")
        polarity, transform, backend = parts[:3]
        if polarity not in ("real", "unreal", "both"):
            raise ValueError(f"invalid arm polarity {arm!r}")
        native = transform in ("gr1", "gr1-lift", "gr1-real-lift", "param-lift")
        if native:
            if backend != "oxidd" or len(parts) != 3 or (
                    transform in ("gr1-lift", "gr1-real-lift") and polarity != "both") or (
                    transform == "param-lift" and polarity != "real"):
                raise ValueError(f"invalid native arm {arm!r}")
            specification = dict(kind=arm.strip(), translation="native", transform="exact",
                                 provider="native")
        else:
            transforms = {"real": ("small", "any"), "unreal": ("formula", "automaton")}
            if transform not in transforms.get(polarity, ()) or backend not in (
                    "backward", "forward", "spot-guarded", "spot-guarded-sparse"):
                raise ValueError(f"invalid legacy arm {arm!r}")
            provider = parts[3] if len(parts) == 4 else flag_value(
                flags, f"--{polarity}-provider", "frozen-graph")
            if provider not in ("frozen-graph", "spot-lazy", "spot-eager", "closure-buchi",
                                "closure-buchi-eager"):
                raise ValueError(f"invalid arm provider {arm!r}")
            translation = transform if polarity == "real" else (
                values["translation_pref"].split("+", 1)[0] if values else None)
            specification = dict(kind="legacy", translation=translation,
                                 transform="real" if polarity == "real" else transform,
                                 provider=provider)
        specification.update(requested_polarity="both" if polarity == "both" else polarity.upper(),
                             requested_backend=backend, **context)
        inventory.append(specification)
    return inventory, context


def expected_worker_count(row):
    inventory, _ = expected_configuration(row)
    return len(inventory) if inventory is not None else None


def audit_row(row, directory, expected_workers=None):
    issues = []
    inventory, requested_context = expected_configuration(row)

    def issue(dimension, reason, evidence):
        item = {"dimension": dimension, "reason": reason, "evidence": str(evidence)}
        if item not in issues:
            issues.append(item)

    records = []
    if not directory.is_dir():
        issue("delivery", "missing_phase_directory", directory)
    for path in sorted(directory.glob("*.jsonl")):
        try:
            emitter = int(path.stem)
            if emitter <= 0:
                raise ValueError("emitter PID must be positive")
            before = path.lstat()
            if not stat.S_ISREG(before.st_mode):
                issue("delivery", "non_regular_record_file", path)
                continue
            if before.st_size > MAX_RECORD_FILE_BYTES:
                issue("delivery", "record_file_too_large", path)
                continue
            # Nonblocking/no-follow open and fstat also cover replacement after lstat.
            descriptor = os.open(path, os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
            with os.fdopen(descriptor, "rb") as stream:
                opened = os.fstat(stream.fileno())
                if not stat.S_ISREG(opened.st_mode):
                    issue("delivery", "non_regular_record_file", path)
                    continue
                if (before.st_dev, before.st_ino) != (opened.st_dev, opened.st_ino):
                    issue("delivery", "record_file_changed", path)
                    continue
                if opened.st_size > MAX_RECORD_FILE_BYTES:
                    issue("delivery", "record_file_too_large", path)
                    continue
                data = stream.read(MAX_RECORD_FILE_BYTES + 1)
            if len(data) > MAX_RECORD_FILE_BYTES:
                issue("delivery", "record_file_too_large", path)
                continue
            lines = data.splitlines(keepends=True)
        except (OSError, ValueError) as error:
            issue("delivery", "unreadable_record_file", f"{path}: {error}")
            continue
        for index, line in enumerate(lines, 1):
            source = f"{path}:{index}"
            if not line.endswith(b"\n"):
                issue("delivery", "truncated_trailing_line", source)
                continue
            try:
                record = json.loads(line)
                if not isinstance(record, dict):
                    raise ValueError("expected a JSON object")
            except (UnicodeError, ValueError) as error:
                issue("delivery", "malformed_json", f"{source}: {error}")
                continue
            record.update(_emitter_pid=emitter, _path=str(path), _line=index)
            records.append(record)
            if "dropped_records" in record:
                drops = record["dropped_records"]
                if not nonnegative_int(drops):
                    issue("delivery", "invalid_drop_count", source)
                    issue("producer", "invalid_drop_count", source)
                elif drops:
                    issue("delivery", "producer_drops", f"{source}: dropped_records={drops}")
                    issue("producer", "producer_drops", f"{source}: dropped_records={drops}")

    writers = [r for r in records if r.get("event") == "writer_summary"]
    if len(writers) != 1:
        issue("delivery", "missing_writer_summary" if not writers else "duplicate_writer_summary",
              directory)
    for writer in writers:
        source = location(writer)
        failed = writer.get("failed_records")
        if not nonnegative_int(failed):
            issue("delivery", "invalid_writer_summary", source)
        elif failed:
            issue("delivery", "writer_failed_records", f"{source}: failed_records={failed}")
        if writer.get("incomplete_packet") is not False:
            issue("delivery", "writer_incomplete_packet", source)
        delivered = writer.get("delivered_records")
        if not nonnegative_int(delivered) or delivered != len(records) - len(writers):
            issue("delivery", "delivered_count_mismatch",
                  f"{source}: delivered_records={delivered}, "
                  f"confirmed={len(records)-len(writers)}")

    workers = defaultdict(list)
    worker_indices = {r['worker_pid']: r['worker'] for r in records
                      if nonnegative_int(r.get('worker_pid')) and nonnegative_int(r.get('worker'))}
    parents = set()
    lifecycle = []
    for record in records:
        event = record.get("event")
        if event is None or event == "writer_summary":
            continue
        if not isinstance(event, str):
            issue("delivery", "invalid_event", location(record))
            continue
        stage_record = event in {'stage_entry', 'stage_completion', 'stage_stopped',
                                 'stage_metric', 'stage_censored'}
        supplemental = stage_record or event.startswith('weakening_')
        if supplemental and 'worker' not in record:
            record['worker'] = worker_indices.get(record.get('worker_pid'))
        pid, index = record.get("worker_pid"), record.get("worker")
        if not nonnegative_int(pid) or pid == 0 or not nonnegative_int(index):
            issue("delivery", "invalid_worker_identity", location(record))
            continue
        workers[pid].append(record)
        lifecycle.append(record)
        parent_observation = event in PARENT_EVENTS or (
            event.startswith('weakening_') and record.get('observer') == 'parent')
        if parent_observation:
            parents.add(record["_emitter_pid"])
            if record["_emitter_pid"] == pid:
                issue("delivery", "wrong_parent_emitter", location(record))
        elif record["_emitter_pid"] != pid:
            issue("delivery", "wrong_child_emitter", location(record))
        if supplemental:
            if not nonnegative_int(record.get("seq")) or record["seq"] == 0:
                issue("delivery", "invalid_sequence", location(record))
            if not nonnegative_int(record.get("mono_ns")):
                issue("delivery", "invalid_event_context", location(record))
            if not nonnegative_int(record.get("dropped_records")):
                issue("delivery", "invalid_drop_count", location(record))
            if stage_record:
                valid = nonnegative_int(record.get('stage_id')) and record.get('stage_id', 0) > 0
                if event == 'stage_metric':
                    valid &= all(isinstance(record.get(k), str) for k in ('key', 'value'))
                else:
                    valid &= isinstance(record.get('stage'), str)
                if not valid:
                    issue('delivery', 'invalid_stage_record', location(record))
            continue
        if event != "worker_spec":
            if not nonnegative_int(record.get("seq")) or record["seq"] == 0:
                issue("delivery", "invalid_sequence", location(record))
            if any(field not in record for field in (*CONTEXT, "dropped_records", "mono_ns")):
                issue("delivery", "missing_event_context", location(record))
            if any(not isinstance(record.get(field), str) for field in CONTEXT[:7]):
                issue("delivery", "invalid_event_context", location(record))
            if not nonnegative_int(record.get("mono_ns")):
                issue("delivery", "invalid_event_context", location(record))
        context_type = "spec" if event == "worker_spec" else "event"
        if any(field not in record for field in BOOLEAN_CONTEXT):
            issue("delivery", f"missing_{context_type}_context", location(record))
        if any(type(record.get(field)) is not bool for field in BOOLEAN_CONTEXT):
            issue("delivery", f"invalid_{context_type}_context", location(record))
        expected = inventory[index] if inventory is not None and index < len(inventory) else None
        context = expected or requested_context
        fields = ((*BOOLEAN_CONTEXT, "requested_backend")
                  if expected and event != "worker_spec" else BOOLEAN_CONTEXT)
        for field in fields:
            value = context.get(field)
            if value is not None and record.get(field) != value:
                issue("delivery", "worker_context_mismatch",
                      f"{location(record)}: worker={index}, {field} expected={value!r}, "
                      f"recorded={record.get(field)!r}")
        if expected and event in ("worker_spawn", "worker_start") and (
                record.get("original_polarity") != expected["requested_polarity"]):
            issue("delivery", "worker_context_mismatch",
                  f"{location(record)}: worker={index}, expected polarity="
                  f"{expected['requested_polarity']!r}")

    parents.update(r["_emitter_pid"] for r in records
                   if r.get("arm") == "legacy_parent")
    if len(parents) != 1:
        issue("delivery", "missing_parent_identity" if not parents else "multiple_parents",
              directory)
    for parent in parents:
        summaries = [r for r in records if r["_emitter_pid"] == parent
                     and r.get("phase") == "record_summary"]
        if len(summaries) != 1:
            issue("delivery", "missing_parent_record_summary" if not summaries
                  else "duplicate_parent_record_summary", f"{directory}/{parent}.jsonl")
        elif not nonnegative_int(summaries[0].get("dropped_records")):
            issue("delivery", "invalid_drop_count", location(summaries[0]))
            issue("producer", "invalid_drop_count", location(summaries[0]))

    indices = [group[0]["worker"] for group in workers.values()]
    count = expected_workers if expected_workers is not None else (
        len(inventory) if inventory is not None else None)
    if count is None:
        count = max(indices) + 1 if indices else 0
    if not count or sorted(indices) != list(range(count)):
        issue("delivery", "worker_inventory_mismatch",
              f"{directory}: expected indices 0..{count-1}, observed={sorted(indices)}")
        issue("producer", "worker_inventory_unknown", directory)
    details = []
    for pid, group in sorted(workers.items(), key=lambda item: item[1][0]["worker"]):
        by_event = defaultdict(list)
        for record in group:
            by_event[record["event"]].append(record)
        child = [r for r in group if r["_emitter_pid"] == pid and r["event"] not in PARENT_EVENTS
                 and r.get('observer') != 'parent']
        child.sort(key=lambda r: r["_line"])
        terminals = by_event["parent_terminal"]
        terminal = terminals[0] if len(terminals) == 1 else None
        source = location(terminal) if terminal else f"{directory}: worker_pid={pid}"
        for event in ("worker_spawn", "worker_spec", "worker_start", "parent_terminal"):
            events = by_event[event]
            if len(events) != 1:
                issue("delivery", f"missing_{event}" if not events else f"duplicate_{event}",
                      source)
        if any(r["worker"] != group[0]["worker"] for r in group):
            issue("delivery", "worker_index_mismatch", source)
        for spec in by_event["worker_spec"]:
            if any(not isinstance(spec.get(field), str) or not spec[field]
                   for field in SPEC_FIELDS):
                issue("delivery", "invalid_worker_spec", location(spec))
            index = spec["worker"]
            if inventory is not None and index < len(inventory):
                for field in SPEC_FIELDS:
                    expected = inventory[index][field]
                    if expected is not None and spec.get(field) != expected:
                        issue("delivery", "worker_spec_mismatch",
                              f"{location(spec)}: worker={index}, {field} expected={expected!r}, "
                              f"recorded={spec.get(field)!r}")
            for record in group:
                for field in BOOLEAN_CONTEXT:
                    if type(spec.get(field)) is bool and type(record.get(field)) is bool and (
                            record[field] != spec[field]):
                        issue("delivery", "worker_context_mismatch",
                              f"{location(record)}: {field} disagrees with {location(spec)}")
        sequences = [r.get("seq") for r in child]
        if sequences != list(range(1, len(child) + 1)):
            issue("delivery", "sequence_gap", f"{directory}/{pid}.jsonl: seq={sequences}")
        # Spawn uses a pre-fork copy. Terminal resumes the reaped child's shared
        # sequence; comparing adjacent parent-file records would invent gaps.
        if terminal and terminal.get("telemetry") != "unavailable":
            last = sequences[-1] if sequences and nonnegative_int(sequences[-1]) else 0
            snapshots = [r for r in group if r['event'] == 'stage_censored' or
                         r['event'].startswith('weakening_') and r.get('observer') == 'parent']
            snapshots.sort(key=lambda r: r['_line'])
            for offset, snapshot in enumerate(snapshots, 1):
                if snapshot.get('seq') != last + offset:
                    issue('delivery', 'sequence_gap', location(snapshot))
            if terminal.get("seq") != last + len(snapshots) + 1:
                issue("delivery", "sequence_gap",
                      f"{source}: parent seq={terminal.get('seq')}, last child seq={last}")
        for winner in by_event["parent_winner"]:
            if terminal and nonnegative_int(terminal.get("seq")):
                if winner.get("seq") != terminal["seq"] + 1:
                    issue("delivery", "sequence_gap", location(winner))
        telemetry = terminal.get("telemetry", "unknown") if terminal else "unknown"
        if not isinstance(telemetry, str):
            issue("delivery", "invalid_telemetry", source)
            telemetry = "unknown"
        if telemetry != "complete":
            reason = {"incomplete": "worker_final_event_not_emitted", "dropped": "producer_drops",
                      "unavailable": "producer_metadata_unavailable"}.get(
                          telemetry, "missing_parent_terminal" if not terminal
                          else "invalid_telemetry")
            issue("producer", reason, source)
        final = by_event["terminal_result"]
        if telemetry == "complete":
            if len(final) != 1:
                issue("delivery", "missing_terminal_result" if not final
                      else "duplicate_terminal_result", source)
            summaries = [r for r in records if r["_emitter_pid"] == pid
                         and r.get("phase") == "record_summary"]
            if len(summaries) != 1:
                issue("delivery", "missing_worker_record_summary" if not summaries
                      else "duplicate_worker_record_summary", source)
            elif not nonnegative_int(summaries[0].get("dropped_records")):
                issue("delivery", "invalid_drop_count", location(summaries[0]))
                issue("producer", "invalid_drop_count", location(summaries[0]))
        elif telemetry == "dropped":
            issue("delivery", "producer_drops", source)
        elif telemetry == "incomplete" and final:
            issue("delivery", "terminal_telemetry_mismatch", source)
        for index, record in enumerate(child):
            if record["event"] == "route_selected":
                following = child[index + 1] if index + 1 < len(child) else None
                if not following or following["event"] != "route_start" or (
                        following.get("route") != record.get("route")):
                    # A kill can occur between selection and start; no missing
                    # delivery is established if this is the confirmed prefix.
                    if following or telemetry == "complete":
                        issue("delivery", "missing_route_start", location(record))
        if telemetry == "complete" and not by_event["route_selected"]:
            issue("delivery", "missing_route_selected", source)
        case = "unknown"
        if terminal:
            if telemetry == "complete":
                case = "finished"
            elif nonnegative_int(terminal.get("signal")) and terminal["signal"] > 0 and (
                    terminal.get("reason") in (
                    "deadline", "winner_cancelled", "interrupted")):
                case = "documented_kill"
            else:
                case = "unexplained_incomplete"
        latest = terminal or (child[-1] if child else by_event["worker_spawn"][0]
                              if by_event["worker_spawn"] else group[0])
        stops = [project(r) for r in child if r["event"] == "route_stopped"]
        if terminal and terminal.get("reason") != "exit":
            stops.append(project(terminal))
        details.append({"worker": group[0]["worker"], "worker_pid": pid,
                        "telemetry": telemetry, "producer_case": case,
                        "parent_only": not child, "last_observed": project(latest),
                        "spec": [project(r) for r in by_event["worker_spec"]],
                        "parent_terminal": project(terminal) if terminal else None,
                        "routes": [project(r) for r in child
                                   if r["event"] in ("route_selected", "route_start")],
                        "declines": [project(r) for r in child if r["event"] == "decline"],
                        "stops": stops})

    winners = [r for r in lifecycle if r["event"] == "parent_winner"]
    accepted = [r for r in winners if r.get("reason") == "accepted"]
    if row["result"] in SOLVED:
        if row.get("exit_code") and row["exit_code"] != str(SOLVED[row["result"]]):
            issue("delivery", "campaign_result_exit_mismatch", directory)
        if len(accepted) != 1:
            issue("delivery", "missing_accepted_winner" if not accepted
                  else "multiple_accepted_winners", directory)
        for winner in accepted:
            if winner.get("exit_code") != SOLVED[row["result"]] or row.get("timed_out") == "true":
                issue("delivery", "winner_result_mismatch", location(winner))
            if winner.get("worker_pid") not in workers or not any(
                    r["event"] == "parent_terminal" for r in workers[winner["worker_pid"]]):
                issue("delivery", "winner_missing_parent_terminal", location(winner))
    elif winners:
        issue("delivery", "unexpected_winner", location(winners[0]))
    if len(accepted) != len(winners):
        issue("delivery", "unaccepted_winner_record", directory)
    if not workers:
        issue("producer", "worker_inventory_unknown", directory)
    checkpoints = [{**r, "pid": str(r["_emitter_pid"])} for r in records if "checkpoint" in r]
    phase = DIAG.classify_target(checkpoints)[0] if checkpoints else "unknown"
    bucket = DIAG.summarize_fixpoint_children(checkpoints)[0] if phase == "fixpoint-bound" else ""
    states = {dimension: "incomplete" if any(i["dimension"] == dimension for i in issues)
              else "complete" for dimension in ("delivery", "producer")}
    return {"campaign": row, "phase_directory": str(directory), "confirmed_records": len(records),
            **states, "complete": not issues, "issues": issues,
            "accepted_winners": [project(r) for r in accepted], "workers": details,
            "expected_worker_specs": inventory,
            "checkpoint_phase": phase, "fixpoint_bucket": bucket}


def audit_campaign(campaign, root):
    results, directories = [], set()
    required = ("solver_label", "instance", "cap_s", "result", "run_index")
    for row in WORKER.read_tsv(campaign, required):
        for field in ("cap_s", "run_index"):
            value = WORKER.number(row[field], campaign, field)
            if value != value.to_integral_value() or (field == "cap_s" and value == 0):
                raise ValueError(f"{campaign}: invalid {field}={row[field]!r}")
            row[field] = str(int(value))
        directory = phase_directory(root, row)
        if directory in directories:
            raise ValueError(f"{campaign}: duplicate phase directory {directory}")
        directories.add(directory)
        results.append(audit_row(row, directory))
    return results


def summarize(results):
    summary = {"rows": len(results),
               "results": dict(Counter(r["campaign"]["result"] for r in results)),
               "complete": sum(r["complete"] for r in results),
               "incomplete": sum(not r["complete"] for r in results)}
    for dimension in ("delivery", "producer"):
        reasons = Counter(reason for r in results for reason in {
            i["reason"] for i in r["issues"] if i["dimension"] == dimension})
        complete = sum(r[dimension] == "complete" for r in results)
        summary[dimension] = {"complete": complete, "incomplete": len(results) - complete,
                              "incomplete_by_reason": dict(sorted(reasons.items()))}
    summary["by_result"] = {result: {"rows": len(group),
        "complete": sum(r["complete"] for r in group),
        "delivery_complete": sum(r["delivery"] == "complete" for r in group),
        "producer_complete": sum(r["producer"] == "complete" for r in group)}
        for result in sorted(summary["results"])
        for group in [[r for r in results if r["campaign"]["result"] == result]]}
    summary["worker_telemetry"] = dict(Counter(
        w["telemetry"] for r in results for w in r["workers"]))
    summary["worker_cases"] = dict(Counter(
        w["producer_case"] for r in results for w in r["workers"]))
    summary["accepted_winners"] = dict(Counter(
        "/".join(str(w.get(field, "unknown")) for field in (
            "original_polarity", "requested_backend", "effective_backend", "route"))
        for r in results for w in r["accepted_winners"]))
    return summary


def write_tsv(path, results):
    fields = ("solver_label", "instance", "cap_s", "run_index", "result", "acacia_sha",
              "binary_sha256", "delivery", "producer", "complete", "delivery_reasons",
              "producer_reasons", "phase_directory", "confirmed_records", "accepted_winners",
              "routes", "declines", "stops", "workers", "issues", "checkpoint_phase",
              "fixpoint_bucket", "expected_worker_specs")
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fields, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        for result in results:
            row = {field: result["campaign"].get(field, "") for field in fields[:7]}
            row.update({field: result[field] for field in fields[7:] if field in result})
            for field in ("routes", "declines", "stops"):
                row[field] = [e for worker in result["workers"] for e in worker[field]]
            for dimension in ("delivery", "producer"):
                row[f"{dimension}_reasons"] = ";".join(sorted({
                    i["reason"] for i in result["issues"]
                                                              if i["dimension"] == dimension}))
            for field in ("accepted_winners", "routes", "declines", "stops", "workers", "issues",
                          "expected_worker_specs"):
                row[field] = json.dumps(row[field], separators=(",", ":"), sort_keys=True)
            writer.writerow(row)


def make_report(results, summary):
    lines = ["# Campaign attribution completeness", "",
             f"Rows: {summary['rows']}; complete: {summary['complete']}; "
             f"incomplete: {summary['incomplete']}.", "",
             "Delivery and producer completeness are separate. Reason counts count rows once per "
             "reason and overlap. Only parent-accepted winners are reported as winners. Unknown "
             "routes remain unknown; missing events do not become declines.", "",
             "Worker inventories use explicit --arms or the current preset registry "
             "when available; "
             "otherwise all observed worker identities and contiguous launch indices are checked. "
             "Specs and requested context are compared per launch index. Runtime settings must be "
             "booleans in specs and lifecycle events. The inventory source is not proof of "
             "binary/configuration equivalence.", "",
             f"Only regular files are read, without following symlinks, with a global "
             f"{MAX_RECORD_FILE_BYTES}-byte limit per file; rejected destinations make "
             "delivery incomplete.", "",
             "Killed workers without final events remain producer-incomplete even with "
             "trustworthy "
             "parent metadata. Missing expected worker starts remain delivery-incomplete. "
             "A contiguous prefix ending at route selection can precede a kill before "
             "route start. "
             "Checkpoint buckets reuse the existing diagnostic reader and stay unknown "
             "when absent. "
             "Complete attribution alone does not authorize smaller-cap reuse.", ""]
    lines += WORKER.markdown_table(["Dimension", "Complete", "Incomplete"],
        [[d, summary[d]["complete"], summary[d]["incomplete"]] for d in ("delivery", "producer")])
    lines += WORKER.markdown_table(["Dimension", "Incomplete reason", "Rows"],
        [[d, reason, count] for d in ("delivery", "producer")
         for reason, count in summary[d]["incomplete_by_reason"].items()])
    lines += WORKER.markdown_table(["Result", "Rows", "Delivery complete", "Producer complete",
                                    "Both complete"],
        [[result, counts["rows"], counts["delivery_complete"], counts["producer_complete"],
          counts["complete"]] for result, counts in summary["by_result"].items()])
    lines += ["## Every incomplete row", ""]
    lines += WORKER.markdown_table(
        ["Run", "Instance", "Result", "Delivery", "Producer", "Reasons"],
        [[r["campaign"]["run_index"], r["campaign"]["instance"], r["campaign"]["result"],
          r["delivery"], r["producer"], "; ".join(sorted({
              i["dimension"] + ":" + i["reason"] for i in r["issues"]}))]
         for r in results if not r["complete"]])
    lines += ["## Accepted winner attribution", ""]
    lines += WORKER.markdown_table(
        ["Original polarity / requested / effective backend / route", "Rows"],
                                  sorted(summary["accepted_winners"].items()))
    lines += ["Per-row TSV JSON cells preserve winner, route, decline and stop events, "
              "worker specs, "
              "parent metadata and path:line evidence for every issue. Inputs are read only.", ""]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("campaign_tsv", type=Path)
    parser.add_argument("--phase-records-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="Per-invocation TSV")
    parser.add_argument("--report", type=Path, required=True, help="Markdown completeness report")
    parser.add_argument("--summary", type=Path, required=True, help="Machine-readable counts JSON")
    args = parser.parse_args()
    try:
        outputs = [p.resolve() for p in (args.output, args.report, args.summary)]
        if len(set(outputs)) != len(outputs):
            raise ValueError("output paths must be distinct")
        sources = {args.campaign_tsv.resolve(), *(
            p.resolve() for p in args.phase_records_dir.rglob("*.jsonl"))}
        if any(p in sources or p.is_relative_to(args.phase_records_dir.resolve())
               for p in outputs):
            raise ValueError("outputs must not overwrite campaign inputs "
                             "or lie in the phase directory")
        results = audit_campaign(args.campaign_tsv.resolve(), args.phase_records_dir.resolve())
        summary = summarize(results)
        summary.update(campaign_tsv=str(args.campaign_tsv.resolve()),
                       phase_records_dir=str(args.phase_records_dir.resolve()))
        for path in outputs:
            path.parent.mkdir(parents=True, exist_ok=True)
        write_tsv(args.output, results)
        args.report.write_text(make_report(results, summary), encoding="utf-8")
        args.summary.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n",
                                encoding="utf-8")
    except (OSError, ValueError) as error:
        parser.exit(1, f"error: {error}\n")
    print(f"rows={summary['rows']} complete={summary['complete']} "
          f"incomplete={summary['incomplete']}")
    for dimension in ("delivery", "producer"):
        print(f"{dimension}: complete={summary[dimension]['complete']} "
              f"incomplete={summary[dimension]['incomplete']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
