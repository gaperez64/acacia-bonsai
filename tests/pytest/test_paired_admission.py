"""Synthetic saved screens in the coverage runner's schema; no solver execution."""

import csv
import hashlib
import importlib.util
import json
from pathlib import Path

import pytest


SCRIPT = Path(__file__).resolve().parents[2] / "benchmarking" / "paired-admission.py"
SPEC = importlib.util.spec_from_file_location("paired_admission", SCRIPT)
admission = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(admission)
runner = admission.coverage
MIB = admission.MIB


def samples(times=(1,) * 5, status="REALIZABLE", memory=None, rss=None):
    return [{"result": status, "seconds": str(seconds),
             "scope_memory_peak_bytes": "" if memory is None else str(memory),
             "max_process_rss_bytes": "" if rss is None else str(rss)} for seconds in times]


def write_screen(path, cases=None, cap=17):
    cases = cases or {"a.ltl": (samples(), samples((0.8,) * 5))}
    (path / "targets.list").write_text("\n".join(cases) + "\n")
    rounds = len(next(iter(cases.values()))[0])
    for number in range(1, rounds + 1):
        for side, label in enumerate(("A", "B")):
            rows = []
            for instance, pair in cases.items():
                sample = pair[side][number - 1]
                status = sample["result"]
                rows.append({
                    **dict.fromkeys(runner.OUTPUT_COLUMNS, ""), **sample,
                    "solver_label": f"test-{label}-r{number}", "instance": instance,
                    "tlsf_file": instance + ".tlsf", "cap_s": str(cap),
                    "exit_code": str(runner.TOOL_EXIT_CODES["acacia"].get(status, 124)),
                    "timed_out": str(status == "TIMEOUT").lower(), "expectation_source": "none",
                    "binary_sha256": label.lower() * 64, "acacia_sha": "frozen-" + label,
                    "preset": "test", "collect_rusage": "true", "memory_max": "8G",
                    "memory_swap_max": "0", "run_index": str(len(rows)),
                })
            raw = path / f"r{number}-{label}.tsv"
            save_round(raw, rows, cap)
            runner.atomic_write_tsv(raw.with_name(f"{raw.stem}-conflicts.tsv"), runner.CONFLICT_COLUMNS, [])
    return path


def save_round(path, rows, cap=17):
    runner.atomic_write_tsv(path, runner.OUTPUT_COLUMNS, rows)
    runner.write_summary(path, rows[0]["solver_label"], [row["instance"] for row in rows], rows, cap)


def cli(path, *extra, cap=17):
    args = ["--screen-dir", str(path), "--control", "A", "--treatment", "B",
            "--rounds", "5", "--cap", str(cap), *extra]
    return admission.main(args)


def evaluate(path, decision, *extra):
    assert cli(path, *extra) == 0  # All decisions, including REJECT, are successful evaluations.
    report = (path / "admission.md").read_text()
    assert f"Decision: **{decision}**" in report
    with (path / "admission.tsv").open() as stream:
        rows = {row["instance"]: row for row in csv.DictReader(stream, delimiter="\t")}
    return rows, report


def test_clean_runtime_admit_and_audit(tmp_path):
    rows, report = evaluate(write_screen(tmp_path), "ADMIT")
    row = rows["a.ltl"]
    assert row["control_solved"] == row["treatment_solved"] == "5/5"
    assert json.loads(row["control_verdicts"]) == ["REALIZABLE"]
    assert float(row["treatment_median_seconds"]) == 0.8
    assert json.loads(row["paired_delta_seconds"]) == pytest.approx([-0.2] * 5)
    assert row["lower_rounds_seconds"] == "5"
    assert row["higher_rounds_seconds"] == "0"
    assert json.loads(row["faster_rounds"]) == [1, 2, 3, 4, 5]
    assert json.loads(row["slower_rounds"]) == []
    assert row["control_noise_seconds"] == "0.0"
    assert row["runtime_benefit"] == "true"
    assert row["control_median_scope_memory_peak_bytes"] == "unavailable"
    assert json.loads(row["control_scope_memory_peak_bytes"]) == ["unavailable"] * 5
    assert "median(abs(control sample - median(control samples)))" in report
    for path in [tmp_path / "targets.list", *tmp_path.glob("r*.tsv")]:
        assert hashlib.sha256(path.read_bytes()).hexdigest() in report


