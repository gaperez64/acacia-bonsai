"""Read native frozen phase/worker records; missing observations remain missing."""

import csv
import json
import math
import re
from collections import defaultdict
from pathlib import Path

CATEGORIES = [
    "translation",
    "preprocessing",
    "r_prepass",
    "equivariance_symmetry",
    "weakening_prepass",
    "game_solving",
    "synthesis",
    "native_combined",
    "obligation_opaque",
    "other",
]
WRAPPERS = {"subjob", "subjob-summary", "k-attempt"}


def read_tsv(path):
    with Path(path).open() as f:
        return list(csv.DictReader(f, delimiter="\t"))


def write_tsv(path, rows, fields):
    with Path(path).open("w", newline="") as f:
        w = csv.DictWriter(f, fields, delimiter="\t", extrasaction="ignore")
        w.writeheader()
        for row in rows:
            w.writerow({k: ("NA" if row.get(k) is None else row.get(k)) for k in fields})


def quantile(values, q):
    values = sorted(x for x in values if x is not None and math.isfinite(x))
    if not values:
        return None
    at = (len(values) - 1) * q
    low = int(at)
    return values[low] + (values[min(low + 1, len(values) - 1)] - values[low]) * (at - low)


def safe(value):
    return re.sub(r"[^A-Za-z0-9_.-]", "_", value)


def number(value):
    if value in (None, "", "NA"):
        return None
    x = float(value)
    return x if math.isfinite(x) and x >= 0 else None


def arm_name(spec, spawn):
    kind = spec.get("kind")
    if kind and kind != "legacy":
        return kind
    pref = spec.get("translation", "small")
    backend = spawn.get("requested_backend", spec.get("requested_backend", "?"))
    if spec.get("requested_polarity") == "REAL":
        return f"real:{pref}:{backend}"
    if spec.get("transform") == "formula":
        return "unreal:formula:" + backend
    if spec.get("transform") == "automaton":
        return "unreal:automaton:" + backend
    return "unknown"


def category(stage):
    if stage in {"translation", "translate"} or stage.startswith("translation-"):
        return "translation"
    if stage in {"search", "solve", "acacia", "verification", "certificate-check"}:
        return "game_solving"
    if stage in {
        "simplification",
        "preprocessing",
        "booleanization",
        "input-push",
        "input-push-lowering",
        "decomposition",
        "io-preparation",
        "io-decoding",
        "action-construction",
        "picker-preparation",
        "orient",
        "classify",
        "parity",
        "split",
        "explicit",
        "trusted_prepare",
    }:
        return "preprocessing"
    if stage == "pre-pass":
        return "weakening_prepass"
    # No stock timing scope isolates native R discovery or equivariance recognition.
    if stage.startswith(("native_r_", "native_candidate_", "native_seed_")):
        return "r_prepass"
    if stage in {"r_prepass", "r-prepass"}:
        return "r_prepass"
    if stage == "combined_call":
        return "native_combined"
    if stage == "obligation_spot":
        return "obligation_opaque"
    if stage == "obligation_preprocessing" or stage == "spot_fast_classification":
        return "preprocessing"
    if stage in {
        "equivariance_solve",
        "spot_fast_solve",
        "native_direct_solve",
        "native_target_check",
    }:
        return "game_solving"
    if stage in {"equivariance_recognition", "symmetry_recognition"}:
        return "equivariance_symmetry"
    if any(x in stage for x in ["synthesis", "export", "aig", "strategy"]):
        return "synthesis"
    return "other"


def load_records(directory):
    records, errors = [], []
    for path in sorted(directory.glob("*.jsonl")):
        with path.open() as stream:
            for line_no, line in enumerate(stream, 1):
                try:
                    e = json.loads(line)
                    if not isinstance(e, dict):
                        raise ValueError("record must be an object")
                except (ValueError, TypeError):
                    errors.append(f"{path.name}:{line_no}:invalid_json")
                    continue
                stem = path.stem.removesuffix("-final")
                e["_file_pid"] = int(stem) if stem.isdigit() else None
                e["_invocation"] = str(directory.resolve())
                e["_pid"] = e.get("worker_pid", e.get("pid", e["_file_pid"]))
                records.append(e)
    return records, errors


