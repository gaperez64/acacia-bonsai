#!/usr/bin/env python3
"""Report held-out solve counts for a greedy depth-1 or depth-2 pair selector.

Only eligible instances outside the held-out fold determine its default and
tree. A node splits only if its two constant leaves strictly improve on its
constant majority pair; depth 2 repeats that rule within the resulting leaves.
Leaf solve-count ties use the fold default. Split-score ties use feature header
order, then the smaller midpoint. No success threshold or verdict is applied.
"""

from __future__ import annotations

import argparse
import csv
import math
import pathlib
from dataclasses import dataclass


METADATA_COLUMNS = {"file", "schema_version", "parse_status"}
DECISIVE_RESULTS = {"REALIZABLE", "UNREALIZABLE"}


@dataclass(frozen=True)
class Instance:
    name: str
    fold: int
    values: tuple[float, ...] | None
    solved: tuple[bool | None, bool | None]
    realizable_only: bool

    @property
    def labelled(self) -> bool:
        return all(value is not None for value in self.solved)

    @property
    def eligible(self) -> bool:
        return self.labelled and self.values is not None and not self.realizable_only


@dataclass(frozen=True)
class Node:
    pair: int
    feature: int | None = None
    threshold: float | None = None
    left: Node | None = None
    right: Node | None = None


def positive_int(value: str) -> int:
    number = int(value)
    if number < 1:
        raise argparse.ArgumentTypeError("must be positive")
    return number


def pair_ids(value: str) -> tuple[str, str]:
    pairs = tuple(part.strip() for part in value.split(","))
    if len(pairs) != 2 or not all(pairs) or pairs[0] == pairs[1]:
        raise argparse.ArgumentTypeError("expected two distinct pair ids, A,B")
    return pairs


def argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--folds", required=True, type=pathlib.Path)
    parser.add_argument("--fold-column", required=True, choices=("fold_family", "fold_directory"))
    parser.add_argument("--features", required=True, type=pathlib.Path)
    parser.add_argument("--family-table", required=True, type=pathlib.Path)
    parser.add_argument("--labels", required=True, type=pathlib.Path)
    parser.add_argument("--pairs", required=True, type=pair_ids)
    parser.add_argument("--repetition", default=1, type=positive_int)
    parser.add_argument("--depth", required=True, type=int, choices=(1, 2))
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--report", required=True, type=pathlib.Path)
    return parser


def read_tsv(path: pathlib.Path, required: set[str]) -> tuple[list[str], list[dict[str, str]]]:
    with path.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        columns = reader.fieldnames or []
        if not required <= set(columns) or len(columns) != len(set(columns)):
            raise ValueError(f"{path}: requires unique columns including {', '.join(sorted(required))}")
        rows = []
        for line, row in enumerate(reader, 2):
            if None in row or any(value is None for value in row.values()):
                raise ValueError(f"{path}:{line}: malformed TSV row")
            rows.append(row)
    return columns, rows


def index_rows(rows: list[dict[str, str]], key: str, path: pathlib.Path) -> dict[str, dict[str, str]]:
    indexed = {}
    for row in rows:
        name = row[key]
        if not name or name in indexed:
            raise ValueError(f"{path}: empty or duplicate {key}: {name!r}")
        indexed[name] = row
    return indexed


def read_features(path: pathlib.Path) -> tuple[list[str], dict[str, tuple[float, ...] | None]]:
    columns, rows = read_tsv(path, METADATA_COLUMNS)
    names = [name for name in columns if name not in METADATA_COLUMNS]
    if not names:
        raise ValueError(f"{path}: no feature columns")
    features = {}
    for filename, row in index_rows(rows, "file", path).items():
        if row["parse_status"] != "ok" or any(not row[name].strip() for name in names):
            features[filename] = None
            continue
        try:
            values = tuple(float(row[name]) for name in names)
        except ValueError as error:
            raise ValueError(f"{path}: nonnumeric features for {filename}") from error
        if not all(math.isfinite(value) for value in values):
            raise ValueError(f"{path}: nonfinite features for {filename}")
        features[filename] = values
    return names, features