def test_coverage_is_set_rule_not_net_gain(tmp_path):
    timeout = samples((0.01,) * 5, "TIMEOUT")
    rows, report = evaluate(write_screen(tmp_path, {
        "gain1": (timeout, samples()), "gain2": (timeout, samples()),
        "loss": (samples(), timeout),
    }), "REJECT")
    assert "unequivocal regression" in rows["loss"]["flags"]
    assert rows["gain1"]["coverage_benefit"] == "false"
    assert json.loads(rows["loss"]["treatment_seconds"]) == [17] * 5
    assert "| No regression | FAIL |" in report


def test_coverage_gain_alone_admits(tmp_path):
    rows, _ = evaluate(write_screen(tmp_path, {"a": (
        samples((0.1,) * 5, "UNKNOWN"), samples(),
    )}), "ADMIT")
    assert rows["a"]["coverage_benefit"] == "true"


@pytest.mark.parametrize("rounds", [1, 4, 5])
@pytest.mark.parametrize("benefit", ["coverage", "runtime", "cohort", "memory"])
def test_admission_requires_five_confirmation_pairs(tmp_path, rounds, benefit):
    cases = {
        "coverage": (samples((17,) * rounds, "TIMEOUT"), samples((1,) * rounds)),
        "runtime": (samples((1,) * rounds), samples((0.8,) * rounds)),
        "cohort": (samples((0.5,) * rounds), samples((0.475,) * rounds)),
        "memory": (samples((1,) * rounds, memory=128 * MIB),
                   samples((1,) * rounds, memory=100 * MIB)),
    }
    _, report = evaluate(write_screen(tmp_path, {"a": cases[benefit]}),
                         "ADMIT" if rounds == 5 else "UNRESOLVED", "--rounds", str(rounds))
    if rounds < 5:
        assert "| Improvement | UNRESOLVED |" in report
        assert f"Incomplete confirmation: {rounds}/5 paired rounds" in report


@pytest.mark.parametrize("status", ["TIMEOUT", "UNREALIZABLE"])
def test_short_screen_definite_failures_still_reject(tmp_path, status):
    evaluate(write_screen(tmp_path, {"a": (samples((1,)), samples((1,), status))}),
             "REJECT", "--rounds", "1")


@pytest.mark.parametrize("slower_rounds,decision,validated", [(4, "REJECT", "true"), (3, "UNRESOLVED", "false")])
def test_slowdown_confirmation(tmp_path, slower_rounds, decision, validated):
    times = [1.2] * slower_rounds + [0.9] * (5 - slower_rounds)
    rows, _ = evaluate(write_screen(tmp_path, {
        "slow": (samples(), samples(times)), "gain": (samples(), samples((0.7,) * 5)),
    }), decision)
    assert rows["slow"]["regression_flag_seconds"] == "true"
    assert rows["slow"]["validated_regression_seconds"] == validated
    assert rows["slow"]["higher_rounds_seconds"] == str(slower_rounds)


@pytest.mark.parametrize("control,treatment", [(1, 1.05), (0.2, 0.25), (2, 2.1)])
def test_equal_slowdown_resolution_is_not_flagged(tmp_path, control, treatment):
    rows, _ = evaluate(write_screen(tmp_path, {"a": (
        samples((control,) * 5), samples((treatment,) * 5),
    )}), "REJECT")
    assert rows["a"]["regression_flag_seconds"] == "false"


@pytest.mark.parametrize("side", ["across", "control", "treatment"])
def test_verdict_disagreement_fails_correctness(tmp_path, side):
    before, after = samples(), samples()
    if side == "across":
        after = samples(status="UNREALIZABLE")
    else:
        (before if side == "control" else after)[2]["result"] = "UNREALIZABLE"
    rows, report = evaluate(write_screen(tmp_path, {"a": (before, after)}), "REJECT")
    assert rows["a"]["correctness_fail"] == "true"
    assert "| Correctness (observed verdicts) | FAIL |" in report


