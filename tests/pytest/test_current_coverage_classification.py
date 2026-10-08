"""Synthetic identity, polarity, censoring and evidence-boundary regressions."""
from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import sys

import pytest

BENCHMARKING = Path(__file__).resolve().parents[2] / "benchmarking"
sys.path.insert(0, str(BENCHMARKING))
spec = importlib.util.spec_from_file_location(
    "current_coverage_classification", BENCHMARKING / "classify-coverage.py")
classification = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = classification
spec.loader.exec_module(classification)
sys.path.pop(0)


def row(result, seconds="1", tlsf="source.tlsf"):
    return {"result": result, "seconds": seconds, "tlsf_file": tlsf}


def meta(name="logical", **updates):
    return {"instance": name, "tlsf_file": "source.tlsf", "tlsf_bytes": 100,
            "inputs": 2, "outputs": 1, "parameter_dimension": 0, **updates}


def test_partition_polarity_and_near_cap():
    names = ["name-unreal-but-real", "a", "b", "unreal-unresolved", "near"]
    metadata = {name: meta(name) for name in names}
    a = dict(zip(names, [row("TIMEOUT", "17"), row("REALIZABLE"), row("UNREALIZABLE"),
                         row("UNKNOWN"), row("UNREALIZABLE", "15")]))
    b = dict(zip(names, [row("REALIZABLE"), row("TIMEOUT", "17"), row("UNREALIZABLE"),
                         row("UNKNOWN"), row("UNREALIZABLE")]))
    result = {r["instance"]: r for r in classification.classify(names, metadata, a, b, 17)}
    assert result[names[0]]["set"] == "ltlsynt-only"
    assert result[names[0]]["polarity"] == "REAL"
    assert result["a"]["set"] == "Acacia-only"
    assert result["b"]["near_cap"] == "false"
    assert result["near"]["near_cap"] == "true"
    assert result["unreal-unresolved"]["polarity"] == "UNKNOWN"
    assert result["unreal-unresolved"]["set"] == "both-unsolved"


def test_conflicts_and_source_identity_fail_closed():
    with pytest.raises(ValueError, match="opposing"):
        classification.observed_polarity([row("REALIZABLE"), row("UNREALIZABLE")])
    with pytest.raises(ValueError, match="source-map"):
        classification.classify(["a"], {"a": meta("a")}, {"a": row("UNKNOWN")},
                                {"a": row("UNKNOWN", tlsf="different.tlsf")}, 17)
    with pytest.raises(ValueError, match="row set"):
        classification.classify(["a"], {"a": meta("a")}, {}, {"a": row("UNKNOWN")}, 17)
    with pytest.raises(ValueError, match="wall time"):
        classification.classify(["a"], {"a": meta("a")}, {"a": row("REALIZABLE", "18")},
                                {"a": row("UNKNOWN")}, 17)


def test_metadata_uses_manifest_parameters_and_ignores_names(tmp_path):
    source = tmp_path / "opaque.tlsf"
    source.write_text('INFO { TITLE: "PARAMETERS { fake = 3; }" }\nMAIN { }')
    digest = classification.join.sha256_file(source)
    manifest = {source.name: {"origin": "param:templates/opaque.tlsf:n=7,m=2",
                              "sha256": digest}}
    conversion = {source.name: {"source_sha256": digest, "inputs": "2", "outputs": "1",
                                "semantics": "Mealy", "effective_target": "Mealy"}}
    mapping = {"renamed-with-no-numbers": source.name}
    result = classification.metadata(list(mapping), mapping, manifest, conversion, tmp_path)
    assert result[next(iter(mapping))]["declared_parameters"] == "true"
    assert json.loads(result[next(iter(mapping))]["parameters_json"]) == {"n": 7, "m": 2}
    manifest[source.name]["origin"] = "direct:templates/opaque.tlsf"
    result = classification.metadata(list(mapping), mapping, manifest, conversion, tmp_path)
    assert result[next(iter(mapping))]["declared_parameters"] == "false"
    source.write_text("MAIN { PARAMETERS { n = 7; } }")
    manifest[source.name]["sha256"] = conversion[source.name]["source_sha256"] = \
        classification.join.sha256_file(source)
    result = classification.metadata(list(mapping), mapping, manifest, conversion, tmp_path)
    assert result[next(iter(mapping))]["declared_parameters"] == "true"
    manifest[source.name]["sha256"] = "0" * 64
    with pytest.raises(ValueError, match="digest"):
        classification.metadata(list(mapping), mapping, manifest, conversion, tmp_path)


