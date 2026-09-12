"""Turn captured campaign worker records into the S0 failure-phase table and report.

Use the largest recorded cap for each instance; never combine counters across workers
or K attempts. Empty TSV cells mean absent measurements. A search snapshot can retain
timings from an earlier completed attempt while its K already names the next attempt.
"""

import argparse
import csv
import io
import json
import re
from collections import Counter
from dataclasses import dataclass
from decimal import Decimal, InvalidOperation
from pathlib import Path
from statistics import median


ARM_FIELDS = ("polarity", "transform", "requested_backend", "requested_provider")
STAGES = (
    "before-translation", "fallback-translation", "preprocessing", "factory",
    "enumeration", "search", "verification", "verified-attempt",
)
NUMERIC_FIELDS = (
    "search_ms", "verification_ms", "row_generation_ms", "prep_ms", "game_states",
    "guarded_choices", "subsumption_scans", "subsumption_nodes_checked",
    "subsumption_nodes_invalidated", "subsumption_prefilter_skips", "losing_insertions",
    "losing_removals", "reopen_enqueues", "search_bdd_operations", "search_queries",
    "search_preimage_hits", "search_peak_live_nodes", "search_cache_rank_bytes",
    "rank_interner_bytes", "wrapper_rows_generated", "wrapper_states_discovered",
    "wrapper_edges_generated",
)
MECHANISMS = (
    "game_states", "guarded_choices", "choices-per-node", "subsumption_scans",
    "subsumption_nodes_checked", "reopen_enqueues", "search_bdd_operations",
    "search_queries", "search_preimage_hits", "wrapper_rows_generated",
    "wrapper_states_discovered", "k",
)


def instance_key(value):
    """Normalize both sides of every join, without removing any other suffix."""
    return value.removesuffix(".ltl")


def number(value, source, field):
    try:
        if isinstance(value, bool) or not isinstance(value, (str, int, float)):
            raise ValueError("expected a number or numeric string")
        result = Decimal(str(value))
        if not result.is_finite() or result < 0:
            raise ValueError("expected a finite nonnegative number")
        return result
    except (InvalidOperation, ValueError) as error:
        raise ValueError(f"{source}: invalid {field}={value!r}: {error}") from error


def read_tsv(path, required):
    try:
        with path.open(encoding="utf-8", newline="") as stream:
            reader = csv.DictReader(stream, delimiter="\t", strict=True)
            missing = set(required) - set(reader.fieldnames or ())
            if missing:
                raise ValueError(f"missing columns: {', '.join(sorted(missing))}")
            rows = []
            for row in reader:
                if None in row or any(value is None for value in row.values()):
                    raise ValueError(f"line {reader.line_num}: wrong number of columns")
                for field in required:
                    if not row[field]:
                        raise ValueError(f"line {reader.line_num}: empty {field}")
                rows.append(row)
            return rows
    except (OSError, UnicodeError, csv.Error, ValueError) as error:
        raise ValueError(f"{path}: {error}") from error


@dataclass
class Worker:
    path: Path
    record: dict
    campaign: dict
    tier: str
    numbers: dict

    @property
    def instance(self):
        return instance_key(self.record["instance"]) + ".ltl"

    @property
    def arm(self):
        return "/".join(self.record[field] for field in ARM_FIELDS)

    @property
    def reached_search(self):
        return self.record["stage"] in ("search", "verification", "verified-attempt")

    @property
    def timed_out(self):
        return self.campaign["timed_out"] == "true"

    @property
    def terminal_attempt(self):
        """A verified attempt is terminal only on an outcome or the last losing K."""
        if self.record["stage"] != "verified-attempt" or not self.record.get("status"):
            return False
        if self.record["status"] == "WIN_K":
            return True
        if self.record["status"] in ("UNKNOWN", "RESOURCE_LIMIT"):
            return self.record.get("candidate_mode") == "only"
        if self.record["status"] != "LOSE_K":
            return False
        return (
            "k" in self.numbers and "kmax" in self.numbers
            and self.numbers["k"] >= self.numbers["kmax"]
        )