def load_instances(args: argparse.Namespace) -> tuple[list[str], list[Instance]]:
    _, fold_rows = read_tsv(args.folds, {"instance", args.fold_column})
    folds = index_rows(fold_rows, "instance", args.folds)
    if not folds:
        raise ValueError(f"{args.folds}: no instances")
    _, family_rows = read_tsv(args.family_table, {"logical_instance", "tlsf_file"})
    families = index_rows(family_rows, "logical_instance", args.family_table)
    names, features = read_features(args.features)
    _, label_rows = read_tsv(args.labels, {
        "solver_label", "instance", "repetition_id", "decisive_result", "still_unsolved_at_max_cap",
    })
    labels = {}
    for row in label_rows:
        if row["solver_label"] not in args.pairs or row["repetition_id"] != str(args.repetition):
            continue
        key = (row["solver_label"], row["instance"], args.repetition)
        if key in labels:
            raise ValueError(f"{args.labels}: duplicate label row for {key}")
        labels[key] = row

    instances = []
    for name, row in sorted(folds.items()):
        try:
            fold = int(row[args.fold_column])
        except ValueError as error:
            raise ValueError(f"{args.folds}: invalid {args.fold_column} for {name}") from error
        if fold < 0:
            raise ValueError(f"{args.folds}: negative fold for {name}")
        if name not in families:
            raise ValueError(f"instance {name!r} missing from family table {args.family_table}")
        pair_rows = [labels.get((pair, name, args.repetition)) for pair in args.pairs]
        solved = tuple(
            None if label is None else (
                label["still_unsolved_at_max_cap"] == "false"
                and label["decisive_result"] in DECISIVE_RESULTS
            )
            for label in pair_rows
        )
        realizable_only = None not in solved and sum(solved) == 1 and any(
            did_solve and label["decisive_result"] == "REALIZABLE"
            for did_solve, label in zip(solved, pair_rows)
        )
        instances.append(Instance(
            name, fold, features.get(families[name]["tlsf_file"]), solved, realizable_only,
        ))
    return names, instances


def solve_counts(instances: list[Instance]) -> tuple[int, int]:
    return tuple(sum(row.solved[pair] is True for row in instances) for pair in range(2))


def majority_pair(counts: tuple[int, int], default: int) -> int:
    if counts[0] == counts[1]:
        return default
    return 0 if counts[0] > counts[1] else 1


def learn(instances: list[Instance], feature_count: int, depth: int, default: int) -> Node:
    """Fit eligible training rows using a sorted sweep over each feature's values."""
    counts = solve_counts(instances)
    pair = majority_pair(counts, default)
    best_score = counts[pair]
    best = Node(pair)
    if depth == 0:
        return best
    for feature in range(feature_count):
        ordered = sorted(instances, key=lambda row: row.values[feature])
        left_counts = [0, 0]
        for index in range(len(ordered) - 1):
            row = ordered[index]
            for candidate in range(2):
                left_counts[candidate] += row.solved[candidate]
            lower, upper = row.values[feature], ordered[index + 1].values[feature]
            if lower == upper:
                continue
            right_counts = tuple(counts[candidate] - left_counts[candidate] for candidate in range(2))
            score = max(left_counts) + max(right_counts)
            # Iteration order implements feature-order, then threshold-order ties.
            if score > best_score:
                best_score = score
                best = Node(pair, feature, lower / 2 + upper / 2)
    if best.feature is None:
        return best
    left = [row for row in instances if row.values[best.feature] <= best.threshold]
    right = [row for row in instances if row.values[best.feature] > best.threshold]
    return Node(pair, best.feature, best.threshold,
                learn(left, feature_count, depth - 1, default),
                learn(right, feature_count, depth - 1, default))


def predict(model: Node, values: tuple[float, ...] | None, default: int) -> int:
    if values is None:
        return default
    while model.feature is not None:
        model = model.left if values[model.feature] <= model.threshold else model.right
    return model.pair


def training_counts(instances: list[Instance]) -> dict[str, int]:
    labelled = [row for row in instances if row.labelled]
    eligible = sum(row.eligible for row in labelled)
    return {
        "labelled": len(labelled),
        "eligible": eligible,
        "realizable_only": sum(row.realizable_only for row in labelled),
        "features_unavailable": sum(row.values is None for row in labelled),
        "unique_excluded_instances": len(labelled) - eligible,
    }


def evaluate(instances: list[Instance], names: list[str], pairs: tuple[str, str], depth: int):
    output, scores, training, models = [], [], [], {}
    for fold in sorted({row.fold for row in instances}):
        candidates = [row for row in instances if row.fold != fold]
        eligible = [row for row in candidates if row.eligible]
        default = majority_pair(solve_counts(eligible), 0)
        model = learn(eligible, len(names), depth, default)
        models[fold] = (default, model)
        counts = training_counts(candidates)
        training.append({"fold": fold, **counts,
                         "training_exclusion_appearances": counts["unique_excluded_instances"]})
        held_out = [row for row in instances if row.fold == fold]
        labelled = [row for row in held_out if row.labelled]
        fixed = solve_counts(labelled)
        score = {
            "fold": fold, "labelled": len(labelled), "selector": 0,
            "pair_a": fixed[0], "pair_b": fixed[1], "default_pair": pairs[default],
            "default": fixed[default], "oracle": sum(any(row.solved) for row in labelled),
            "missing": len(held_out) - len(labelled),
        }
        for row in held_out:
            choice = predict(model, row.values, default)
            choice_solved = row.solved[choice] if row.labelled else None
            score["selector"] += choice_solved is True
            output.append({
                "instance": row.name, "fold": fold, "chosen_pair": pairs[choice],
                f"{pairs[0]}_solved": boolean_cell(row.solved[0]),
                f"{pairs[1]}_solved": boolean_cell(row.solved[1]),
                "choice_solved": boolean_cell(choice_solved),
            })
        scores.append(score)
    totals = {key: sum(score[key] for score in scores) for key in (
        "labelled", "selector", "pair_a", "pair_b", "default", "oracle", "missing",
    )}
    scores.append({"fold": "Total", "default_pair": "per fold", **totals})
    exclusion_appearances = sum(row["training_exclusion_appearances"] for row in training)
    training.append({"fold": "Total (unique instances)", **training_counts(instances),
                     "training_exclusion_appearances": exclusion_appearances})
    return sorted(output, key=lambda row: row["instance"]), scores, training, models