def test_seeded_sample_is_order_independent_and_structural():
    rows = [meta(str(i)) for i in range(8)] + [meta("large", tlsf_bytes=100000, inputs=100)]
    first = classification.structural_sample(rows, 3, 207)
    assert first == classification.structural_sample(list(reversed(rows)), 3, 207)
    assert "large" in {r["instance"] for r in first}
    assert len({r["instance"] for r in first}) == 3
    assert classification.structural_sample(rows, 0, 207) == []


def test_missing_counters_and_completed_phase_are_not_stop_causes():
    result = classification.phase_attribution([
        {"phase": "child_startup"}, {"phase": "trusted_prepare", "work_count": 0}])
    assert result["first_stop_category"] == "UNKNOWN"
    assert result["last_observed_stage"] == "trusted_prepare"
    assert result["recorded_route"] == "UNKNOWN"
    assert classification.phase_attribution([])["last_observed_stage"] == "UNKNOWN"


def test_first_explicit_stop_and_route_summary_are_kept_separate():
    result = classification.phase_attribution([
        {"phase": "budget_decline", "stage": "budget-structure"},
        {"phase": "reduce_decline_budget-structure"}, {"phase": "record_summary"}])
    assert result["first_stop_category"] == "reduction resources"
    result = classification.phase_attribution([
        {"phase": "route_direct"}, {"phase": "decline_schema_abi"},
        {"phase": "final_checks", "work_count": 0}])
    assert result["first_stop_category"] == "schema fitting"
    assert result["recorded_route"] == "direct"
    assert result["last_observed_stage"] == "final_checks"
    assert classification.stop_category("new-unrecorded-cause") == "UNKNOWN"


def test_converted_alias_is_explicit_identity_and_bad_exit_is_rejected():
    a, b = {"logical": row("UNKNOWN")}, {"logical": row("REALIZABLE", tlsf="logical")}
    result = classification.classify(["logical"], {"logical": meta()}, a, b, 17)
    assert result[0]["set"] == "ltlsynt-only"
    b["logical"]["exit_code"] = "1"
    with pytest.raises(ValueError, match="exit mismatch"):
        classification.classify(["logical"], {"logical": meta()}, a, b, 17)


def test_cli_writes_synthetic_tables_and_report(tmp_path):
    source = tmp_path / "source.tlsf"
    source.write_text("MAIN { PARAMETERS { n = 2; } }")
    digest = classification.join.sha256_file(source)
    classification.write_tsv(tmp_path / "map.tsv", ["instance", "tlsf"],
                             [{"instance": "logical", "tlsf": source.name}])
    classification.write_tsv(tmp_path / "manifest.tsv", ["instance", "origin", "sha256"],
                             [{"instance": source.name, "origin": "direct:source.tlsf",
                               "sha256": digest}])
    converted = {"instance": source.name, "source_sha256": digest, "inputs": "1",
                 "outputs": "1", "semantics": "Mealy", "effective_target": "Mealy"}
    classification.write_tsv(tmp_path / "conversion.tsv", list(converted), [converted])
    (tmp_path / "all.list").write_text("logical\n")
    series = []
    for role, verdict, exit_code in [("acacia", "UNKNOWN", "2"),
                                      ("comparator", "REALIZABLE", "0")]:
        observation = dict.fromkeys(classification.join.coverage.OUTPUT_COLUMNS, "")
        observation.update(solver_label=role, instance="logical", tlsf_file=source.name,
                           cap_s="17", result=verdict, exit_code=exit_code, seconds="1",
                           expectation_source="none")
        path = tmp_path / f"{role}.tsv"
        classification.write_tsv(path, list(observation), [observation])
        series.extend(["--series", f"17:{role}={path}"])
    args = []
    for option, filename in [("list", "all.list"), ("tlsf-map", "map.tsv"),
                             ("manifest", "manifest.tsv"), ("conversion", "conversion.tsv")]:
        args.extend([f"--{option}", str(tmp_path / filename)])
    args.extend(["--corpus", str(tmp_path), "--out", str(tmp_path / "out"), *series])
    assert classification.main(args) == 0
    assert "| 17 | 0 | 1 | 1 | 0 | 0 | 0 |" in (tmp_path / "out/coverage-report.md").read_text()
    assert (tmp_path / "out/ltlsynt-only-17s.tsv").is_file()
    provenance = json.loads((tmp_path / "out/classification-provenance.json").read_text())
    assert provenance["arguments"] == args


