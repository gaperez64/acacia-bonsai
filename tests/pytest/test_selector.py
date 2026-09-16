from __future__ import annotations

import csv
import importlib.util
import os
import pathlib
import random
import subprocess
import sys

import pytest


BENCHMARKING = pathlib.Path(__file__).resolve().parents[2] / "benchmarking"
sys.path.insert(0, str(BENCHMARKING))


def load_script(name):
    spec = importlib.util.spec_from_file_location(name.replace("-", "_"), BENCHMARKING / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


folds_script = load_script("selector-folds")
stump = load_script("selector-stump")


def write_tsv(path, columns, rows):
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=columns, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def read_tsv(path):
    with path.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        return reader.fieldnames, list(reader)


def rewrite_tsv(path, change):
    columns, rows = read_tsv(path)
    change(rows)
    write_tsv(path, columns, rows)


def family_fixture(tmp_path):
    rows = []
    for family, size, directory in (
        ("alpha", 5, "shared"), ("beta", 3, "shared"), ("gamma", 2, "shared"),
        ("delta", 4, "elsewhere"), ("epsilon", 3, None), ("zeta", 2, None),
    ):
        for index in range(size):
            name = f"{family}-{index}.ltl"
            rows.append({
                "logical_instance": name, "family_key": family,
                "origin_kind": "direct" if directory else "param",
                "origin": f"direct:tlsf/{directory}/{name}.tlsf" if directory else "param:template:n=1",
            })
    listing, families = tmp_path / "all.list", tmp_path / "families.tsv"
    listing.write_text("# comment\n\n  " + "\n".join(row["logical_instance"] for row in rows) + "\n")
    write_tsv(families, list(rows[0]), rows)
    return listing, families


def test_folds_keep_groups_and_ignore_input_order(tmp_path):
    listing, families = family_fixture(tmp_path)
    output = tmp_path / "folds.tsv"
    argv = ["--list", str(listing), "--family-table", str(families),
            "--folds", "3", "--output", str(output)]
    assert folds_script.main(argv) == 0
    first = output.read_bytes()
    _, rows = read_tsv(output)
    for group_key, fold_key in (("family_key", "fold_family"), ("directory_key", "fold_directory")):
        memberships = {}
        for row in rows:
            memberships.setdefault(row[group_key], set()).add(row[fold_key])
        assert all(len(folds) == 1 for folds in memberships.values())
    shared = [row for row in rows if row["directory_key"] == "direct:tlsf/shared"]
    assert {row["family_key"] for row in shared} == {"alpha", "beta", "gamma"}
    assert {row["fold_directory"] for row in shared} == {"0"}
    assert {row["fold_family"] for row in rows if row["family_key"] == "alpha"} == {"0"}
    assert all(row["directory_key"] == row["family_key"] for row in rows
               if row["family_key"] in {"epsilon", "zeta"})

    shuffled = [row["instance"] for row in rows]
    random.Random(19).shuffle(shuffled)
    listing.write_text("# shuffled\n\n" + "\n".join(shuffled) + "\n")
    rewrite_tsv(families, random.Random(23).shuffle)
    assert folds_script.main(argv) == 0
    assert output.read_bytes() == first


def test_fold_assignment_uses_sha256_ties_and_smallest_fold():
    # SHA-256 orders these equal-size keys c, b, a. The last group returns to fold 0.
    assert folds_script.assign_groups(["a", "b", "c"], 2) == {"c": 0, "b": 1, "a": 0}


def test_folds_read_only_list_and_family_table(tmp_path, monkeypatch):
    listing, families = family_fixture(tmp_path)
    opened = []
    original_open = pathlib.Path.open

    def record_open(path, *args, **kwargs):
        opened.append(path)
        return original_open(path, *args, **kwargs)

    calls = []
    original_read_list = folds_script.read_list

    def record_read_list(path):
        calls.append(path)
        return original_read_list(path)

    monkeypatch.setattr(pathlib.Path, "open", record_open)
    monkeypatch.setattr(folds_script, "read_list", record_read_list)
    assert len(folds_script.build(listing, families, 3)) == 19
    assert calls == [listing]
    assert opened == [listing, families]


@pytest.mark.parametrize("option", ["--labels", "--results", "--label"])
def test_folds_parser_rejects_label_arguments(option, capsys):
    parser = folds_script.argument_parser()
    with pytest.raises(SystemExit) as error:
        parser.parse_args(["--list", "all.list", "--family-table", "families.tsv",
                           "--output", "folds.tsv", option, "labels.tsv"])
    assert error.value.code == 2
    assert "unrecognized arguments" in capsys.readouterr().err
    assert {action.dest for action in parser._actions} == {
        "help", "list", "family_table", "folds", "output",
    }


def test_missing_family_is_clear_error(tmp_path, capsys):
    listing, families = family_fixture(tmp_path)
    listing.write_text("missing.ltl\n")
    with pytest.raises(SystemExit) as error:
        folds_script.main(["--list", str(listing), "--family-table", str(families),
                           "--output", str(tmp_path / "folds.tsv")])
    assert error.value.code == 2
    assert "'missing.ltl' missing from family table" in capsys.readouterr().err


def selector_fixture(tmp_path, cases, names=("inputs",)):
    """Cases are (instance, fold, feature tuple or None, A result, B result).

    A None result omits that label row; None features omit the source file row.
    TLSF filenames deliberately differ from logical instance names.
    """
    fold_rows, family_rows, feature_rows, labels = [], [], [], []
    for name, fold, values, result_a, result_b in cases:
        filename = f"source-{name}.tlsf"
        fold_rows.append({"instance": name, "family_key": name, "fold_family": fold,
                          "directory_key": name, "fold_directory": fold})
        family_rows.append({"logical_instance": name, "tlsf_file": filename})
        if values is not None:
            feature_rows.append({"file": filename, "schema_version": "2", "parse_status": "ok",
                                 **dict(zip(names, values))})
        for pair, result in (("A", result_a), ("B", result_b)):
            if result is not None:
                decisive = result in {"REALIZABLE", "UNREALIZABLE"}
                labels.append({"solver_label": pair, "instance": name, "repetition_id": "1",
                               "decisive_result": result if decisive else "",
                               "still_unsolved_at_max_cap": "false" if decisive else "true"})
    write_tsv(tmp_path / "folds.tsv", folds_script.COLUMNS, fold_rows)
    write_tsv(tmp_path / "families.tsv", ["logical_instance", "tlsf_file"], family_rows)
    write_tsv(tmp_path / "features.tsv", ["file", "schema_version", "parse_status", *names], feature_rows)
    write_tsv(tmp_path / "labels.tsv", ["solver_label", "instance", "repetition_id",
                                      "decisive_result", "still_unsolved_at_max_cap"], labels)
    return ["--folds", str(tmp_path / "folds.tsv"), "--fold-column", "fold_family",
            "--features", str(tmp_path / "features.tsv"),
            "--family-table", str(tmp_path / "families.tsv"),
            "--labels", str(tmp_path / "labels.tsv"), "--pairs", "A,B", "--depth", "1",
            "--output", str(tmp_path / "predictions.tsv"), "--report", str(tmp_path / "report.md")]


def assess(argv):
    args = stump.argument_parser().parse_args(argv)
    names, instances = stump.load_instances(args)
    return stump.evaluate(instances, names, args.pairs, args.depth)


def summary_fixture(tmp_path, cases):
    """Match run-portfolio-pairs.py's summary, including blank run metadata."""
    argv = selector_fixture(tmp_path, cases)
    labels = []
    pairs = ("rf-ugf", "rf-ufa")
    arms = ("real:small:forward,unreal:formula:spot-guarded-sparse",
            "real:small:forward,unreal:automaton:forward")
    for name, _, _, result_a, result_b in cases:
        for pair, arm_tokens, result in zip(pairs, arms, (result_a, result_b)):
            if result is None:
                continue
            decisive = result in {"REALIZABLE", "UNREALIZABLE"}
            labels.append({
                "solver_label": pair, "instance": name,
                "smallest_cap_solved": "17" if decisive else "",
                "decisive_result": result if decisive else "",
                "decisive_seconds": "0.25" if decisive else "",
                "still_unsolved_at_max_cap": "false" if decisive else "true",
                "failure_kind_at_max_cap": result, "max_cap_s": "17",
                "pair_id": pair, "arm_tokens": arm_tokens,
                "repetition_id": "1" if decisive else "",
                "order_index": str(len(labels)) if decisive else "",
            })
    write_tsv(tmp_path / "labels.tsv", [
        "solver_label", "instance", "smallest_cap_solved", "decisive_result",
        "decisive_seconds", "still_unsolved_at_max_cap", "failure_kind_at_max_cap", "max_cap_s",
        "pair_id", "arm_tokens", "repetition_id", "order_index",
    ], labels)
    return [*argv, "--pairs", ",".join(pairs)]


@pytest.mark.parametrize("failure_kind", ["TIMEOUT", "UNKNOWN", "ERROR", "CRASH"])
def test_empty_decisive_summary_row_is_unsolved_not_missing(tmp_path, failure_kind):
    cases = [(f"one-{fold}", fold, (0,), "UNREALIZABLE", failure_kind) for fold in range(2)]
    argv = summary_fixture(tmp_path, cases)
    _, labels = read_tsv(tmp_path / "labels.tsv")
    unsolved = [row for row in labels if row["solver_label"] == "rf-ufa"]
    assert all(row["decisive_result"] == row["repetition_id"] == "" for row in unsolved)
    assert all(row["failure_kind_at_max_cap"] == failure_kind for row in unsolved)
    output, scores, training, _ = assess(argv)
    assert scores[-1]["labelled"] == scores[-1]["pair_a"] == 2
    assert scores[-1]["missing"] == scores[-1]["pair_b"] == 0
    assert [row["eligible"] for row in training] == [1, 1, 2]
    assert all(row["rf-ufa_solved"] == "false" for row in output)


def test_both_pairs_unsolved_summary_is_labelled_and_cannot_be_solved(tmp_path):
    cases = [(f"neither-{fold}", fold, (0,), "TIMEOUT", "ERROR") for fold in range(2)]
    output, scores, training, _ = assess(summary_fixture(tmp_path, cases))
    assert [row["labelled"] for row in scores] == [1, 1, 2]
    assert [row["eligible"] for row in training] == [1, 1, 2]
    assert all(row[key] == 0 for row in scores
               for key in ("missing", "selector", "pair_a", "pair_b", "default", "oracle"))
    assert all(row["rf-ugf_solved"] == row["rf-ufa_solved"] == row["choice_solved"] == "false"
               for row in output)


def test_summary_missing_means_no_pair_row(tmp_path):
    cases = [
        ("present", 0, (0,), "TIMEOUT", "ERROR"),
        ("missing-a", 0, (0,), None, "ERROR"),
        ("missing-b", 1, (0,), "TIMEOUT", None),
        ("missing-both", 1, (0,), None, None),
    ]
    output, scores, training, _ = assess(summary_fixture(tmp_path, cases))
    assert scores[-1]["labelled"] == training[-1]["eligible"] == 1
    assert scores[-1]["missing"] == 3
    assert scores[-1]["selector"] == scores[-1]["oracle"] == 0
    by_name = {row["instance"]: row for row in output}
    assert by_name["present"]["choice_solved"] == "false"
    assert by_name["missing-a"]["rf-ufa_solved"] == "false"
    assert by_name["missing-b"]["rf-ugf_solved"] == "false"
    for name, pair in (("missing-a", "rf-ugf"), ("missing-b", "rf-ufa"),
                       ("missing-both", "rf-ugf"), ("missing-both", "rf-ufa")):
        assert by_name[name][f"{pair}_solved"] == by_name[name]["choice_solved"] == ""


def mixed_summary_cases():
    return [(f"f{fold}-{name}", fold, (x,), a, b) for fold in range(2) for name, x, a, b in (
        ("ugf-only", 0, "UNREALIZABLE", "TIMEOUT"),
        ("ufa-only", 4, "TIMEOUT", "UNREALIZABLE"),
        ("both", 2, "REALIZABLE", "REALIZABLE"),
        ("neither", 3, "UNKNOWN", "ERROR"),
        ("real-only", 6, "TIMEOUT", "REALIZABLE"),
    )]


def test_summary_per_fold_counts_and_totals_are_consistent(tmp_path):
    cases = mixed_summary_cases() + [(f"missing-{fold}", fold, (1,), "UNREALIZABLE", None)
                                     for fold in range(2)]
    output, scores, training, _ = assess(summary_fixture(tmp_path, cases))
    for row in scores:
        assert row["selector"] <= row["oracle"] <= row["labelled"]
        assert row["pair_a"] <= row["oracle"]
        assert row["pair_b"] <= row["oracle"]
        assert row["default"] <= row["oracle"]
    for score in scores[:-1]:
        held_out = [row for row in output if row["fold"] == score["fold"]]
        assert score["labelled"] == 5
        assert score["missing"] == 1
        assert score["labelled"] + score["missing"] == len(held_out)
        labelled = [row for row in held_out if row["choice_solved"] != ""]
        assert score["selector"] == sum(row["choice_solved"] == "true" for row in labelled)
        for key, pair in (("pair_a", "rf-ugf"), ("pair_b", "rf-ufa")):
            assert score[key] == sum(row[f"{pair}_solved"] == "true" for row in labelled)
        assert score["oracle"] == sum(
            row["rf-ugf_solved"] == "true" or row["rf-ufa_solved"] == "true" for row in labelled)
    for key in ("labelled", "selector", "pair_a", "pair_b", "default", "oracle", "missing"):
        assert scores[-1][key] == sum(row[key] for row in scores[:-1])
    assert scores[-1]["labelled"] + scores[-1]["missing"] == len(cases)
    assert [row["realizable_only"] for row in training] == [1, 1, 2]
    assert [row["eligible"] for row in training] == [4, 4, 8]


def test_real_summary_schema_cli_regression(tmp_path):
    argv = summary_fixture(tmp_path, mixed_summary_cases())
    assert stump.main(argv) == 0
    columns, predictions = read_tsv(tmp_path / "predictions.tsv")
    assert columns == ["instance", "fold", "chosen_pair", "rf-ugf_solved", "rf-ufa_solved",
                       "choice_solved"]
    assert len(predictions) == 10
    assert all(row["choice_solved"] != "" for row in predictions)
    assert all(row["choice_solved"] == "false" for row in predictions
               if row["instance"].endswith("neither"))
    report = (tmp_path / "report.md").read_text()
    assert "| 0 | 5 | 4 | 2 | 3 | rf-ugf | 2 | 4 | 0 |" in report
    assert "| 1 | 5 | 4 | 2 | 3 | rf-ugf | 2 | 4 | 0 |" in report
    assert "| Total | 10 | 8 | 4 | 6 | per fold | 4 | 8 | 0 |" in report


@pytest.mark.parametrize("repetition", [1, 2])
def test_unsolved_summary_retained_with_explicit_repetition_filter(tmp_path, repetition):
    argv = summary_fixture(tmp_path, [("one", 0, (0,), "UNREALIZABLE", "ERROR")])

    def add_second_repetition(rows):
        rows.append({**rows[0], "repetition_id": "2"})

    rewrite_tsv(tmp_path / "labels.tsv", add_second_repetition)
    output, scores, _, _ = assess([*argv, "--repetition", str(repetition)])
    assert scores[-1]["labelled"] == 1
    assert scores[-1]["missing"] == 0
    assert output[0]["rf-ugf_solved"] == "true"
    assert output[0]["rf-ufa_solved"] == "false"


def test_duplicate_unsolved_summary_row_rejected(tmp_path, capsys):
    argv = summary_fixture(tmp_path, [("one", 0, (0,), "TIMEOUT", "ERROR")])
    rewrite_tsv(tmp_path / "labels.tsv", lambda rows: rows.append(rows[0].copy()))
    with pytest.raises(SystemExit) as error:
        stump.main(argv)
    assert error.value.code == 2
    assert "duplicate label row for ('rf-ugf', 'one', 1)" in capsys.readouterr().err


def threshold_cases():
    return [(f"f{fold}-x{x}", fold, (x,),
             "UNREALIZABLE" if x < 2.5 else "TIMEOUT",
             "TIMEOUT" if x < 2.5 else "UNREALIZABLE")
            for fold in range(3) for x in (0, 1, 4, 5)]


def test_planted_threshold_recovered_exactly(tmp_path):
    argv = selector_fixture(tmp_path, threshold_cases())
    output, scores, training, models = assess(argv)
    assert all(default == 0 and model.feature == 0 and model.threshold == 2.5
               and model.left.pair == 0 and model.right.pair == 1
               for default, model in models.values())
    assert all(row["choice_solved"] == "true" for row in output)
    assert scores[-1] == {"fold": "Total", "default_pair": "per fold", "labelled": 12,
                          "selector": 12, "pair_a": 6, "pair_b": 6,
                          "default": 6, "oracle": 12, "missing": 0}
    assert [row["eligible"] for row in training] == [8, 8, 8, 12]
    assert stump.main(argv) == 0
    report = (tmp_path / "report.md").read_text()
    assert report.count("if inputs <= 2.5:") == 3
    assert "feature column order, then the smaller midpoint" in report


def test_realizable_only_excluded_from_training_but_evaluated(tmp_path):
    cases = [(f"f{fold}-{tag}", fold, (x,), a, b) for fold in range(2) for tag, x, a, b in (
        ("lo", 0, "UNREALIZABLE", "TIMEOUT"),
        ("hi", 4, "TIMEOUT", "UNREALIZABLE"),
        ("real", 0, "TIMEOUT", "REALIZABLE"),
    )]
    output, scores, training, models = assess(selector_fixture(tmp_path, cases))
    assert all(default == 0 and model.threshold == 2 for default, model in models.values())
    assert scores[-1]["labelled"] == 6
    assert scores[-1]["selector"] == 4
    assert scores[-1]["oracle"] == 6
    assert scores[-1]["pair_b"] == 4
    assert [row["realizable_only"] for row in training] == [1, 1, 2]
    assert [row["unique_excluded_instances"] for row in training] == [1, 1, 2]
    real = [row for row in output if row["instance"].endswith("real")]
    assert len(real) == 2
    assert all(row["B_solved"] == "true" and row["chosen_pair"] == "A"
               and row["choice_solved"] == "false" for row in real)


def test_training_exclusion_appearances_are_additive_and_unique_count_is_separate(tmp_path):
    cases = [
        ("real", 0, (0,), "TIMEOUT", "REALIZABLE"),
        ("overlapping-exclusions", 0, None, "TIMEOUT", "REALIZABLE"),
        ("missing-label", 0, (1,), "UNREALIZABLE", None),
        ("missing-features", 1, None, "UNREALIZABLE", "TIMEOUT"),
        ("eligible-a", 1, (10,), "UNREALIZABLE", "TIMEOUT"),
        ("eligible-b", 2, (20,), "TIMEOUT", "UNREALIZABLE"),
    ]
    argv = selector_fixture(tmp_path, cases)
    _, _, training, _ = assess(argv)
    assert [row["training_exclusion_appearances"] for row in training] == [1, 2, 3, 6]
    assert training[-1]["training_exclusion_appearances"] == sum(
        row["training_exclusion_appearances"] for row in training[:-1])
    assert [row["unique_excluded_instances"] for row in training] == [1, 2, 3, 3]
    # Keep the other total training counts as unique input-instance counts.
    assert training[-1] == {
        "fold": "Total (unique instances)", "labelled": 5, "eligible": 2,
        "realizable_only": 2, "features_unavailable": 2,
        "unique_excluded_instances": 3, "training_exclusion_appearances": 6,
    }
    assert stump.main(argv) == 0
    report = (tmp_path / "report.md").read_text()
    assert "| Features unavailable | unique_excluded_instances |" in report
    assert "| Total (unique instances) | 5 | 2 | 2 | 2 | 3 |" in report
    assert (
        "| Held-out fold | training_exclusion_appearances |\n"
        "| --- | --- |\n| 0 | 1 |\n| 1 | 2 |\n| 2 | 3 |\n| Total | 6 |"
    ) in report
    assert "Its total is the sum of the per-fold counts" in report
    assert "counts each excluded input instance once" in report


def test_missing_labels_are_missing_not_unsolved(tmp_path):
    cases = threshold_cases() + [("missing-b", 0, (0,), "UNREALIZABLE", None),
                                 ("missing-both", 1, (5,), None, None)]
    argv = selector_fixture(tmp_path, cases)
    output, scores, training, _ = assess(argv)
    assert scores[-1]["labelled"] == 12
    assert scores[-1]["missing"] == 2
    assert scores[-1]["pair_a"] == 6
    assert scores[-1]["selector"] == scores[-1]["oracle"] == 12
    assert training[-1]["labelled"] == training[-1]["eligible"] == 12
    by_name = {row["instance"]: row for row in output}
    assert by_name["missing-b"]["A_solved"] == "true"
    assert by_name["missing-b"]["B_solved"] == by_name["missing-b"]["choice_solved"] == ""
    assert by_name["missing-both"]["A_solved"] == by_name["missing-both"]["B_solved"] == ""
    assert stump.main(argv) == 0
    assert "| Total | 12 | 12 | 6 | 6 | per fold | 6 | 12 | 2 |" in (
        tmp_path / "report.md").read_text()


def test_duplicate_label_rejected(tmp_path, capsys):
    argv = selector_fixture(tmp_path, threshold_cases())
    rewrite_tsv(tmp_path / "labels.tsv", lambda rows: rows.append(rows[0].copy()))
    with pytest.raises(SystemExit) as error:
        stump.main(argv)
    assert error.value.code == 2
    assert "duplicate label row for ('A', 'f0-x0', 1)" in capsys.readouterr().err
    assert not (tmp_path / "predictions.tsv").exists()


@pytest.mark.parametrize("unavailable", ["absent", "parse_error", "blank"])
def test_unavailable_features_train_exclusion_and_default_fallback(tmp_path, unavailable):
    cases = [(f"f{fold}-{tag}", fold, (x,), a, b) for fold in range(2) for tag, x, a, b in (
        ("lo", 0, "UNREALIZABLE", "TIMEOUT"),
        ("hi", 4, "TIMEOUT", "UNREALIZABLE"),
        ("higher", 5, "TIMEOUT", "UNREALIZABLE"),
        ("unavailable", 0, "UNREALIZABLE", "TIMEOUT"),
    )]
    argv = selector_fixture(tmp_path, cases)

    def invalidate(rows):
        if unavailable == "absent":
            rows[:] = [row for row in rows if "unavailable" not in row["file"]]
        else:
            for row in rows:
                if "unavailable" in row["file"]:
                    row["inputs"] = "not numeric" if unavailable == "parse_error" else ""
                    if unavailable == "parse_error":
                        row["parse_status"] = "error"

    rewrite_tsv(tmp_path / "features.tsv", invalidate)
    output, scores, training, models = assess(argv)
    assert all(default == 1 and model.threshold == 2 for default, model in models.values())
    assert [row["features_unavailable"] for row in training] == [1, 1, 2]
    assert training[-1]["eligible"] == 6
    fallback = [row for row in output if "unavailable" in row["instance"]]
    assert all(row["chosen_pair"] == "B" and row["choice_solved"] == "false" for row in fallback)
    assert scores[-1]["labelled"] == 8
    assert scores[-1]["selector"] == 6


def test_split_ties_use_feature_order_then_smaller_threshold(tmp_path):
    cases = [(f"f{fold}-x{x}", fold, (x, x),
              "UNREALIZABLE" if x in (0, 4) else "TIMEOUT",
              "TIMEOUT" if x in (0, 4) else "UNREALIZABLE")
             for fold in range(2) for x in (0, 2, 4, 6)]
    argv = selector_fixture(tmp_path, cases, names=("guard_x", "inputs"))
    _, scores, _, models = assess(argv)
    assert all(model.feature == 0 and model.threshold == 1 for _, model in models.values())
    assert scores[-1]["selector"] == 6


def test_depth_two_recursively_improves_leaves(tmp_path):
    cases = [(f"f{fold}-x{x}", fold, (x,),
              "UNREALIZABLE" if x in (0, 1, 4) else "TIMEOUT",
              "TIMEOUT" if x in (0, 1, 4) else "UNREALIZABLE")
             for fold in range(2) for x in range(5)]
    argv = selector_fixture(tmp_path, cases)
    assert assess(argv)[1][-1]["selector"] == 8
    _, scores, _, models = assess([*argv, "--depth", "2"])
    assert scores[-1]["selector"] == 10
    assert all(model.threshold == 1.5 and model.right.threshold == 3.5
               for _, model in models.values())


def test_constant_when_no_split_strictly_improves_even_at_depth_two(tmp_path):
    # XOR offers no immediate gain, so the greedy root must remain constant.
    cases = [(f"f{fold}-{x}-{y}", fold, (x, y),
              "UNREALIZABLE" if x == y else "TIMEOUT",
              "TIMEOUT" if x == y else "UNREALIZABLE")
             for fold in range(2) for x in (0, 1) for y in (0, 1)]
    argv = selector_fixture(tmp_path, cases, names=("inputs", "outputs"))
    for depth in (1, 2):
        _, scores, _, models = assess([*argv, "--depth", str(depth), "--pairs", "B,A"])
        assert all(default == model.pair == 0 and model.feature is None
                   for default, model in models.values())
        assert scores[-1]["selector"] == scores[-1]["default"] == 4


def asymmetric_leakage_cases():
    # Training on folds 1 and 2 gives A <= 7, B > 7, with default A.
    # Fold 0 reverses the winners inside the training gap. Pooling its labels
    # moves the best split to 4.5. Even pooling only its feature values makes
    # 4.5 the smallest optimal midpoint, instead of the training-only 7.
    return [
        ("train-1-a0", 1, (0,), "UNREALIZABLE", "TIMEOUT"),
        ("train-1-a2", 1, (2,), "UNREALIZABLE", "TIMEOUT"),
        ("train-1-b10", 1, (10,), "TIMEOUT", "UNREALIZABLE"),
        ("train-2-a4", 2, (4,), "UNREALIZABLE", "TIMEOUT"),
        ("train-2-b12", 2, (12,), "TIMEOUT", "UNREALIZABLE"),
        ("train-2-b14", 2, (14,), "TIMEOUT", "UNREALIZABLE"),
        ("held-low-5", 0, (5,), "TIMEOUT", "UNREALIZABLE"),
        ("held-low-6", 0, (6,), "TIMEOUT", "UNREALIZABLE"),
        ("held-high-8", 0, (8,), "UNREALIZABLE", "TIMEOUT"),
        ("held-high-9", 0, (9,), "UNREALIZABLE", "TIMEOUT"),
    ]


@pytest.mark.parametrize("depth", [1, 2])
def test_held_out_labels_do_not_affect_their_model(tmp_path, depth):
    argv = selector_fixture(tmp_path, asymmetric_leakage_cases()) + ["--depth", str(depth)]
    before_output, before_scores, _, before_models = assess(argv)
    expected = (0, stump.Node(0, 0, 7, stump.Node(0), stump.Node(1)))
    assert before_models[0] == expected
    assert before_scores[0]["selector"] == 0

    def change_held_out(rows):
        for row in rows:
            if row["instance"].startswith("held-"):
                winner = "A" if row["instance"].startswith("held-low-") else "B"
                solves = row["solver_label"] == winner
                row["decisive_result"] = "UNREALIZABLE" if solves else ""
                row["still_unsolved_at_max_cap"] = "false" if solves else "true"

    rewrite_tsv(tmp_path / "labels.tsv", change_held_out)
    after_output, after_scores, _, after_models = assess(argv)
    assert after_models[0] == expected
    assert after_scores[0]["selector"] == 4
    for output in (before_output, after_output):
        assert {row["instance"]: row["chosen_pair"] for row in output if row["fold"] == 0} == {
            "held-low-5": "A", "held-low-6": "A", "held-high-8": "B", "held-high-9": "B",
        }


def test_held_out_solves_do_not_affect_their_default(tmp_path):
    # Training is unanimously B. Four eligible held-out A wins would flip a
    # pooled default to A; the missing-feature row exposes the fallback choice.
    cases = [
        ("train-1-b0", 1, (0,), "TIMEOUT", "UNREALIZABLE"),
        ("train-1-b2", 1, (2,), "TIMEOUT", "UNREALIZABLE"),
        ("train-2-b4", 2, (4,), "TIMEOUT", "UNREALIZABLE"),
        *[(f"held-a{x}", 0, (x,), "UNREALIZABLE", "TIMEOUT") for x in (5, 6, 8, 9)],
        ("held-fallback", 0, None, "UNREALIZABLE", "TIMEOUT"),
    ]
    argv = selector_fixture(tmp_path, cases)
    before_output, before_scores, _, before_models = assess(argv)
    assert before_models[0] == (1, stump.Node(1))

    def change_held_out(rows):
        for row in rows:
            if row["instance"].startswith("held-"):
                solves = row["solver_label"] == "B"
                row["decisive_result"] = "UNREALIZABLE" if solves else ""
                row["still_unsolved_at_max_cap"] = "false" if solves else "true"

    rewrite_tsv(tmp_path / "labels.tsv", change_held_out)
    after_output, after_scores, _, after_models = assess(argv)
    assert after_models[0] == (1, stump.Node(1))
    assert before_scores[0]["default"] == 0
    assert after_scores[0]["default"] == 5
    for output, scores in ((before_output, before_scores), (after_output, after_scores)):
        assert scores[0]["default_pair"] == "B"
        assert all(row["chosen_pair"] == "B" for row in output if row["fold"] == 0)


@pytest.mark.parametrize("depth", [1, 2])
def test_held_out_feature_values_do_not_affect_thresholds(tmp_path, depth):
    argv = selector_fixture(tmp_path, asymmetric_leakage_cases()) + ["--depth", str(depth)]
    _, before_scores, _, before_models = assess(argv)
    expected = (0, stump.Node(0, 0, 7, stump.Node(0), stump.Node(1)))
    assert before_models[0] == expected
    assert before_scores[0]["selector"] == 0

    def change_held_out(rows):
        values = {"held-low-5": 20, "held-low-6": 21, "held-high-8": 4.5, "held-high-9": 4.75}
        for row in rows:
            name = row["file"].removeprefix("source-").removesuffix(".tlsf")
            if name in values:
                row["inputs"] = values[name]

    rewrite_tsv(tmp_path / "features.tsv", change_held_out)
    output, scores, _, models = assess(argv)
    # These new held-out values also never occur in training. Leaking them into
    # midpoint selection would move the threshold again, from 4.5 to 4.25.
    assert models[0] == expected
    assert scores[0]["selector"] == 4
    assert {row["instance"]: row["chosen_pair"] for row in output if row["fold"] == 0} == {
        "held-low-5": "B", "held-low-6": "B", "held-high-8": "A", "held-high-9": "A",
    }


def test_repetition_and_pair_filtering(tmp_path):
    argv = selector_fixture(tmp_path, threshold_cases())

    def add_rows(rows):
        second = [{**row, "repetition_id": "2", "decisive_result": "UNREALIZABLE",
                   "still_unsolved_at_max_cap": "true" if row["solver_label"] == "A" else "false"}
                  for row in rows]
        rows += second
        rows += [{**rows[0], "solver_label": "unselected"}] * 2

    rewrite_tsv(tmp_path / "labels.tsv", add_rows)
    assert assess(argv)[1][-1]["selector"] == 12
    output, scores, _, models = assess([*argv, "--repetition", "2"])
    assert all(default == model.pair == 1 and model.feature is None for default, model in models.values())
    assert all(row["A_solved"] == "false" and row["B_solved"] == "true" for row in output)
    assert scores[-1]["pair_a"] == 0


def test_solve_requires_false_and_a_decisive_result(tmp_path):
    argv = selector_fixture(tmp_path, [("one", 0, (0,), "UNREALIZABLE", "UNREALIZABLE")])

    def invalidate(rows):
        rows[0]["still_unsolved_at_max_cap"] = "true"
        rows[1]["decisive_result"] = "UNKNOWN"

    rewrite_tsv(tmp_path / "labels.tsv", invalidate)
    output, scores, _, models = assess(argv)
    assert scores[-1]["labelled"] == 1
    assert scores[-1]["missing"] == scores[-1]["oracle"] == 0
    assert output[0]["A_solved"] == output[0]["B_solved"] == "false"
    # No training folds: ties, including empty training, fall back to the first pair.
    assert models[0] == (0, stump.Node(0))


def test_empty_label_panel_reports_missing_without_training(tmp_path):
    argv = selector_fixture(tmp_path, [("one", 0, (0,), None, None), ("two", 1, None, None, None)])
    output, scores, training, models = assess(argv)
    assert scores[-1]["labelled"] == scores[-1]["selector"] == 0
    assert scores[-1]["missing"] == 2
    assert training[-1]["eligible"] == 0
    assert all(model == (0, stump.Node(0)) for model in models.values())
    assert all(row["choice_solved"] == "" for row in output)
    assert stump.main(argv) == 0


@pytest.mark.parametrize("fold_column", ["fold_family", "fold_directory"])
def test_cli_output_byte_identical_across_runs_and_shuffled_rows(tmp_path, fold_column):
    argv = selector_fixture(tmp_path, threshold_cases())
    argv += ["--fold-column", fold_column, "--depth", "2"]
    outputs = [tmp_path / "predictions.tsv", tmp_path / "report.md"]
    snapshots = []
    for seed in (1, 27):
        result = subprocess.run(
            [sys.executable, str(BENCHMARKING / "selector-stump.py"), *argv],
            env={**os.environ, "PYTHONHASHSEED": str(seed)}, capture_output=True, text=True,
        )
        assert result.returncode == 0, result.stderr
        snapshots.append([path.read_bytes() for path in outputs])
        for filename in ("folds.tsv", "families.tsv", "features.tsv", "labels.tsv"):
            rewrite_tsv(tmp_path / filename, random.Random(seed).shuffle)
    assert snapshots[0] == snapshots[1]