def boolean_cell(value: bool | None) -> str:
    return "" if value is None else str(value).lower()


def tree_lines(node: Node, names: list[str], pairs: tuple[str, str], indent: str = "") -> list[str]:
    if node.feature is None:
        return [f"{indent}predict {pairs[node.pair]}"]
    return (
        [f"{indent}if {names[node.feature]} <= {node.threshold:.17g}:"]
        + tree_lines(node.left, names, pairs, indent + "  ")
        + [f"{indent}else:"]
        + tree_lines(node.right, names, pairs, indent + "  ")
    )


def markdown_table(headers: list[str], rows: list[list]) -> list[str]:
    def line(values):
        return "| " + " | ".join(str(value).replace("|", "\\|") for value in values) + " |"
    return [line(headers), line(["---"] * len(headers))] + [line(row) for row in rows]


def render_report(args: argparse.Namespace, names: list[str], scores, training, models) -> str:
    pairs = args.pairs
    lines = [
        "# Pair selector", "",
        f"Fold column: `{args.fold_column}`; pairs: `{pairs[0]}`, `{pairs[1]}`; "
        f"repetition: {args.repetition}; depth: {args.depth}.", "",
        "Defaults use eligible training solves, with ties to the first pair. Leaf ties use "
        "the fold default. Split-score ties use feature column order, then the smaller midpoint. "
        "Each node splits only for a strict training-solve increase; depth 2 repeats this "
        "greedily within each leaf.", "",
        "Feature columns: " + ", ".join(f"`{name}`" for name in names) + ".", "",
        "Missing or non-ok features are excluded from training and use the fold default "
        "at evaluation. Missing feature cells also count as unavailable.", "",
        "## Held-out evaluation", "",
        "Only instances with both label rows contribute to solve counts. The per-instance "
        "TSV includes all instances; missing pair labels and unlabelled choice_solved cells "
        "are empty. Total default solves sum the defaults used by the individual folds.", "",
    ]
    lines += markdown_table(
        ["Fold", "Labelled instances", "Selector solves", f"{pairs[0]} solves", f"{pairs[1]} solves",
         "Default pair", "Default solves", "Oracle solves", "Missing labels"],
        [[row[key] for key in ("fold", "labelled", "selector", "pair_a", "pair_b",
                               "default_pair", "default", "oracle", "missing")] for row in scores],
    )
    lines += [
        "", "## Training exclusions", "",
        "Each fold row counts unique training candidates from every other fold. The "
        "Total (unique instances) row counts each instance in the full input once. "
        "REALIZABLE-only means exactly one pair solved, with result REALIZABLE; these "
        "instances remain in evaluation. Exclusion reasons can overlap; "
        "`unique_excluded_instances` counts their union, counting each excluded instance once. "
        "Unlabelled instances are outside these training counts.", "",
    ]
    lines += markdown_table(
        ["Held-out fold", "Labelled candidates", "Eligible training", "REALIZABLE-only",
         "Features unavailable", "unique_excluded_instances"],
        [[row[key] for key in ("fold", "labelled", "eligible", "realizable_only",
                               "features_unavailable", "unique_excluded_instances")]
         for row in training],
    )
    lines += [
        "", "`training_exclusion_appearances` counts an excluded labelled instance once for "
        "each fold where it is a training candidate. Its total is the sum of the per-fold "
        "counts, so an instance can contribute more than once. The separate "
        "`unique_excluded_instances` total above counts each excluded input instance once.", "",
    ]
    lines += markdown_table(
        ["Held-out fold", "training_exclusion_appearances"],
        [[row["fold"], row["training_exclusion_appearances"]] for row in training[:-1]]
        + [["Total", training[-1]["training_exclusion_appearances"]]],
    )
    lines += ["", "## Learned splits", ""]
    for fold, (default, model) in models.items():
        lines += [f"### Fold {fold}", "", f"Default pair: `{pairs[default]}`.", "", "```text"]
        lines += tree_lines(model, names, pairs)
        lines += ["```", ""]
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argument_parser()
    args = parser.parse_args(argv)
    try:
        names, instances = load_instances(args)
        output, scores, training, models = evaluate(instances, names, args.pairs, args.depth)
        report = render_report(args, names, scores, training, models)
        columns = ["instance", "fold", "chosen_pair", f"{args.pairs[0]}_solved",
                   f"{args.pairs[1]}_solved", "choice_solved"]
        with args.output.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=columns, delimiter="\t", lineterminator="\n")
            writer.writeheader()
            writer.writerows(output)
        args.report.write_text(report, encoding="utf-8")
    except (OSError, ValueError) as error:
        parser.exit(2, f"error: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