@pytest.mark.parametrize("control_count,treatment_count", [(5, 4), (4, 3), (1, 0)])
def test_lower_success_rate_blocks(tmp_path, control_count, treatment_count):
    before = samples()[:control_count] + samples(status="TIMEOUT")[control_count:]
    after = samples()[:treatment_count] + samples(status="TIMEOUT")[treatment_count:]
    rows, report = evaluate(write_screen(tmp_path, {"a": (before, after)}), "UNRESOLVED")
    assert "lower success rate — unresolved, blocks admission" in rows["a"]["flags"]
    assert "| No regression | UNRESOLVED |" in report


@pytest.mark.parametrize("control_count", [3, 4])
def test_mixed_near_cap_losses_block_despite_equal_or_better_success_count(tmp_path, control_count):
    before = samples((16,) * control_count) + samples((17,) * (5 - control_count), "TIMEOUT")
    after = samples((17,), "TIMEOUT") + samples((16,) * 4)
    rows, report = evaluate(write_screen(tmp_path, {
        "near-cap": (before, after), "gain": (samples(), samples((0.8,) * 5)),
    }), "UNRESOLVED")
    assert json.loads(rows["near-cap"]["lost_verdict_rounds"]) == [1]
    assert "mixed paired verdict losses — unresolved, blocks admission" in rows["near-cap"]["flags"]
    assert rows["gain"]["runtime_benefit"] == "true"
    assert "| No regression | UNRESOLVED |" in report


def test_matching_intermittent_outcomes_have_no_paired_loss(tmp_path):
    intermittent = samples((16,) * 4) + samples((17,), "TIMEOUT")
    rows, _ = evaluate(write_screen(tmp_path, {
        "near-cap": (intermittent, intermittent), "gain": (samples(), samples((0.8,) * 5)),
    }), "ADMIT")
    assert json.loads(rows["near-cap"]["lost_verdict_rounds"]) == []


@pytest.mark.parametrize("status", sorted(admission.FAILURES))
def test_new_failure_rejects_even_without_coverage_loss(tmp_path, status):
    _, report = evaluate(write_screen(tmp_path, {
        "failure": (samples(status="TIMEOUT"), samples(status=status)),
        "gain": (samples(), samples((0.7,) * 5)),
    }), "REJECT")
    assert f"new/increased failure: {status}" in report


def test_memory_improvement_with_unchanged_coverage(tmp_path):
    rows, _ = evaluate(write_screen(tmp_path, {"a": (
        samples(memory=128 * MIB, rss=80 * MIB), samples(memory=100 * MIB, rss=72 * MIB),
    )}), "ADMIT")
    assert rows["a"]["memory_benefit"] == "true"
    assert float(rows["a"]["control_median_scope_memory_peak_bytes"]) == 128 * MIB
    assert json.loads(rows["a"]["treatment_max_process_rss_bytes"]) == [72 * MIB] * 5


@pytest.mark.parametrize("kind", ["rss_only", "partial", "coverage_changed", "time_regression", "noisy"])
def test_memory_benefit_requirements(tmp_path, kind):
    before, after = samples(memory=128 * MIB), samples(memory=100 * MIB)
    cases = {"a": (before, after)}
    if kind == "rss_only":
        cases = {"a": (samples(rss=128 * MIB), samples(rss=100 * MIB))}
    elif kind == "partial":
        after[0]["scope_memory_peak_bytes"] = ""
    elif kind == "coverage_changed":
        cases["gain"] = (samples(status="TIMEOUT"), samples())
    elif kind == "time_regression":
        cases["slow"] = (samples(), samples((1.2,) * 5))
    else:
        for row, memory in zip(before, [80, 90, 128, 166, 176]):
            row["scope_memory_peak_bytes"] = str(memory * MIB)
    decision = "ADMIT" if kind == "coverage_changed" else "REJECT"
    rows, _ = evaluate(write_screen(tmp_path, cases), decision)
    assert rows["a"]["memory_benefit"] == "false"
    if kind == "partial":
        assert rows["a"]["treatment_median_scope_memory_peak_bytes"] == "unavailable"