def evidence_fixture(tmp_path, flags="--arms real:small:forward", **updates):
    observation = {
        "solver_label": "arbitrary", "instance": "opaque", "tlsf_file": "source.tlsf",
        "cap_s": "60", "run_index": "1", "result": "TIMEOUT", "seconds": "60.1",
        "exit_code": "124", "flags": flags, "binary_sha256": "old-binary", **updates,
    }
    classification.write_tsv(tmp_path / "observations.tsv", list(observation), [observation])
    entry = {"campaign": "renamed-input", "root": str(tmp_path),
             "rows": "observations.tsv", "phase_root": "phases", "arm": "",
             "regime": "historical", "comparability": "different binary"}
    classification.write_tsv(tmp_path / "evidence.tsv", list(entry), [entry])
    directory = classification.phase_record_dir(
        tmp_path / "phases", observation["solver_label"], observation["cap_s"],
        observation["instance"], observation["run_index"])
    directory.mkdir(parents=True)
    return tmp_path / "evidence.tsv", directory


def test_solo_legacy_identity_requires_explicit_single_arm(tmp_path):
    manifest, directory = evidence_fixture(tmp_path)
    (directory / "1.jsonl").write_text(
        '{"arm":"legacy","phase":"child_startup"}\n'
        '{"arm":"legacy","phase":"record_summary","dropped_records":0}\n')
    records = classification.load_phase_records(directory)
    detail = classification.arm_attribution(records, "real:small:forward", standalone=True)
    assert detail["last_observed_stage"] == "child_startup"
    assert detail["first_terminal_obstruction"] == "UNKNOWN"
    assert detail["producer_arm"] == "legacy"
    detail = classification.arm_attribution(records, "real:small:forward")
    assert detail["last_observed_stage"] == "UNKNOWN"
    observations, hashes = classification.load_evidence(
        manifest, {"opaque": "source.tlsf"}, {"opaque"}, {})
    assert observations[0]["context"] == "standalone"
    assert observations[0]["first_stop_category"] == "UNKNOWN"
    assert str(directory / "1.jsonl") in hashes
    assert classification.requested_arms('--arms=a,b -q') == ["a", "b"]


@pytest.mark.parametrize("result, exit_code, winner", [
    ("REALIZABLE", "0", "real:small:forward"),
    ("UNREALIZABLE", "1", "unreal:automaton:forward"),
])
def test_solved_race_preserves_declining_loser(tmp_path, result, exit_code, winner):
    loser = "both:gr1:oxidd"
    manifest, directory = evidence_fixture(
        tmp_path, flags=f"--arms {winner},{loser}", result=result, seconds="2",
        exit_code=exit_code)
    for index, (arm, phases) in enumerate([
        (winner, ["child_startup", "final_checks"]),
        (loser, ["child_startup", "reduce_decline_mp-class"]),
    ]):
        events = [{"arm": arm, "phase": phase} for phase in phases]
        events.append({"arm": arm, "phase": "record_summary", "dropped_records": 0})
        (directory / f"{index}.jsonl").write_text(
            "".join(json.dumps(event) + "\n" for event in events))
    observations, hashes = classification.load_evidence(
        manifest, {"opaque": "source.tlsf"}, {"opaque"}, {})
    by_arm = {observation["arm"]: observation for observation in observations}
    decline = by_arm[loser]
    assert decline["result"] == result
    assert decline["context"] == "race"
    assert decline["records_complete"] is True
    assert decline["first_terminal_obstruction"] == "exact-reduction rejection"
    assert decline["terminal_reason"] == "mp-class"
    assert decline["first_stop_category"] == "exact-reduction rejection"
    assert decline["last_observed_stage"] == "reduce_decline_mp-class"
    assert by_arm[winner]["first_terminal_obstruction"] == "UNKNOWN"
    assert str(directory / "1.jsonl") in hashes


def test_multi_producer_order_and_missing_events_stay_unknown(tmp_path):
    _, directory = evidence_fixture(tmp_path)
    for i, phase in enumerate(["reduce_decline_mp-class", "trusted_prepare"]):
        (directory / f"{i}.jsonl").write_text(json.dumps({"arm": "native", "phase": phase}))
    detail = classification.arm_attribution(classification.load_phase_records(directory), "native")
    assert detail["processes"] == 2
    assert detail["last_observed_stage"] == "UNKNOWN"
    assert detail["first_terminal_obstruction"] == "UNKNOWN"