def accounting_stages(records):
    """Exclusive CPU uses actual absolute intervals, including interleaved work.

    Entries recovered from the shared snapshot remain censored: their last
    entry CPU is an entry value, never an invented completion measurement.
    """
    starts = {
        (e["_pid"], e["phase_id"]): e for e in records if e.get("event") == "accounting_entry"
    }
    snapshots = {
        (e["_pid"], e["phase_id"]): e for e in records if e.get("event") == "accounting_censored"
    }
    entries = dict(snapshots)
    entries.update(starts)
    ends = {(e["_pid"], e["phase_id"]): e for e in records if e.get("event") == "accounting_end"}
    observed = entries | ends
    # A lost parent hides which enclosing scopes must subtract its CPU.
    orphaned = {
        pid for (pid, _), e in observed.items()
        if e.get("parent_phase_id")
        and ((pid, e["parent_phase_id"]) not in starts or (pid, e["parent_phase_id"]) not in ends)
    }
    output = []
    for key in observed:
        entry, end = entries.get(key), ends.get(key)
        e = end or snapshots.get(key) or entry
        children = [
            k for k, v in observed.items() if k[0] == key[0] and v.get("parent_phase_id") == key[1]
        ]
        wall, cpu = number((end or {}).get("wall_ns")), number((end or {}).get("cpu_ns"))
        if (
            end
            and key in starts
            and key[0] not in orphaned
            and end.get("dropped_records", 0) == entry.get("dropped_records", 0)
            and all(k in starts and k in ends for k in children)
        ):
            exclusive_wall = (
                wall - sum(ends[k]["wall_ns"] for k in children)
                if wall is not None
                and all(number(ends[k].get("wall_ns")) is not None for k in children)
                else None
            )
            exclusive_cpu = (
                cpu - sum(ends[k]["cpu_ns"] for k in children)
                if cpu is not None
                and all(number(ends[k].get("cpu_ns")) is not None for k in children)
                else None
            )
            if exclusive_wall is not None and exclusive_wall < 0:
                exclusive_wall = None
            if exclusive_cpu is not None and exclusive_cpu < 0:
                exclusive_cpu = None
        else:
            exclusive_wall = exclusive_cpu = None
        output.append(
            dict(
                e,
                stage=e["phase"],
                stage_id=key[1],
                entry_ns=e.get("entry_ns"),
                raw_wall_ns=wall,
                raw_cpu_ns=cpu,
                exclusive_wall_ns=exclusive_wall,
                exclusive_cpu_ns=exclusive_cpu,
                complete=bool(end),
            )
        )
    return output


def timed_stages(records):
    """Give frozen legacy records parent IDs, then use the same exclusive accounting.

    A completion with a lost entry still belongs to the current enclosing scope,
    so it invalidates that parent's exclusive measurement.
    """
    stacks, entries = defaultdict(list), {}
    normalized = []
    for e in sorted(records, key=lambda e: (e["_pid"], e.get("seq", 0))):
        event = e.get("event")
        if event not in {"stage_entry", "stage_completion", "stage_stopped", "stage_censored"}:
            continue
        pid, phase_id = e["_pid"], e["stage_id"]
        stack = stacks[pid]
        begin = entries.get((pid, phase_id))
        parent = begin["parent_phase_id"] if begin else stack[-1] if stack else 0
        scope = dict(e, phase=e["stage"], phase_id=phase_id, parent_phase_id=parent,
                     entry_ns=(begin or {}).get("entry_ns", e.get("entry_ns")))
        if event == "stage_entry":
            scope.update(event="accounting_entry", entry_ns=e["mono_ns"])
            entries[(pid, phase_id)] = scope
            stack.append(phase_id)
        elif event == "stage_censored":
            scope["event"] = "accounting_censored"
        else:
            scope["event"] = "accounting_end"
            if phase_id in stack:
                del stack[stack.index(phase_id):]
        normalized.append(scope)
    return accounting_stages(normalized)


def phase_directory(root, row):
    return (
        Path(root)
        / safe(row["solver_label"])
        / str(row["cap_s"])
        / (safe(row["instance"]) + "-" + str(row["run_index"]))
    )