def test_validated_memory_growth_blocks_runtime_gain(tmp_path):
    rows, _ = evaluate(write_screen(tmp_path, {"a": (
        samples(memory=100 * MIB), samples((0.8,) * 5, memory=128 * MIB),
    )}), "REJECT")
    assert rows["a"]["validated_regression_scope_memory_peak_bytes"] == "true"


def test_missing_memory_columns_are_unavailable(tmp_path):
    write_screen(tmp_path)
    columns = [column for column in runner.OUTPUT_COLUMNS
               if column not in {"scope_memory_peak_bytes", "max_process_rss_bytes"}]
    for path in tmp_path.glob("r?-?.tsv"):
        rows = runner.load_output(path)
        runner.atomic_write_tsv(path, columns, [{key: row[key] for key in columns} for row in rows])
    rows, _ = evaluate(tmp_path, "ADMIT")
    assert rows["a.ltl"]["control_median_scope_memory_peak_bytes"] == "unavailable"
    assert rows["a.ltl"]["treatment_median_max_process_rss_bytes"] == "unavailable"


@pytest.mark.parametrize("control,treatment,benefit", [(1, 0.95, True), (2, 1.94, False), (0.8, 0.76, False)])
def test_runtime_instance_requires_both_inclusive_thresholds(tmp_path, control, treatment, benefit):
    write_screen(tmp_path, {"a": (samples((control,) * 5), samples((treatment,) * 5))})
    assert cli(tmp_path) == 0
    with (tmp_path / "admission.tsv").open() as stream:
        row = next(csv.DictReader(stream, delimiter="\t"))
    assert row["runtime_benefit"] == str(benefit).lower()


@pytest.mark.parametrize("wins", [3, 4])
def test_runtime_instance_requires_four_paired_wins(tmp_path, wins):
    rows, _ = evaluate(write_screen(tmp_path, {"a": (
        samples(), samples([0.8] * wins + [1.01] * (5 - wins)),
    )}), "ADMIT" if wins == 4 else "REJECT")
    assert rows["a"]["lower_rounds_seconds"] == str(wins)
    assert rows["a"]["runtime_benefit"] == str(wins == 4).lower()


@pytest.mark.parametrize("control,treatment,benefit", [(160, 136, True), (100, 85, False), (200, 172, False)])
def test_memory_requires_both_inclusive_thresholds(tmp_path, control, treatment, benefit):
    rows, _ = evaluate(write_screen(tmp_path, {"a": (
        samples(memory=control * MIB), samples(memory=treatment * MIB),
    )}), "ADMIT" if benefit else "REJECT")
    assert rows["a"]["memory_benefit"] == str(benefit).lower()


def test_no_improvement_rejects(tmp_path):
    _, report = evaluate(write_screen(tmp_path, {"a": (samples(), samples())}), "REJECT")
    assert "No qualifying improvement" in report
    assert "| Correctness (observed verdicts) | PASS |" in report
    assert "| No regression | PASS |" in report


@pytest.mark.parametrize("ratio,decision", [(0.95, "ADMIT"), (0.95001, "REJECT"), (0.94999, "ADMIT")])
def test_geomean_boundary_without_instance_benefit(tmp_path, ratio, decision):
    rows, report = evaluate(write_screen(tmp_path, {
        "a": (samples((0.5,) * 5), samples((0.5 * ratio,) * 5)),
        "b": (samples((0.6,) * 5), samples((0.6 * ratio,) * 5)),
    }), decision)
    assert all(row["runtime_benefit"] == "false" for row in rows.values())
    assert ("runtime_cohort_benefit" in report) == (decision == "ADMIT")


def test_cohort_requires_four_paired_wins(tmp_path):
    _, report = evaluate(write_screen(tmp_path, {"a": (
        samples((0.5,) * 5), samples((0.475, 0.475, 0.475, 0.51, 0.51)),
    )}), "REJECT")
    assert '"faster_rounds": 3' in report


def test_noise_is_local_control_mad_and_blocks_benefit(tmp_path):
    rows, _ = evaluate(write_screen(tmp_path, {"a": (
        samples((0.1, 0.5, 1, 1.5, 1.9)), samples((0.01, 0.4, 0.9, 1.4, 1.8)),
    )}), "REJECT")
    assert float(rows["a"]["control_noise_seconds"]) == 0.5
    assert rows["a"]["lower_rounds_seconds"] == "5"