def test_evidence_identity_conflicts_and_escape_fail_closed(tmp_path):
    manifest, _ = evidence_fixture(tmp_path, result="REALIZABLE", seconds="1", exit_code="0")
    with pytest.raises(ValueError, match="opposing"):
        classification.load_evidence(manifest, {"opaque": "source.tlsf"}, {"opaque"},
                                     {"opaque": [row("UNREALIZABLE")]})
    with pytest.raises(ValueError, match="source identity"):
        classification.load_evidence(manifest, {"opaque": "other.tlsf"}, {"opaque"}, {})
    with pytest.raises(ValueError, match="escapes"):
        classification.evidence_path(tmp_path, "../outside.tsv")


def test_explicit_alias_map_and_unknown_timeout_cause(tmp_path):
    manifest, _ = evidence_fixture(tmp_path, instance="raw-alias", tlsf_file="/original/source")
    aliases = {"row_instance": "raw-alias", "row_tlsf": "/original/source",
               "instance": "opaque", "tlsf_file": "source.tlsf"}
    classification.write_tsv(tmp_path / "aliases.tsv", list(aliases), [aliases])
    entry = classification.table_rows(manifest)[0]
    entry["row_identity_map"] = "aliases.tsv"
    classification.write_tsv(manifest, list(entry), [entry])
    observations, _ = classification.load_evidence(
        manifest, {"opaque": "source.tlsf"}, {"opaque"}, {})
    assert observations[0]["instance"] == "opaque"
    assert observations[0]["row_instance"] == "raw-alias"
    assert observations[0]["last_observed_stage"] == "UNKNOWN"
    assert observations[0]["first_terminal_obstruction"] == "UNKNOWN"
    aliases["row_tlsf"] = "/wrong/source"
    classification.write_tsv(tmp_path / "aliases.tsv", list(aliases), [aliases])
    with pytest.raises(ValueError, match="alias/source"):
        classification.load_evidence(manifest, {"opaque": "source.tlsf"}, {"opaque"}, {})


def test_historical_standalone_success_never_proves_race_only_interference():
    missing = [{"instance": "opaque", "cap_s": 17}]
    observation = {"instance": "opaque", "context": "standalone", "result": "UNREALIZABLE",
                   "seconds": 16, "evidence_cap_s": 60, "campaign": "old",
                   "arm": "unreal:formula:spot-guarded-sparse", "row_path": "source.tsv",
                   "binary_sha256": "different-binary", "regime": "older Spot"}
    result = classification.standalone_boundary(missing, [observation])[0]
    assert result["within_cap_historical_solves"] == 1
    assert result["race_only_interference_proven"] == "UNKNOWN"
    assert result["pattern"] == "historical standalone solve / current race miss"
    observation["seconds"] = 18
    assert classification.standalone_boundary(missing, [observation])[0][
        "within_cap_historical_solves"] == 0


def probe_fixture(tmp_path, diagnostic):
    worker = {"instance": "opaque", "worker_formula": "G request", "polarity": "unreal",
              "transform": "formula", "requested_backend": "forward", "stage": "before-translation"}
    (tmp_path / "worker.json").write_text(json.dumps(worker))
    (tmp_path / "formula.txt").write_text("G request\n")
    (tmp_path / "diagnostic.txt").write_text(diagnostic)
    entry = {"campaign": "arbitrary-probe", "root": str(tmp_path), "instance": "opaque",
             "arm": "unreal:formula:forward", "worker_record": "worker.json",
             "formula": "formula.txt", "diagnostic": "diagnostic.txt", "regime": "probe"}
    classification.write_tsv(tmp_path / "probes.tsv", list(entry), [entry])
    return tmp_path / "probes.tsv"


def test_probe_requires_formula_binding_and_explicit_exception(tmp_path):
    manifest = probe_fixture(tmp_path, "simplified promise_occurrences=71 unique_promises=71\n"
                             "translation_error=Too many acceptance sets used.  The limit is 64.\n")
    observations, _ = classification.load_probes(manifest, {"opaque": "source.tlsf"})
    assert observations[0]["first_terminal_obstruction"] == "translation size"
    assert observations[0]["unique_promises"] == "71"
    assert observations[0]["worker_snapshot_stage"] == "before-translation"
    (tmp_path / "formula.txt").write_text("F request\n")
    with pytest.raises(ValueError, match="binding"):
        classification.load_probes(manifest, {"opaque": "source.tlsf"})