def load_workers(args):
    tiers = {}
    for row in read_tsv(args.cohort, ("instance", "tier")):
        key = instance_key(row["instance"])
        if key in tiers:
            raise ValueError(f"{args.cohort}: duplicate normalized instance {key!r}")
        tiers[key] = row["tier"]

    campaign_path = args.campaign_dir / f"{args.label}.tsv"
    campaigns = {}
    required = ("solver_label", "instance", "cap_s", "result", "seconds", "timed_out")
    for row in read_tsv(campaign_path, required):
        if row["solver_label"] != args.label:
            continue
        key = instance_key(row["instance"])
        if key not in tiers:
            raise ValueError(f"{campaign_path}: unmatched cohort instance {row['instance']!r}")
        cap = number(row["cap_s"], campaign_path, "cap_s")
        number(row["seconds"], campaign_path, "seconds")
        if row["timed_out"] not in ("true", "false"):
            raise ValueError(f"{campaign_path}: invalid timed_out for {key!r}")
        if (key, cap) in campaigns:
            raise ValueError(f"{campaign_path}: duplicate instance/cap: {key!r}/{cap}")
        campaigns[key, cap] = row
    if not campaigns:
        raise ValueError(f"{campaign_path}: no runs for label {args.label!r}")
    selected = {}
    for key, cap in campaigns:
        selected[key] = max(cap, selected.get(key, cap))

    summary_path = args.campaign_dir / f"{args.label}-summary.tsv"
    summaries = {}
    for row in read_tsv(summary_path, ("solver_label", "instance", "max_cap_s")):
        if row["solver_label"] != args.label:
            continue
        key = instance_key(row["instance"])
        if key not in tiers or key not in selected:
            raise ValueError(f"{summary_path}: unmatched instance {row['instance']!r}")
        if key in summaries:
            raise ValueError(f"{summary_path}: duplicate normalized instance {key!r}")
        max_cap = number(row["max_cap_s"], summary_path, "max_cap_s")
        if selected[key] > max_cap:
            raise ValueError(f"{summary_path}: max_cap_s precedes recorded run for {key!r}")
        run = campaigns[key, selected[key]]
        if selected[key] == max_cap and row.get("failure_kind_at_max_cap") != run["result"]:
            raise ValueError(f"{summary_path}: result disagrees with campaign for {key!r}")
        summaries[key] = row
    missing = set(selected) - set(summaries)
    if missing:
        raise ValueError(f"{summary_path}: missing instances: {', '.join(sorted(missing))}")

    safe_label = re.sub(r"[^A-Za-z0-9_.-]", "_", args.label)
    root = args.campaign_dir / "wrec" / safe_label
    paths = sorted(root.rglob("*.json"))
    if not paths:
        raise ValueError(f"{root}: no worker JSON records")
    workers = {}
    for path in paths:
        try:
            record = json.loads(path.read_text(encoding="utf-8"))
            if not isinstance(record, dict):
                raise ValueError("expected a JSON object")
            for field in ("instance", "stage", *ARM_FIELDS):
                if not isinstance(record.get(field), str) or not record[field]:
                    raise ValueError(f"missing or invalid {field}")
            if record["stage"] not in STAGES:
                raise ValueError(f"unknown stage {record['stage']!r}")
            if "status" in record and not isinstance(record["status"], str):
                raise ValueError("invalid status")
            fields = set(NUMERIC_FIELDS) | {"k", "kmax"}
            fields.update(field for field in record if field.startswith(("verify_", "search_")))
            numbers = {field: number(record[field], path, field)
                       for field in fields if field in record}
            key = instance_key(record["instance"])
            if key not in tiers:
                raise ValueError(f"unmatched cohort instance {record['instance']!r}")
            cap = number(path.relative_to(root).parts[0], path, "capture cap")
            if (key, cap) not in campaigns:
                raise ValueError(f"unmatched campaign instance/cap: {key!r}/{cap}")
            if cap != selected[key]:
                continue
            worker = Worker(path, record, campaigns[key, cap], tiers[key], numbers)
            worker_key = key, worker.arm
            if worker_key in workers:
                raise ValueError(f"duplicate instance/arm, also in {workers[worker_key].path}")
            workers[worker_key] = worker
        except (OSError, UnicodeError, ValueError) as error:
            raise ValueError(f"{path}: cannot parse worker record: {error}") from error

    arms = {arm for _, arm in workers}
    for key in selected:
        missing = arms - {arm for instance, arm in workers if instance == key}
        if missing or not arms:
            raise ValueError(f"{root}: missing workers for {key!r}: {sorted(missing)}")
    sparse = [w for w in workers.values()
              if w.record["requested_backend"] == "spot-guarded-sparse"]
    if len({w.arm for w in sparse}) != 1 or len(sparse) != len(selected):
        raise ValueError(f"{root}: expected exactly one sparse guarded arm per instance")
    return sorted(workers.values(), key=lambda w: (w.instance, w.arm))


def markdown_table(headers, rows):
    def line(cells):
        return "| " + " | ".join(str(cell).replace("|", "\\|") for cell in cells) + " |"
    return [line(headers), line(["---"] * len(headers)), *(line(row) for row in rows), ""]