def analyze_invocation(row, phase_root):
    directory = phase_directory(phase_root, row)
    records, errors = load_records(directory)
    specs = {e["worker_pid"]: e for e in records if e.get("event") == "worker_spec"}
    spawns = {e["worker_pid"]: e for e in records if e.get("event") == "worker_spawn"}
    terminals = {e["worker_pid"]: e for e in records if e.get("event") == "parent_terminal"}
    winners = [e for e in records if e.get("event") == "parent_winner"]
    winner = min(winners, key=lambda e: e["mono_ns"]) if winners else None
    accounting = accounting_stages(records)
    accounting_pids = {e["_pid"] for e in records if (e.get("event") or "").startswith("accounting_")}
    stages = timed_stages([e for e in records if e["_pid"] not in accounting_pids])
    reaps = {e["worker_pid"]: e for e in records if e.get("event") == "worker_reap"}
    workers, phase_rows, lifetime_rows, duplicate_candidates = [], [], [], []
    raw_stages = []
    for pid, spec in sorted(specs.items()):
        own = [e for e in records if e["_pid"] == pid]
        mine = [s for s in stages if s["_pid"] == pid]
        extra = [s for s in accounting if s["_pid"] == pid]
        if pid in accounting_pids:
            mine = extra
        arm = arm_name(spec, spawns.get(pid, {}))
        common = dict(
            instance=row["instance"],
            solver_label=row["solver_label"],
            stratum=row.get("stratum", "unknown"),
            arm=arm,
            worker_pid=pid,
            record_directory=str(directory.resolve()),
        )
        dropped = max((e.get("dropped_records", 0) for e in own), default=0)
        spawn, terminal = (
            spawns.get(pid, spec if "mono_ns" in spec else None),
            reaps.get(pid, terminals.get(pid)),
        )
        reap = reaps.get(pid)
        lifetime = (terminal["mono_ns"] - spawn["mono_ns"]) / 1e9 if terminal and spawn else None
        peaks = [number(e.get("peak_rss_kb")) for e in own]
        peak = max((v * 1024 for v in peaks if v is not None), default=None)
        final_peak = number((reap or {}).get("peak_rss_kb"))
        if final_peak is not None:
            peak = final_peak * 1024
        resources = [e for e in own if e.get("event") == "worker_resource"]
        final_cpu = number((reap or {}).get("self_cpu_ns"))
        # wait4 (minus explicitly reaped descendants) includes exit cleanup.

        descendant_reaps = {
            e["descendant_pid"]: e for e in own if e.get("event") == "descendant_reap"
        }
        descendant_spawns = {
            e["descendant_pid"] for e in own if e.get("event") == "descendant_spawn"
        }
        descendant_missing = (
            any(
                number(e.get(k)) is None
                for e in descendant_reaps.values()
                for k in ["user_ns", "sys_ns"]
            )
            or bool(descendant_spawns - descendant_reaps.keys())
            or any(e.get("event") == "descendant_censored" for e in own)
        )
        workers.append(
            dict(
                common,
                lifetime_wall_s=lifetime,
                peak_rss_observed_bytes=peak,
                peak_status=(
                    "wait4_final"
                    if final_peak is not None
                    else "milestone_lower_bound"
                    if peak is not None
                    else "missing"
                ),
                user_s=(
                    number(reap.get("user_ns")) / 1e9
                    if reap and number(reap.get("user_ns")) is not None
                    else None
                ),
                sys_s=(
                    number(reap.get("sys_ns")) / 1e9
                    if reap and number(reap.get("sys_ns")) is not None
                    else None
                ),
                self_cpu_s=(final_cpu / 1e9 if final_cpu is not None else None),
                descendant_cpu_s=(
                    None
                    if descendant_missing
                    else sum((e["user_ns"] + e["sys_ns"]) / 1e9 for e in descendant_reaps.values())
                ),
                dropped_records=dropped,
                terminal_observed=bool(terminal),
            )
        )
        totals = defaultdict(lambda: [None, None, 0])
        for stage in mine:
            raw_stages.append(
                dict(
                    common,
                    stage=stage["stage"],
                    stage_id=stage.get("stage_id"),
                    subjob=stage.get("subjob"),
                    run_id=stage.get("run_id"),
                    wall_s=(
                        stage["raw_wall_ns"] / 1e9 if stage["raw_wall_ns"] is not None else None
                    ),
                    cpu_s=(stage["raw_cpu_ns"] / 1e9 if stage["raw_cpu_ns"] is not None else None),
                    exclusive_wall_s=(
                        stage["exclusive_wall_ns"] / 1e9
                        if stage["exclusive_wall_ns"] is not None
                        else None
                    ),
                    exclusive_cpu_s=(
                        stage["exclusive_cpu_ns"] / 1e9
                        if stage["exclusive_cpu_ns"] is not None
                        else None
                    ),
                    complete=stage["complete"],
                    entry_mono_ns=stage.get("entry_ns"),
                    end_mono_ns=stage.get("mono_ns") if stage["complete"] else None,
                    entry_cpu_ns=stage.get("entry_cpu_ns"),
                    end_process_cpu_ns=stage.get("process_cpu_ns"),
                    parent_phase_id=stage.get("parent_phase_id"),
                )
            )
            if not stage["complete"] or stage["stage"] in WRAPPERS:
                continue
            bucket = totals[category(stage["stage"])]
            for i, field in enumerate(["exclusive_wall_ns", "exclusive_cpu_ns"]):
                if stage[field] is not None:
                    bucket[i] = (bucket[i] or 0) + stage[field] / 1e9
            bucket[2] += 1
        # These shipping native scopes are sequential. Combined_call is opaque;
        # its seed_cache/final_checks records are metadata samples, not durations.
        for e in own:
            if extra:
                continue
            cat = {
                "trusted_prepare": "preprocessing",
                "combined_call": "native_combined",
                "proof_binding": "other",
            }.get(e.get("phase"))
            if cat is None or "wall_ns" not in e:
                continue
            bucket = totals[cat]
            for i, field in enumerate(["wall_ns", "cpu_ns"]):
                value = number(e.get(field))
                if value is not None:
                    bucket[i] = (bucket[i] or 0) + value / 1e9
            bucket[2] += 1
        for cat in CATEGORIES:
            wall, cpu, n = totals[cat]
            phase_rows.append(
                dict(
                    common,
                    phase=cat,
                    wall_s=wall,
                    cpu_s=cpu,
                    wall_share_of_worker_lifetime=(
                        wall / lifetime if wall is not None and lifetime else None
                    ),
                    completed_scopes=n,
                    status=(
                        "partial_observed"
                        if n
                        else "not_exercised_decision_mode"
                        if cat == "synthesis"
                        else "unobserved"
                    ),
                )
            )
        for trigger in [
            "decline",
            "route_stopped",
            "verification_complete",
            "terminal_result",
            "winner_cancelled",
            "interrupted",
            "winner",
        ]:
            boundary_events = [e for e in resources if e.get("boundary") == trigger]
            events = boundary_events or [e for e in own if e.get("event") == trigger]
            if trigger == "winner":
                events = [winner] if winner and winner.get("worker_pid") != pid else []
            if not events:
                continue
            e = min(events, key=lambda x: x["mono_ns"])
            time_ns = e["mono_ns"]
            elapsed = max(0, (terminal["mono_ns"] - time_ns) / 1e9) if terminal else None
            # Count only fully completed, nonoverlapping exclusive scopes that
            # start after the trigger. This is an observed CPU lower estimate.
            after = [
                s
                for s in mine
                if s["complete"]
                and s.get("entry_ns") is not None
                and s["entry_ns"] >= time_ns
                and s["stage"] not in WRAPPERS
                and s["exclusive_cpu_ns"] is not None
            ]
            cpu = sum(s["exclusive_cpu_ns"] for s in after) / 1e9 if after else None
            lifetime_rows.append(
                dict(
                    common,
                    trigger=trigger,
                    reason=e.get("reason", ""),
                    trigger_mono_ns=time_ns,
                    live_until_reaped_s_upper_bound=elapsed,
                    completed_cpu_after_trigger_s_lower_bound=cpu,
                    continued_phase_observed=bool(after),
                    cpu_boundary_to_reap_s=(
                        max(0, final_cpu - e["process_cpu_ns"]) / 1e9
                        if final_cpu is not None and number(e.get("process_cpu_ns")) is not None
                        else None
                    ),
                    boundary_cpu_resolution_ns=e.get("cpu_resolution_ns"),
                    cpu_boundary_to_reap_s_lower_bound=(
                        max(
                            0,
                            final_cpu
                            - e["process_cpu_ns"]
                            - 2 * e.get("cpu_resolution_ns", 1)
                            - 2000 * (1 + len(descendant_reaps)),
                        )
                        / 1e9
                        if final_cpu is not None and number(e.get("process_cpu_ns")) is not None
                        else None
                    ),
                    cpu_boundary_to_reap_s_upper_bound=(
                        max(0, final_cpu - e["process_cpu_ns"] + 2000 * (1 + len(descendant_reaps)))
                        / 1e9
                        if final_cpu is not None and number(e.get("process_cpu_ns")) is not None
                        else None
                    ),
                    memory_after_trigger_bytes=(
                        number(e.get("rss_kb")) * 1024
                        if number(e.get("rss_kb")) is not None
                        else None
                    ),
                    retained_memory_integral_byte_s=None,
                    memory_sampling="boundary_only_no_retention_integral",
                    interpretation=(
                        "fallback_can_be_useful"
                        if trigger in {"decline", "route_stopped"}
                        else "reap_time_includes_zombie_delay"
                    ),
                )
            )
        def work_key(e):
            return (e.get("_invocation"), e.get("_pid"), e.get("subjob"), e.get("run_id"))

        def unique_identities(event):
            groups = defaultdict(list)
            for e in own:
                if e.get("event") == event:
                    groups[work_key(e)].append(e)
            return {key: group[0] for key, group in groups.items() if len(group) == 1}

        identities = unique_identities("work_identity")
        for scope in mine:
            if not scope["complete"] or scope["raw_cpu_ns"] is None:
                continue
            key = work_key(scope)
            identity = identities.get(key)
            if None in key or identity is None:
                continue
            keys = [
                "formula_sha256",
                "alphabet_sha256",
                "provider",
                "translation_options_sha256",
                "preprocessing_options_sha256",
            ]
            if not all(identity.get(k) for k in keys):
                continue
            if scope["stage"] not in {
                "translation",
                "preprocessing",
                "booleanization",
                "obligation_spot",
                "obligation_preprocessing",
            }:
                continue
            duplicate_candidates.append(
                dict(
                    common,
                    phase=scope["stage"],
                    subjob=scope["subjob"],
                    run_id=scope["run_id"],
                    identity=json.dumps([identity[k] for k in keys]),
                    cpu_s=scope["raw_cpu_ns"] / 1e9,
                )
            )
        native_identity = unique_identities("native_work_identity")
        artifacts = {e.get("phase_id"): e for e in own if e.get("event") == "accounting_artifact"}
        for scope in extra:
            key = work_key(scope)
            config = native_identity.get(key)
            artifact = artifacts.get(scope.get("phase_id"))
            if (None in key or not config or not artifact or not scope["complete"]
                    or scope["exclusive_cpu_ns"] is None):
                continue
            identity = [
                config.get(k)
                for k in [
                    "source_sha256",
                    "provider",
                    "translation_options",
                    "preprocessing_options_sha256",
                ]
            ]
            if not all(identity):
                continue
            duplicate_candidates.append(
                dict(
                    common,
                    phase=scope["stage"],
                    subjob=scope["subjob"],
                    run_id=scope["run_id"],
                    identity=json.dumps(identity + [artifact["artifact_sha256"]]),
                    cpu_s=scope["exclusive_cpu_ns"] / 1e9,
                )
            )
    native = []
    for e in records:
        if "phase" in e and e.get("event") is None and number(e.get("wall_ns")) is not None:
            pid = e["_pid"]
            native.append(
                dict(
                    instance=row["instance"],
                    worker_pid=pid,
                    arm=arm_name(specs[pid], spawns.get(pid, {})) if pid in specs else e.get("arm"),
                    phase=e["phase"],
                    wall_s=e["wall_ns"] / 1e9,
                    cpu_s=number(e.get("cpu_ns")) / 1e9
                    if number(e.get("cpu_ns")) is not None
                    else None,
                    status="inclusive_or_metadata_sample_not_aggregated",
                )
            )
    quality = dict(
        instance=row["instance"],
        record_directory=str(directory),
        missing_directory=not directory.is_dir(),
        workers=len(specs),
        parse_errors=len(errors),
        dropped_records=sum(
            max((e.get("dropped_records", 0) for e in records if e["_pid"] == pid), default=0)
            for pid in {e["_pid"] for e in records}
        ),
        writer_failed_records=(
            sum(e.get("failed_records", 0) for e in records)
            if any(e.get("event") == "writer_summary" for e in records)
            else None
        ),
        writer_summary_present=any(e.get("event") == "writer_summary" for e in records),
        censored_stages=sum(not s["complete"] for s in stages + accounting),
        wait4_workers=len(reaps),
        parent_summary_present=any(e.get("event") == "parent_record_summary" for e in records),
        descendant_censored=sum(e.get("event") == "descendant_censored" for e in records),
        unmatched_stage_entries=len(
            {(e["_pid"], e.get("stage_id")) for e in records if e.get("event") == "stage_entry"}
            - {
                (e["_pid"], e.get("stage_id"))
                for e in records
                if e.get("event") in {"stage_completion", "stage_stopped"}
            }
        ),
        scope_peak_bytes=number(row.get("scope_memory_peak_bytes")),
        invocation_cpu_s=number(row.get("cpu_seconds")),
    )
    descendants = [
        dict(instance=row["instance"], record_directory=str(directory.resolve()),
             **{k: v for k, v in e.items() if not k.startswith("_")})
        for e in records
        if e.get("event") in {"descendant_spawn", "descendant_reap", "descendant_censored"}
    ]
    identities = [
        dict(instance=row["instance"], record_directory=str(directory.resolve()),
             **{k: v for k, v in e.items() if not k.startswith("_")})
        for e in records
        if e.get("event") in {"work_identity", "native_work_identity", "accounting_artifact"}
    ]
    segments = [
        dict(instance=row["instance"], record_directory=str(directory.resolve()),
             **{k: v for k, v in e.items() if not k.startswith("_")})
        for e in records
        if e.get("event") in {"accounting_segment", "accounting_segment_censored"}
    ]
    return dict(
        workers=workers,
        phases=phase_rows,
        lifetimes=lifetime_rows,
        descendants=descendants,
        identities=identities,
        segments=segments,
        duplicates=duplicate_candidates,
        native=native,
        stages=raw_stages,
        quality=quality,
    )