def test_only_declared_benefit_targets_can_claim_runtime(tmp_path):
    screen = write_screen(tmp_path, {
        "stable": (samples(), samples()), "gain": (samples(), samples((0.5,) * 5)),
    })
    benefits = tmp_path / "benefits.list"
    benefits.write_text("stable\n")
    rows, report = evaluate(screen, "REJECT", "--benefit-targets", str(benefits))
    assert rows["gain"]["benefit_target"] == "false"
    assert hashlib.sha256(benefits.read_bytes()).hexdigest() in report


@pytest.mark.parametrize("problem", ["copied_rounds", "swapped_rounds", "wrong_side", "changed_tag"])
def test_recorded_round_identity_must_match_files(tmp_path, capsys, problem):
    write_screen(tmp_path)
    if problem in {"copied_rounds", "swapped_rounds"}:
        for suffix in (".tsv", "-summary.tsv", "-conflicts.tsv"):
            if problem == "copied_rounds":
                for label in ("A", "B"):
                    original = (tmp_path / f"r1-{label}{suffix}").read_bytes()
                    for number in range(2, 6):
                        (tmp_path / f"r{number}-{label}{suffix}").write_bytes(original)
            else:
                first, last = (tmp_path / f"r{number}-B{suffix}" for number in (1, 5))
                first_bytes, last_bytes = first.read_bytes(), last.read_bytes()
                first.write_bytes(last_bytes)
                last.write_bytes(first_bytes)
    else:
        raw = tmp_path / "r3-B.tsv"
        rows = runner.load_output(raw)
        for row in rows:
            row["solver_label"] = "test-A-r3" if problem == "wrong_side" else "different-B-r3"
        save_round(raw, rows)
    (tmp_path / "admission.md").write_text("keep markdown")
    (tmp_path / "admission.tsv").write_text("keep TSV")
    with pytest.raises(SystemExit) as error:
        cli(tmp_path)
    assert error.value.code == 2
    assert "solver_label" in capsys.readouterr().err
    assert (tmp_path / "admission.md").read_text() == "keep markdown"
    assert (tmp_path / "admission.tsv").read_text() == "keep TSV"


@pytest.mark.parametrize("problem", [
    "missing_round", "missing_summary", "missing_conflicts", "extra_round", "duplicate_round",
    "extra_id", "duplicate_id", "partial_round", "staged", "cap60", "conflict", "bad_exit",
    "bad_memory", "bad_seconds", "failed_done", "changed_binary", "changed_regime",
    "changed_source", "duplicate_targets", "malformed_row",
])
def test_invalid_inputs_leave_reports_untouched(tmp_path, problem):
    write_screen(tmp_path)
    raw = tmp_path / "r3-B.tsv"
    rows = runner.load_output(raw)
    if problem in {"missing_round", "missing_summary", "missing_conflicts"}:
        suffix = {"missing_round": ".tsv", "missing_summary": "-summary.tsv",
                  "missing_conflicts": "-conflicts.tsv"}[problem]
        (tmp_path / (raw.stem + suffix)).unlink()
    elif problem in {"extra_round", "duplicate_round"}:
        name = "r6-B.tsv" if problem == "extra_round" else "r03-B.tsv"
        (tmp_path / name).write_bytes(raw.read_bytes())
    elif problem == "conflict":
        runner.atomic_write_tsv(tmp_path / "r3-B-conflicts.tsv", runner.CONFLICT_COLUMNS, [{
            "solver_label": "test-B-r3", "instance": "a.ltl", "tlsf_file": "a.tlsf",
            "cap_s": "17", "expected": "UNREALIZABLE", "actual": "REALIZABLE",
            "seconds": "0.8", "expectation_source": "status",
        }])
    elif problem == "failed_done":
        raw.with_suffix(".tsv.done").write_text("3\n")
    elif problem == "duplicate_targets":
        (tmp_path / "targets.list").write_text("a.ltl\na.ltl\n")
    elif problem == "malformed_row":
        with raw.open("a") as stream:
            stream.write("partial\trow\n")
    else:
        if problem in {"extra_id", "duplicate_id", "staged"}:
            rows.append({**rows[0]})
            if problem == "extra_id":
                rows[-1]["instance"] = "extra"
            if problem == "staged":
                rows[-1]["cap_s"] = "1"
        elif problem == "partial_round":
            rows = []
        else:
            field, value = {
                "cap60": ("cap_s", "60"), "bad_exit": ("exit_code", "1"),
                "bad_memory": ("scope_memory_peak_bytes", "nan"),
                "bad_seconds": ("seconds", "inf"), "changed_binary": ("binary_sha256", "changed"),
                "changed_regime": ("memory_max", "16G"), "changed_source": ("tlsf_file", "different"),
            }[problem]
            rows[0][field] = value
        runner.atomic_write_tsv(raw, runner.OUTPUT_COLUMNS, rows)
    (tmp_path / "admission.md").write_text("keep markdown")
    (tmp_path / "admission.tsv").write_text("keep TSV")
    with pytest.raises(SystemExit) as error:
        cli(tmp_path)
    assert error.value.code != 0
    assert (tmp_path / "admission.md").read_text() == "keep markdown"
    assert (tmp_path / "admission.tsv").read_text() == "keep TSV"