def display(value):
    return "absent" if value is None else format(value, ".6f").rstrip("0").rstrip(".")


def make_report(workers, label):
    sparse = [w for w in workers if w.record["requested_backend"] == "spot-guarded-sparse"]
    searched = [w for w in sparse if w.reached_search]
    tiers = sorted({w.tier for w in sparse})
    stages = [stage for stage in STAGES if any(w.record["stage"] == stage for w in sparse)]
    counts = Counter((w.record["stage"], w.tier) for w in sparse)
    lines = [
        f"# {label}: worker failure phases", "",
        f"{len(workers)} worker records; {len(sparse)} instances. One row per instance/arm "
        "at its largest recorded cap. Campaign seconds describe the whole invocation.", "",
        "## 1. Routing table", "", f"Sparse guarded arm: `{sparse[0].arm}`.", "",
    ]
    routing = [[stage, *(counts[stage, tier] for tier in tiers),
                sum(counts[stage, tier] for tier in tiers)] for stage in stages]
    routing.append(["Total", *(sum(w.tier == tier for w in sparse) for tier in tiers),
                    len(sparse)])
    lines += markdown_table(["Furthest stage", *tiers, "Total"], routing)
    before = sum(not w.reached_search for w in sparse)
    lines += [
        f"{before}/{len(sparse)} workers stopped before search: the game was never reached. "
        "`before-translation` (or `fallback-translation`) is translation-bound; "
        "`preprocessing`, `factory`, or `enumeration` is preprocessing-bound. "
        "These are last observed boundaries, not completed phase durations.", "",
        "## 2. Verification share", "",
        "Share = 100 × verification_ms / (search_ms + verification_ms). "
        "Search includes `verification` and `verified-attempt`. "
        "Timings and counters in a `search` snapshot "
        "may survive from the preceding completed K attempt; the recorded K can already "
        "refer to the new, unfinished attempt. These are snapshot distributions, not "
        "whole-run totals or measurements of the unfinished attempt.", "",
    ]
    shares, missing, zero_total = [], [], []
    for worker in searched:
        search = worker.numbers.get("search_ms")
        verify = worker.numbers.get("verification_ms")
        if search is None or verify is None:
            missing.append(worker)
        elif search + verify == 0:
            zero_total.append(worker)
        else:
            shares.append((100 * verify / (search + verify), worker))
    shares.sort(key=lambda item: (-item[0], item[1].instance))
    lines += [f"Defined shares: n={len(shares)} of {len(searched)} search-reaching instances.", ""]
    lines += markdown_table(
        ["Instance", "Tier", "search_ms", "verification_ms", "Verification share"],
        [[w.instance, w.tier, w.record["search_ms"], w.record["verification_ms"],
          f"{share:.1f}%"] for share, w in shares],
    )
    if shares:
        values = [share for share, _ in shares]
        lines += [f"Share distribution (n={len(values)}): minimum {min(values):.1f}%, "
                  f"median {median(values):.1f}%, maximum {max(values):.1f}%. "
                  "No average of percentages is used.", ""]
    lines += [f"Present-but-uninstrumented: n={len(missing)}. Missing timings are absent, "
              "not zero, and are excluded from the share distribution.", ""]
    lines += markdown_table(
        ["Instance", "Tier", "Stage", "k", "search_ms", "verification_ms"],
        [[w.instance, w.tier, w.record["stage"], w.record.get("k", "absent"),
          w.record.get("search_ms", "absent"), w.record.get("verification_ms", "absent")]
         for w in missing],
    )
    if zero_total:
        lines += [f"Undefined shares with both timings zero: n={len(zero_total)}; "
                  + ", ".join(w.instance for w in zero_total) + ".", ""]
    lines += ["## 3. Mechanism table", "",
              f"Population: {len(searched)} sparse workers that reached search. "
              "Each statistic uses only present fields; n applies to both median and max. "
              "Choices-per-node is guarded_choices / game_states for each instance, "
              "requiring both counters and game_states > 0. K is the recorded K, including "
              "workers without timing counters.", ""]
    mechanisms = []
    for field in MECHANISMS:
        values = []
        for worker in searched:
            if field == "choices-per-node":
                states = worker.numbers.get("game_states")
                choices = worker.numbers.get("guarded_choices")
                value = choices / states if choices is not None and states else None
            else:
                value = worker.numbers.get(field)
            if value is not None:
                values.append(value)
        mechanisms.append([field, len(values), display(median(values)) if values else "absent",
                           display(max(values)) if values else "absent"])
    lines += markdown_table(["Metric", "n", "Median", "Max"], mechanisms)

    capped = [w for w in workers if w.timed_out]
    sparse_capped = [w for w in sparse if w.timed_out]
    censored = [w for w in sparse_capped if not w.terminal_attempt]
    terminal = [w for w in sparse if w.terminal_attempt]
    unfinished = [w for w in sparse if not w.terminal_attempt]
    uncapped_decisive = [w for w in unfinished if not w.timed_out
                        and w.campaign["result"] in ("REALIZABLE", "UNREALIZABLE")]
    conventional = [w for w in workers
                    if w.record["requested_backend"] in ("backward", "forward")]
    conventional_capped = [w for w in conventional if w.timed_out]
    lines += [
        "## 4. Censoring note", "",
        f"{len({w.instance for w in capped})}/{len(sparse)} campaign invocations timed out, "
        f"covering {len(capped)}/{len(workers)} captured workers. "
        f"Among sparse guarded workers, {len(censored)}/{len(sparse_capped)} in timed-out "
        f"invocations ({len(censored)}/{len(sparse)} overall) lack a terminal attempt snapshot. "
        "These are cap-censored observations: their last stage is not a completed stage "
        "duration. This counts absent terminal evidence at the cap; the snapshots alone "
        "do not prove the individual process was alive when the kill occurred.", "",
        f"{len(terminal)}/{len(sparse)} sparse workers have terminal attempt evidence. "
        "`verified-attempt` marks completion of one K attempt, not automatically worker "
        "completion: terminal evidence requires WIN_K, UNKNOWN/RESOURCE_LIMIT in "
        "candidate-only mode, or LOSE_K with k >= kmax. "
        "Retained timings or a LOSE_K status at stage `search` do not mark "
        "completion of the worker.", "",
        f"In total, {len(unfinished)}/{len(sparse)} sparse workers lack terminal evidence; "
        f"{len(unfinished) - len(censored)} of these are outside timed-out invocations. "
        f"Of those, {len(uncapped_decisive)} belong to decisive portfolio runs, consistent "
        "with cancellation after another arm answered. They are excluded from the "
        "cap-censored count.", "",
        f"Backward and forward workers emit no final-record marker. Of their "
        f"{len(conventional)} records, {len(conventional_capped)} belong to capped "
        f"invocations and {len(conventional) - len(conventional_capped)} to uncapped "
        "invocations. The capped count establishes exposure to a campaign timeout, "
        "not how many individual backward/forward workers were killed or failed to "
        "finish. Their last capture can remain at preprocessing even if the game ran. "
        "Absence of a marker cannot distinguish normal return from termination for "
        "these arms, so no exact all-arm killed-worker count is identifiable from "
        "these files. Treat their last stages as potentially censored, not durations.", "",
    ]
    return "\n".join(lines)