def test_snapshot_and_counts_do_not_establish_translation_timeout(tmp_path):
    manifest = probe_fixture(tmp_path, "simplified promise_occurrences=71 unique_promises=71\n")
    result = classification.load_probes(manifest, {"opaque": "source.tlsf"})[0][0]
    assert result["last_observed_stage"] == "formula simplified"
    assert result["first_terminal_obstruction"] == "UNKNOWN"
    (tmp_path / "diagnostic.txt").write_text("translation_error=unsupported syntax\n")
    assert classification.load_probes(manifest, {"opaque": "source.tlsf"})[0][0][
        "first_terminal_obstruction"] == "translation exception"


def test_worker_snapshot_only_establishes_its_recorded_stage(tmp_path):
    manifest, _ = evidence_fixture(tmp_path)
    entry = classification.table_rows(manifest)[0]
    entry["worker_root"] = "workers"
    classification.write_tsv(manifest, list(entry), [entry])
    snapshot_dir = tmp_path / "workers/arbitrary/60/opaque"
    snapshot_dir.mkdir(parents=True)
    snapshot = {"instance": "opaque", "polarity": "real", "transform": "small",
                "requested_backend": "forward", "stage": "before-translation"}
    (snapshot_dir / "1.json").write_text(json.dumps(snapshot))
    observations, hashes = classification.load_evidence(
        manifest, {"opaque": "source.tlsf"}, {"opaque"}, {})
    assert observations[0]["last_observed_stage"] == "before-translation"
    assert observations[0]["first_terminal_obstruction"] == "UNKNOWN"
    assert str(snapshot_dir / "1.json") in hashes


def test_cli_accepts_generic_evidence_arguments(tmp_path):
    test_cli_writes_synthetic_tables_and_report(tmp_path)
    auxiliary = tmp_path / "auxiliary"
    auxiliary.mkdir()
    manifest, _ = evidence_fixture(auxiliary, instance="logical", result="REALIZABLE",
                                  seconds="1", exit_code="0")
    arguments = json.loads((tmp_path / "out/classification-provenance.json").read_text())[
        "arguments"]
    arguments += ["--evidence-manifest", str(manifest)]
    assert classification.main(arguments) == 0
    result = classification.table_rows(tmp_path / "out/standalone-race-boundary.tsv")[0]
    assert result["within_cap_historical_solves"] == "1"
    assert result["race_only_interference_proven"] == "UNKNOWN"
    assert classification.table_rows(tmp_path / "out/classification-17s.tsv")[0][
        "view"] == "measured"
    assert "17 s measured" in (tmp_path / "out/coverage-report.md").read_text()


def test_solved_race_is_reported_without_entering_missing_census(tmp_path):
    test_cli_writes_synthetic_tables_and_report(tmp_path)
    primary = classification.table_rows(tmp_path / "acacia.tsv")[0]
    primary.update(result="REALIZABLE", exit_code="0")
    classification.write_tsv(tmp_path / "acacia.tsv", list(primary), [primary])
    auxiliary = tmp_path / "auxiliary"
    auxiliary.mkdir()
    manifest, directory = evidence_fixture(
        auxiliary, instance="logical", result="REALIZABLE", seconds="2", exit_code="0",
        flags="--arms real:small:forward,both:gr1:oxidd")
    entry = classification.table_rows(manifest)[0]
    entry["selection"] = "all"
    classification.write_tsv(manifest, list(entry), [entry])
    (directory / "1.jsonl").write_text(
        '{"arm":"both:gr1:oxidd","phase":"reduce_decline_mp-class"}\n'
        '{"arm":"both:gr1:oxidd","phase":"record_summary","dropped_records":0}\n')
    arguments = json.loads((tmp_path / "out/classification-provenance.json").read_text())[
        "arguments"]
    arguments[arguments.index("--out") + 1] = str(tmp_path / "solved-out")
    arguments += ["--evidence-manifest", str(manifest)]
    assert classification.main(arguments) == 0
    output = tmp_path / "solved-out"
    observations = classification.table_rows(output / "missing-evidence-observations.tsv")
    loser = next(r for r in observations if r["arm"] == "both:gr1:oxidd")
    assert loser["result"] == "REALIZABLE"
    assert loser["first_terminal_obstruction"] == "exact-reduction rejection"
    assert loser["terminal_reason"] == "mp-class"
    assert loser["records_complete"] == "True"
    assert classification.table_rows(output / "ltlsynt-only-17s.tsv") == []
    assert all(r["missing"] == "0" for r in classification.table_rows(output / "missing-split.tsv"))
    assert classification.table_rows(output / "classification-17s.tsv")[0]["set"] == "common-solved"