@pytest.mark.parametrize("option,value", [("--cap", "60"), ("--rounds", "0"), ("--control", "B")])
def test_invalid_cli_options(tmp_path, option, value):
    write_screen(tmp_path)
    with pytest.raises(SystemExit) as error:
        cli(tmp_path, option, value)
    assert error.value.code == 2


def test_non_17s_cap_requires_research_protocol_label(tmp_path):
    write_screen(tmp_path, cap=120)
    with pytest.raises(SystemExit) as error:
        cli(tmp_path, cap=120)
    assert error.value.code == 2


def test_research_protocol_label_forbidden_at_historical_cap(tmp_path):
    write_screen(tmp_path)
    with pytest.raises(SystemExit) as error:
        cli(tmp_path, "--research-protocol", "long-budget-120s")
    assert error.value.code == 2


def test_nonpositive_or_infinite_cap_rejected(tmp_path):
    write_screen(tmp_path, cap=120)
    with pytest.raises(SystemExit) as error:
        cli(tmp_path, "--research-protocol", "x", cap=0)
    assert error.value.code == 2


def test_long_budget_cap_admits_with_label_and_is_labelled(tmp_path):
    write_screen(tmp_path, cap=120)
    assert cli(tmp_path, "--research-protocol", "long-budget-120s", cap=120) == 0
    report = (tmp_path / "admission.md").read_text()
    assert "Decision: **ADMIT**" in report
    assert "cap: 120 s; protocol: `long-budget-120s`" in report
    result = json.loads((tmp_path / "admission-result.json").read_text())
    assert result == {
        "decision": "ADMIT", "correctness": "PASS", "no_regression": "PASS",
        "improvement": "PASS", "confirmation_complete": True,
        "cap": 120.0, "protocol": "long-budget-120s", "rounds": 5,
    }


def test_historical_cap_result_json_labelled_and_present_on_every_decision(tmp_path):
    rows, report = evaluate(write_screen(tmp_path), "ADMIT")
    result = json.loads((tmp_path / "admission-result.json").read_text())
    assert result["protocol"] == "historical-17s"
    assert result["cap"] == 17.0
    assert result["decision"] == "ADMIT"
    # Exit 0 alone is not ADMIT: a REJECT decision also exits 0 and still
    # needs the machine-readable field, not a grep of the prose.
    evaluate(write_screen(tmp_path, {"a": (samples(), samples())}), "REJECT")
    result = json.loads((tmp_path / "admission-result.json").read_text())
    assert result["decision"] == "REJECT"


def test_120s_rows_rejected_under_17s_protocol(tmp_path):
    # A screen recorded at 120s must not be silently accepted as a 17s
    # confirmation just because no --cap was passed.
    write_screen(tmp_path, cap=120)
    with pytest.raises(SystemExit) as error:
        cli(tmp_path)  # defaults to --cap 17
    assert error.value.code != 0