def make_tsv(workers):
    counters = list(NUMERIC_FIELDS)
    counters += sorted({field for w in workers for field in w.numbers}
                       - set(counters) - {"k", "kmax"})
    fields = ["instance", "tier", "arm", "stage_reached", "status", "k",
              "campaign_result", "campaign_seconds", *counters, *ARM_FIELDS,
              "campaign_cap_s", "campaign_timed_out", "record_path"]
    stream = io.StringIO(newline="")
    writer = csv.DictWriter(stream, fields, delimiter="\t", lineterminator="\n")
    writer.writeheader()
    for worker in workers:
        row = {field: worker.record[field] for field in (*counters, *ARM_FIELDS, "status", "k")
               if field in worker.record}
        row.update(instance=worker.instance, tier=worker.tier, arm=worker.arm,
                   stage_reached=worker.record["stage"], campaign_result=worker.campaign["result"],
                   campaign_seconds=worker.campaign["seconds"],
                   campaign_cap_s=worker.campaign["cap_s"],
                   campaign_timed_out=worker.campaign["timed_out"], record_path=str(worker.path))
        writer.writerow(row)
    return stream.getvalue()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--campaign-dir", type=Path, default=Path("_bm-logs.20260912-s0-phases"))
    parser.add_argument(
        "--cohort", type=Path,
        default=Path("benchmarking/coverage-frontier-20260912/diagnostic-cohort.tsv"),
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--label", default="s0-phases")
    args = parser.parse_args()
    try:
        if args.output.resolve() == args.report.resolve():
            raise ValueError("--output and --report must be different paths")
        workers = load_workers(args)
        tsv = make_tsv(workers)
        report = make_report(workers, args.label)
        args.output.write_text(tsv, encoding="utf-8")
        args.report.write_text(report, encoding="utf-8")
    except (OSError, ValueError) as error:
        parser.exit(1, f"error: {error}\n")
    print(f"Wrote {len(workers)} worker rows to {args.output} and report to {args.report}")


if __name__ == "__main__":
    main()
