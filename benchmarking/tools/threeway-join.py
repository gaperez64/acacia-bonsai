#!/usr/bin/env python3
"""Join the closing comparison at one cap: public series plus internal baselines.

Every input is an observation TSV in run-syntcomp26-coverage.py's schema: a
coverage leg, a run-subset.py --raw-tsv leg (ltlsynt, Acacia 1.x), or a
tools/recycle-cap.py derived series.  Nothing here runs a solver.

  --series LABEL=TSV     a public series (the three-way), repeatable
  --internal LABEL=TSV   an internal baseline (for example the frozen E5), repeatable
  --new LABEL            the candidate that paired gains and losses are measured for
  --legacy-audit LABEL   a series whose solved verdicts are audited as Acacia 1.x's
                         (join-obfuscated-threeway.audit_solved_verdicts): a verdict
                         contradicting another series, the expected status, or its
                         own exit code is scored as ERROR at the cap

Scoring: REALIZABLE/UNREALIZABLE are solved; every other outcome costs twice the
cap (benchlib.par2, via cactus-report.summarize).  Expected verdicts come from
each TLSF's //STATUS, corrected by the status-exceptions table; the table is
applied here in scoring; campaign-orchestrate.py also passes the table to the
native runner for conflict collection. A solved verdict of a
non-audited series that contradicts the expected status or another series is a
conflict: the report is written, marked NOT FINAL, and the exit status is 3.

Timing boundary: series produced by run-subset.py's converted-pair routes
(preset ltlsynt or acacia1x) are timed on SyFCo output converted beforehand, as
in every earlier three-way, and a failed conversion is a SYFCO-FAIL charged as
unsolved.  Acacia's native TLSF frontend parses inside its timed run.  The
report states both, per series.

Derived rows (provenance recycled-*) are counted and labelled in every table.
PAR-2 differences are read against --noise-floor when one is supplied; the
documented floors were measured at 17 s, so none is assumed at another cap.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import importlib.util
import json
import math
import pathlib
import statistics
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "benchmarking"))


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


coverage = _load("syntcomp26_coverage", ROOT / "benchmarking/run-syntcomp26-coverage.py")
cactus = _load("cactus_report", ROOT / "benchmarking/cactus-report.py")
legacy_join = _load("join_obfuscated_threeway",
                    ROOT / "benchmarking/tools/join-obfuscated-threeway.py")
campaign = _load("p4_campaign_for_join",
                 ROOT / "benchmarking/tools/campaign-orchestrate.py")

SOLVED = frozenset({"REALIZABLE", "UNREALIZABLE"})
CACTUS_RESULT = {"MEMOUT": "RESOURCE_LIMIT", "CRASH": "ERROR"}
SUBTYPES = ("TIMEOUT", "MEMOUT", "UNKNOWN", "ERROR", "CRASH", "SYFCO-FAIL", "AUDIT-ERROR")
UNTIMED_CONVERSION_TOOLS = frozenset({"ltlsynt", "acacia1x"})
IDENTITY_FIELDS = ("solver_label", "acacia_sha", "binary_sha256", "preset", "flags",
                   "memory_max", "memory_swap_max")


class JoinError(Exception):
    """Inputs that cannot be joined honestly."""


def split_assignment(value, option):
    label, sep, path = value.partition("=")
    if not sep or not label.strip() or not path.strip():
        raise JoinError(f"{option} expects LABEL=PATH, got {value!r}")
    return label.strip(), pathlib.Path(path.strip())


def load_series(label, path, cap, instances):
    rows = coverage.load_output(path, allow_missing_memory=True)
    keyed = {}
    for row in rows:
        if row["instance"] in keyed:
            raise JoinError(f"{label}: duplicate instance {row['instance']}")
        try:
            seconds = float(row["seconds"])
        except ValueError:
            raise JoinError(f"{label}: invalid seconds for {row['instance']}") from None
        if not math.isfinite(seconds) or seconds < 0 or \
                (row["result"] in SOLVED and seconds > cap):
            raise JoinError(f"{label}: invalid measured time for {row['instance']}")
        keyed[row["instance"]] = row
    if set(keyed) != set(instances):
        missing = sorted(set(instances) - set(keyed))
        extra = sorted(set(keyed) - set(instances))
        raise JoinError(f"{label}: instance set differs from the list; "
                        f"missing {len(missing)} {missing[:5]}, extra {len(extra)} {extra[:5]}")
    caps = {row["cap_s"] for row in rows}
    if caps != {str(cap)}:
        raise JoinError(f"{label}: rows are at cap(s) {sorted(caps)}, not {cap}")
    identity = {}
    for field in IDENTITY_FIELDS:
        values = {row[field] for row in rows}
        if len(values) != 1:
            raise JoinError(f"{label}: mixed {field} {sorted(values)}")
        identity[field] = values.pop()
    derived = "provenance" in rows[0]
    if derived:
        source_rows = {}
        for name, row in keyed.items():
            source_cap = int(row["source_cap_s"])
            provenance = row["provenance"]
            valid = source_cap > 0 and (
                provenance == f"measured-{cap}s" and source_cap == cap or
                provenance == f"recycled-{source_cap}s" and source_cap < cap)
            if not valid:
                raise JoinError(f"{label}: invalid derived provenance for {name}")
            source = (path.parent / row["source_run"]).resolve()
            if source not in source_rows:
                original = coverage.load_output(source, allow_missing_memory=True)
                if any("provenance" in item for item in original):
                    raise JoinError(f"{label}: source run is itself derived: {source}")
                keyed_source = {}
                for item in original:
                    if item["instance"] in keyed_source:
                        raise JoinError(f"{label}: duplicate source instance {item['instance']}")
                    keyed_source[item["instance"]] = item
                source_rows[source] = (sha256_file(source), keyed_source)
            source_sha, originals = source_rows[source]
            if source_sha != row["source_run_sha256"]:
                raise JoinError(f"{label}: source run SHA-256 differs for {name}")
            original = originals.get(name)
            if original is None:
                raise JoinError(f"{label}: {name} absent from source run {source}")
            if original["cap_s"] != str(source_cap):
                raise JoinError(f"{label}: {name} source cap differs from derived provenance")
            for field in coverage.OUTPUT_COLUMNS + coverage.ROUTE_COLUMNS:
                if field == "cap_s" and provenance.startswith("recycled-"):
                    continue
                if row.get(field) != original.get(field):
                    raise JoinError(f"{label}: {name} derived {field} differs from source run")
    sources = sorted({(row["provenance"], row["source_run"], row["source_run_sha256"])
                      for row in rows}) if derived else []
    return {"label": label, "path": str(path), "sha256": sha256_file(path),
            "rows": keyed, "identity": identity, "derived": derived, "sources": sources}


def sha256_file(path):
    digest = hashlib.sha256()
    with pathlib.Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def corpus_digest(instances, mapping, corpus):
    digest = hashlib.sha256()
    for name in instances:
        if name not in mapping:
            raise JoinError(f"{name}: absent from the TLSF map")
        path = (corpus / mapping[name]).resolve()
        if not path.is_file() or not path.is_relative_to(corpus.resolve()):
            raise JoinError(f"{name}: missing TLSF in corpus: {path}")
        digest.update(json.dumps([name, str(path.relative_to(corpus.resolve())),
                                  sha256_file(path)], separators=(",", ":")).encode() + b"\n")
    return digest.hexdigest()


def row_source(row):
    cap = row.get("source_cap_s") or row.get("cap_s", "")
    return {"source_cap_s": cap,
            "source_run": row.get("source_run") or row.get("input_path", ""),
            "provenance": row.get("provenance") or f"measured-{cap}s"}


def expected_verdicts(instances, tlsf_map_path, corpus, exceptions_path):
    mapping = coverage.read_tlsf_map(tlsf_map_path)
    exceptions = coverage.read_status_exceptions(exceptions_path, corpus, required=True)
    expected, source = {}, {}
    for name in instances:
        if name not in mapping:
            raise JoinError(f"{name}: absent from the TLSF map")
        tlsf = mapping[name]
        if tlsf in exceptions:
            verdict, origin = exceptions[tlsf], "exception"
        else:
            verdict, origin = coverage.expected_verdict(corpus / tlsf), "status"
        if verdict is not None:
            expected[name] = verdict
            source[name] = origin
    return expected, source


def cactus_rows(series, cap):
    """Scoring view: cactus vocabulary, with the exit code for the audit."""
    view = {}
    for name, row in series["rows"].items():
        result = CACTUS_RESULT.get(row["result"], row["result"])
        seconds = row["seconds"] if result in SOLVED else str(cap)
        view[name] = {"result": result, "seconds": seconds, "exit": row["exit_code"],
                      "subtype": row["result"] if result not in SOLVED else "",
                      **row_source({**row, "input_path": series["path"]})}
    return view


def score(label, view, cap):
    runs = {name: cactus.RunResult(row["result"], float(row["seconds"]), row["exit"])
            for name, row in view.items()}
    return cactus.summarize(label, runs, cap)


def peak_memory(series):
    peaks = [int(row["scope_memory_peak_bytes"]) for row in series["rows"].values()
             if row.get("scope_memory_peak_bytes", "").isdigit()]
    return {"max_mib": round(max(peaks) / 2**20, 1) if peaks else None,
            "median_mib": round(statistics.median(peaks) / 2**20, 1) if peaks else None,
            "rows_with_peak": len(peaks), "rows_without_peak": len(series["rows"]) - len(peaks)}


def conversion_boundary(label, untimed, manifests):
    tools = {(item["series"].get("kind"), item["series"].get("tool"))
             for item in manifests
             if item.get("verification", "").startswith("campaign specification")}
    converted = {(kind, tool) for kind, tool in tools
                 if kind == "legacy" and tool in UNTIMED_CONVERSION_TOOLS}
    if label in untimed:
        if tools and any(kind in {"arm", "race"} or kind == "legacy" and
                         tool == "acacia" for kind, tool in tools):
            raise JoinError(f"{label}: --untimed-conversion conflicts with manifest runner")
        return ("SyFCo conversion declared outside the timed run (unverified); "
                "SYFCO-FAIL scored as unsolved")
    if tools and converted == tools:
        return "SyFCo conversion outside the timed run; SYFCO-FAIL scored as unsolved"
    if tools and all(kind in {"arm", "race"} or kind == "legacy" and tool == "acacia"
                     for kind, tool in tools):
        return "TLSF parsed inside the timed run (native frontend)"
    return "timing boundary unverified; supply a campaign manifest or --untimed-conversion"


def match_manifests(label, series, attached, cap, panel):
    """Tie manifest panels to the observed rows and their actual source files."""
    if not attached:
        return
    observed = series["identity"]
    for item in attached:
        frozen = item["series"]
        if frozen.get("binary_sha256") != observed["binary_sha256"] or \
                frozen.get("source_revision") != observed["acacia_sha"]:
            raise JoinError(f"{label}: frozen manifest binary or revision differs "
                            "from the observation")
        if item["verification"].startswith("campaign specification"):
            native = frozen["kind"] in {"arm", "race"}
            expected_flags = campaign.native_flags(frozen) if native else frozen["flags"]
            expected_preset = frozen["preset"] if native else frozen["tool"]
            if observed["flags"] != expected_flags or observed["preset"] != expected_preset or \
                    observed["memory_max"] != "8G" or observed["memory_swap_max"] != "0":
                raise JoinError(f"{label}: observation treatment differs from manifest")
    if not series["derived"]:
        for item in attached:
            if item["selected"] != panel:
                raise JoinError(f"{label}: manifest selected panel differs from observed rows")
            if item["cap_s"] != cap:
                raise JoinError(f"{label}: manifest cap differs from observation")
            if pathlib.Path(series["path"]).resolve() != \
                    (item["path"].parent / f"{item['key']}-{cap}s.tsv").resolve():
                raise JoinError(f"{label}: observation path differs from campaign manifest run")
        return
    covered = set()
    measured = {name for name, row in series["rows"].items()
                if row["provenance"] == f"measured-{cap}s"}
    recycled = {name for name, row in series["rows"].items()
                if row["provenance"] == "recycled-17s" and row["source_cap_s"] == "17"}
    if measured | recycled != panel or measured & recycled:
        raise JoinError(f"{label}: invalid derived row provenance or source cap")
    for item in attached:
        source_cap = item["cap_s"]
        if source_cap not in {17, cap}:
            raise JoinError(f"{label}: derived source manifest has wrong or missing cap")
        if source_cap == cap and cap == 17:
            raise JoinError(f"{label}: derived series needs a longer cap than its source")
        expected = measured if source_cap == cap else recycled
        source = (item["path"].parent / f"{item['key']}-{source_cap}s.tsv").resolve()
        source_rows = coverage.load_output(source, allow_missing_memory=True)
        source_names = [row["instance"] for row in source_rows]
        if len(source_names) != len(set(source_names)):
            raise JoinError(f"{label}: duplicate instance in source run {source}")
        if set(source_names) != item["selected"]:
            missing = sorted(item["selected"] - set(source_names))
            extra = sorted(set(source_names) - item["selected"])
            raise JoinError(f"{label}: source run panel differs from manifest: "
                            f"missing {missing[:5]}, extra {extra[:5]}")
        if any(row["cap_s"] != str(source_cap) for row in source_rows):
            raise JoinError(f"{label}: source run cap differs from manifest")
        linked = set()
        for name in expected:
            row = series["rows"][name]
            row_source = (pathlib.Path(series["path"]).parent / row["source_run"]).resolve()
            if row_source == source:
                if row["source_run_sha256"] != sha256_file(source):
                    raise JoinError(f"{label}: source run SHA-256 differs from manifest source")
                linked.add(name)
        if source_cap == cap:
            if item["selected"] != linked:
                raise JoinError(f"{label}: rerun manifest selected panel differs from "
                                "measured-60s rows")
        elif item["selected"] != panel or linked != panel - measured:
            raise JoinError(f"{label}: recycled rows differ from 17 s panel complement")
        if covered & linked:
            raise JoinError(f"{label}: overlapping source manifests")
        covered.update(linked)
    if covered != panel:
        raise JoinError(f"{label}: derived rows lack matching source manifests: "
                        f"{sorted(panel - covered)}")


def join(args):
    if type(args.cap) is not int or args.cap <= 0:
        raise JoinError("--cap must be a positive integer")
    instances = coverage.read_instance_list(args.list)
    if not instances or len(set(instances)) != len(instances):
        raise JoinError(f"{args.list}: empty list or duplicate instances")
    mapping = coverage.read_tlsf_map(args.tlsf_map)
    corpus_sha = corpus_digest(instances, mapping, args.tlsf_corpus)
    if args.noise_floor is not None:
        if not math.isfinite(args.noise_floor) or args.noise_floor < 0:
            raise JoinError("--noise-floor must be finite and nonnegative")
        source = args.noise_floor_source or ""
        if f"cap={args.cap}" not in source.split() or f"corpus={corpus_sha}" not in source.split():
            raise JoinError("--noise-floor-source must state matching cap=<seconds> "
                            "and corpus=<SHA-256>")
    public, internal = {}, {}
    for option, target in (("--series", public), ("--internal", internal)):
        for value in getattr(args, option.strip("-")) or []:
            label, path = split_assignment(value, option)
            if label in public or label in internal:
                raise JoinError(f"duplicate series label {label!r}")
            target[label] = load_series(label, path, args.cap, instances)
    if len(public) < 2:
        raise JoinError("at least two --series are required")
    everything = {**public, **internal}
    if args.new not in public:
        raise JoinError(f"--new {args.new!r} must name a --series")
    for label in args.legacy_audit or []:
        if label not in everything:
            raise JoinError(f"--legacy-audit {label!r} names no series")
    for label in args.untimed_conversion or []:
        if label not in everything:
            raise JoinError(f"--untimed-conversion {label!r} names no series")
    expected, expected_source = expected_verdicts(
        instances, args.tlsf_map, args.tlsf_corpus, args.status_exceptions)
    views = {label: cactus_rows(series, args.cap) for label, series in everything.items()}

    audits = {}
    for label in args.legacy_audit or []:
        others = {other: views[other] for other in everything if other != label
                  and other not in (args.legacy_audit or [])}
        audits[label] = legacy_join.audit_solved_verdicts(views[label], others, expected,
                                                          args.cap)
        for entry in audits[label]:
            views[label][entry["instance"]]["subtype"] = "AUDIT-ERROR"

    conflicts = []
    trusted = [label for label in everything if label not in (args.legacy_audit or [])]
    for name in instances:
        verdicts = {label: views[label][name]["result"] for label in trusted
                    if views[label][name]["result"] in SOLVED}
        for label, verdict in verdicts.items():
            if name in expected and verdict != expected[name]:
                conflicts.append({"instance": name, "series": label, "verdict": verdict,
                                  "against": f"expected ({expected_source[name]})",
                                  "other_verdict": expected[name]})
        if len(set(verdicts.values())) > 1:
            conflicts.append({"instance": name, "series": ",".join(sorted(verdicts)),
                              "verdict": ";".join(f"{k}={v}" for k, v in sorted(verdicts.items())),
                              "against": "each other", "other_verdict": ""})

    def solved(label, name):
        return views[label][name]["result"] in SOLVED

    report = {"cap_s": args.cap, "instances": len(instances), "new": args.new,
              "list": str(args.list), "list_sha256": sha256_file(args.list),
              "corpus_sha256": corpus_sha,
              "status_exceptions_sha256": sha256_file(args.status_exceptions),
              "noise_floor_s": args.noise_floor, "noise_floor_source": args.noise_floor_source,
              "verification": ("input, corpus and attached source bytes are checked; "
                               "solver outcomes, source revision, status correction authority "
                               "and any declared noise floor remain unverified"),
              "series": {}, "paired": {}, "conflicts": conflicts, "audits": audits}
    manifests = {}
    for path in args.manifest or []:
        record = json.loads(path.read_text())
        manifest_cap = record.get("cap_s")
        if type(manifest_cap) is not int or manifest_cap <= 0:
            raise JoinError(f"{path}: manifest needs a valid positive cap_s")
        if record.get("comparison"):
            try:
                selected_path = record["inputs"]["list"]["path"]
                rebuilt, _ = campaign.read_comparison(
                    pathlib.Path(record["comparison"]), manifest_cap,
                    selected=list(record["series"]), subset_list=selected_path)
            except (campaign.CampaignError, OSError, KeyError, TypeError,
                    ValueError) as error:
                raise JoinError(f"{path}: cannot verify campaign inputs: {error}") from error
            if rebuilt != record:
                raise JoinError(f"{path}: campaign manifest differs from current input content")
            if pathlib.Path(record["frozen_list"]).resolve() != args.list.resolve() or \
                    pathlib.Path(record["corpus"]).resolve() != args.tlsf_corpus.resolve() or \
                    record["inputs"]["tlsf_map"]["sha256"] != sha256_file(args.tlsf_map) or \
                    record["inputs"]["status_exceptions"]["sha256"] != \
                    report["status_exceptions_sha256"]:
                raise JoinError(f"{path}: join inputs differ from campaign manifest inputs")
            verification = "campaign specification and input bytes verified"
        else:
            verification = ("partial: no campaign specification; binary bytes, revision, "
                            "runner, runtime and converted inputs unverified")
        if record.get("frozen_list_sha256") != report["list_sha256"]:
            raise JoinError(f"{path}: frozen list SHA-256 differs from join observations")
        inputs = record.get("inputs")
        listed = inputs.get("list", {}) if isinstance(inputs, dict) else {}
        if not isinstance(listed, dict) or not listed.get("path") or \
                sha256_file(listed["path"]) != listed.get("sha256"):
            raise JoinError(f"{path}: selected list SHA-256 differs from manifest")
        selected = coverage.read_instance_list(pathlib.Path(listed["path"]))
        if len(selected) != len(set(selected)) or not set(selected) <= set(instances) or \
                record.get("corpus_sha256") != \
                corpus_digest(selected, mapping, args.tlsf_corpus):
            raise JoinError(f"{path}: corpus SHA-256 or selected list differs from observations")
        for key, value in record.get("series", {}).items():
            identity = {"manifest": str(path), "manifest_sha256": sha256_file(path),
                        "series": value, "list_sha256": record.get("frozen_list_sha256"),
                        "corpus_sha256": record.get("corpus_sha256"),
                        "verification": verification}
            manifests.setdefault(key, []).append({**identity, "path": path, "key": key,
                                                   "selected": set(selected),
                                                   "cap_s": manifest_cap})
    for label, series in everything.items():
        summary = score(label, views[label], args.cap)
        subtypes = {subtype: sum(row["subtype"] == subtype for row in views[label].values())
                    for subtype in SUBTYPES}
        group = public if label in public else internal
        exclusive = sum(solved(label, name) and not any(
            solved(other, name) for other in group if other != label) for name in instances)
        exclusive_all = sum(solved(label, name) and not any(
            solved(other, name) for other in everything if other != label) for name in instances)
        recycled = sum(row.get("provenance", "").startswith("recycled")
                       for row in series["rows"].values())
        entry = {
            "role": "public" if label in public else "internal",
            "solved": summary.solved, "real": summary.real, "unreal": summary.unreal,
            "par2_total_s": round(summary.par2, 6), "par2_mean_s": round(summary.par2_mean, 6),
            "solved_time_s": round(summary.solved_time, 6),
            "failure_subtypes": subtypes,
            "exclusive_solves_in_group": exclusive,
            "exclusive_solves_all_series": exclusive_all,
            "peak_memory": peak_memory(series),
            "derived": series["derived"], "recycled_rows": recycled,
            "measured_rows": len(instances) - recycled, "sources": series["sources"],
            "input": series["path"], "input_sha256": series["sha256"],
            "identity": series["identity"],
            "observation_verification": (
                "TSV bytes and cited source rows verified; solver outcome, source revision, "
                "and recycling eligibility or validation require independent evidence"
                if series["derived"] else
                "TSV bytes verified; solver outcome and source revision require independent evidence"),
        }
        manifest_key = series["identity"]["solver_label"]
        relevant = []
        if manifest_key in manifests:
            relevant = [item for item in manifests[manifest_key]
                        if series["derived"] or item["cap_s"] == args.cap]
            if relevant:
                match_manifests(label, series, relevant, args.cap, set(instances))
                for item in relevant:
                    if item["verification"].startswith("campaign specification"):
                        try:
                            campaign.verify_observation_labels(
                                item["series"], series["rows"].values(),
                                tlsf_map=args.tlsf_map, corpus=args.tlsf_corpus,
                                status_exceptions=args.status_exceptions)
                        except campaign.CampaignError as error:
                            raise JoinError(f"{label}: {error}") from error
            if relevant:
                entry["manifest_identity"] = [
                    {key: value for key, value in item.items()
                     if key not in {"path", "key", "selected", "cap_s"}}
                    for item in relevant]
        if series["derived"] and not relevant:
            raise JoinError(f"{label}: derived series needs its 17 s and rerun manifests")
        entry["timing_boundary"] = conversion_boundary(
            label, args.untimed_conversion or [], relevant)
        report["series"][label] = entry
    new = report["series"][args.new]
    for label in everything:
        if label == args.new:
            continue
        gains = [name for name in instances if solved(args.new, name) and not solved(label, name)]
        losses = [name for name in instances if solved(label, name) and not solved(args.new, name)]
        delta = new["par2_total_s"] - report["series"][label]["par2_total_s"]
        reading = "no noise floor supplied; not interpreted"
        if args.noise_floor is not None:
            reading = ("within the noise floor: not evidence on its own"
                       if abs(delta) <= args.noise_floor else "exceeds the noise floor")
        report["paired"][label] = {"gains": gains, "losses": losses,
                                   "par2_delta_s": round(delta, 6), "noise_reading": reading}
    return report, views


def render(report, labels_in_order):
    cap = report["cap_s"]

    def tag(label):
        entry = report["series"][label]
        return (f"{label} (derived: {entry['recycled_rows']} rows recycled)"
                if entry["derived"] else label)

    lines = []
    if report["conflicts"]:
        lines += [f"**NOT FINAL: {len(report['conflicts'])} verdict conflict(s) need "
                  "adjudication (see conflicts TSV).**", ""]
    lines += [f"# Closing comparison at {cap} s ({report['instances']} instances)", ""]
    for role, title in (("public", "Three-way"), ("internal", "Internal baselines")):
        labels = [label for label in labels_in_order if report["series"][label]["role"] == role]
        if not labels:
            continue
        lines += [f"## {title}", "",
                  "| Series | Solved | REAL | UNREAL | PAR-2 total (s) | PAR-2 mean (s) | "
                  + " | ".join(SUBTYPES) + " | Exclusive | Peak MiB (max / median) |",
                  "|---|" + "---:|" * (6 + len(SUBTYPES)) + "---|"]
        for label in labels:
            entry = report["series"][label]
            peak = entry["peak_memory"]
            lines.append(
                f"| {tag(label)} | {entry['solved']} | {entry['real']} | {entry['unreal']} | "
                f"{entry['par2_total_s']:.3f} | {entry['par2_mean_s']:.3f} | "
                + " | ".join(str(entry["failure_subtypes"][s]) for s in SUBTYPES)
                + f" | {entry['exclusive_solves_in_group']} | "
                f"{peak['max_mib']} / {peak['median_mib']} |")
        lines.append("")
    lines += [f"## Paired against {report['new']}", "",
              "| Other series | Gains | Losses | PAR-2 delta (s) | Reading |", "|---|---:|---:|---:|---|"]
    for label, pair in report["paired"].items():
        lines.append(f"| {tag(label)} | {len(pair['gains'])} | {len(pair['losses'])} | "
                     f"{pair['par2_delta_s']:+.3f} | {pair['noise_reading']} |")
    floor = report["noise_floor_s"]
    lines += ["", ("Noise floor: " + (f"{floor} s ({report['noise_floor_source']})."
                                      if floor is not None else
                                      "none supplied for this cap; PAR-2 differences are "
                                      "reported, not interpreted.")),
              "", "## Timing boundary and provenance", ""]
    for label in labels_in_order:
        entry = report["series"][label]
        identity = entry["identity"]
        lines.append(f"- **{tag(label)}**: {entry['timing_boundary']}. Binary SHA-256 "
                     f"`{identity['binary_sha256']}`, revision `{identity['acacia_sha']}`, "
                     f"preset `{identity['preset']}`, flags `{identity['flags']}`, "
                     f"MemoryMax {identity['memory_max']}, swap {identity['memory_swap_max']}. "
                     f"Input `{entry['input']}` ({entry['input_sha256']}).")
        lines.append(f"  Verification: {entry['observation_verification']}.")
        if "manifest_identity" in entry:
            lines.append(f"  Campaign manifest: `{json.dumps(entry['manifest_identity'], sort_keys=True)}`.")
        for provenance, source, digest in entry["sources"]:
            lines.append(f"  - {provenance}: `{source}` ({digest})")
    for label, audit in report["audits"].items():
        lines.append(f"- {label}: {len(audit)} solved verdict(s) failed the legacy audit and "
                     "are scored as ERROR at the cap (AUDIT-ERROR).")
    lines.append("- Status exceptions were applied in join scoring and passed by the "
                 "orchestrator to the native runner for conflict collection.")
    lines.append(f"- Verification limits: {report['verification']}.")
    return "\n".join(lines) + "\n"


def write_outputs(report, views, out, labels_in_order):
    out.mkdir(parents=True, exist_ok=True)
    cap = report["cap_s"]
    stem = out / f"threeway-{cap}s"
    stem.with_suffix(".json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    stem.with_suffix(".md").write_text(render(report, labels_in_order))
    with open(f"{stem}-conflicts.tsv", "w", newline="") as stream:
        writer = csv.DictWriter(stream, ["instance", "series", "verdict", "against",
                                         "other_verdict"], delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(report["conflicts"])
    with open(f"{stem}-gains-losses.tsv", "w", newline="") as stream:
        writer = csv.writer(stream, delimiter="\t", lineterminator="\n")
        writer.writerow(["other_series", "kind", "instance", "new_result", "new_seconds",
                         "new_source_cap_s", "new_source_run", "new_provenance",
                         "other_result", "other_seconds", "other_source_cap_s",
                         "other_source_run", "other_provenance"])
        for label, pair in report["paired"].items():
            for kind in ("gains", "losses"):
                for name in pair[kind]:
                    new, other = views[report["new"]][name], views[label][name]
                    new_source = row_source(new)
                    other_source = row_source(other)
                    writer.writerow([label, kind, name, new["result"], new["seconds"],
                                     *new_source.values(), other["result"], other["seconds"],
                                     *other_source.values()])
    for label, audit in report["audits"].items():
        with open(f"{stem}-audit-{slug(label)}.tsv", "w", newline="") as stream:
            columns = ["instance", "verdict", "exit", "contradicts_tools", "expected_status",
                       "contradicts_expected", "unexpected_exit", "signal_crash"]
            writer = csv.DictWriter(stream, columns, delimiter="\t", lineterminator="\n")
            writer.writeheader()
            writer.writerows(audit)
    for label in labels_in_order:
        # Scored cactus CSVs, for cactus-report.py plots; audit rescoring included.
        with open(out / f"{slug(label)}-{cap}s-scored.csv", "w", newline="") as stream:
            writer = csv.writer(stream)
            writer.writerow(["instance", "result", "seconds", "exit", "source_cap_s",
                             "source_run", "provenance"])
            for name, row in views[label].items():
                writer.writerow([name, row["result"], row["seconds"], row["exit"],
                                 *row_source(row).values()])
    return stem


def slug(label):
    return "".join(ch if ch.isalnum() or ch in "._-" else "_" for ch in label)


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--cap", required=True, type=int)
    parser.add_argument("--list", required=True, type=pathlib.Path)
    parser.add_argument("--series", action="append", metavar="LABEL=TSV")
    parser.add_argument("--internal", action="append", metavar="LABEL=TSV")
    parser.add_argument("--new", required=True)
    parser.add_argument("--legacy-audit", action="append", metavar="LABEL")
    parser.add_argument("--untimed-conversion", action="append", metavar="LABEL",
                        help="declare a series timed on pre-converted inputs when no "
                             "campaign manifest supplies the runner identity")
    parser.add_argument("--tlsf-map", required=True, type=pathlib.Path)
    parser.add_argument("--tlsf-corpus", required=True, type=pathlib.Path)
    parser.add_argument("--status-exceptions", required=True, type=pathlib.Path)
    parser.add_argument("--noise-floor", type=float,
                        help="same-configuration PAR-2 spread for this corpus and cap (s)")
    parser.add_argument("--noise-floor-source", default="",
                        help="where the floor was measured; required with --noise-floor")
    parser.add_argument("--manifest", action="append", type=pathlib.Path,
                        help="campaign-orchestrate.py manifest, for binary identities "
                             "behind wrappers")
    parser.add_argument("--out", required=True, type=pathlib.Path)
    return parser


def main(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.noise_floor is not None and not args.noise_floor_source:
        parser.error("--noise-floor needs --noise-floor-source")
    try:
        report, views = join(args)
    except (JoinError, coverage.CoverageError, OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    labels = list(report["series"])
    stem = write_outputs(report, views, args.out, labels)
    print(stem.with_suffix(".md").read_text(), end="")
    print(f"wrote {stem}.md, {stem}.json and sidecars")
    if report["conflicts"]:
        print(f"NOT FINAL: {len(report['conflicts'])} verdict conflict(s)", file=sys.stderr)
        return 3
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