def duplicate_estimates(candidates):
    groups = defaultdict(lambda: defaultdict(float))
    for c in candidates:
        key = (c.get("record_directory"), c.get("worker_pid"), c.get("subjob"), c.get("run_id"))
        if None in key:
            continue
        groups[(key[0], c["instance"], key[2], key[3], c["phase"], c["identity"])][(key[1], c["arm"])] += c["cpu_s"]
    output = []
    for (directory, instance, subjob, run_id, phase, identity), arms in sorted(groups.items()):
        if len(arms) < 2:
            continue
        total = sum(arms.values())
        # Keep one (most expensive) arm's work. Allocation proportional to CPU
        # is accounting only; it does not select an arm to remove.
        redundant = total - max(arms.values())
        for (pid, arm), cpu in sorted(arms.items()):
            output.append(
                dict(
                    instance=instance,
                    record_directory=directory,
                    subjob=subjob,
                    run_id=run_id,
                    phase=phase,
                    arm=arm,
                    worker_pid=pid,
                    group_arms=",".join(sorted({arm for _, arm in arms})),
                    group_cpu_s=total,
                    duplicated_cpu_s_estimate=(redundant * cpu / total if total else 0),
                    identity=identity,
                    status="completed_cpu_only_identity_matched",
                )
            )
    return output
