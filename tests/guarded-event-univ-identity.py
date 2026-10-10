"""Compare unmodified master translation with the guarded path in fresh processes."""

import json
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile


def main():
    executable = sys.argv[1]
    rng = random.Random(7241)
    formulas = [
        "F a", "G F a", "F G a", "a U b", "a M b", "a W b", "a R b",
        "(F a) <-> (F b)", "!(a W (b U c))", "G (a -> F b)",
        "(G F a) -> ((G !b) & (G F (a & b)))",
    ]
    atoms = ["a", "b", "c", "!a", "!b", "!c"]
    for _ in range(25):
        left, right = rng.sample(atoms, 2)
        formulas.append(f"({rng.choice(['F', 'G', 'X'])} ({left})) "
                        f"{rng.choice(['&', '|', '->', '<->', 'U', 'W', 'R', 'M'])} "
                        f"({rng.choice(['F', 'G', 'X'])} ({right}))")
    width = int(subprocess.check_output([executable, "--width"], text=True))
    terms = ["F (" + "X " * index + "request)" for index in range(width - 1)]
    formulas.append("(G F response) & (" + " | ".join(terms) + ")")
    comparisons = 0
    # Spot's public Small, Any and Deterministic preference bits.
    for formula in formulas:
        for preference in (1, 0, 2):
            common = [executable, "", formula, str(preference)]
            common[1] = "--master"
            master = subprocess.run(common, check=True, capture_output=True, timeout=15)
            common[1] = "--guarded"
            guarded = subprocess.run(common, check=True, capture_output=True, timeout=15)
            assert master.stdout == guarded.stdout, (formula, preference)
            assert master.stderr == guarded.stderr, (formula, preference)
            comparisons += 1
    print(f"{comparisons} fresh-process automata byte-identical to master")
    with tempfile.TemporaryDirectory(prefix="event-univ-", dir=".") as directory:
        env = dict(os.environ, ACACIA_PHASE_RECORDS=str(Path(directory).resolve()))
        # Exercise a high-promise formula with many shared DAG nodes.
        terms = ["F (" + "X " * index + "request)" for index in range(width + 1)]
        formula = "(G F response) & (" + " | ".join(terms) + ")"
        subprocess.run([executable, "--guarded", formula, "1"], env=env,
                       check=True, capture_output=True, timeout=15)
        records = [json.loads(line) for path in Path(directory).rglob("*.jsonl")
                   for line in path.read_text().splitlines()]
        entries = [row for row in records if row.get("stage") == "translation-event-univ"]
        assert any(row.get("event") == "stage_entry" for row in entries)
        assert any(row.get("event") == "stage_completion" for row in entries)
        metrics = {row.get("key"): row.get("value") for row in records
                   if row.get("event") == "stage_metric"}
        assert int(metrics["distinct_promises"]) > int(metrics["acceptance_set_width"])
    print("guard firing has phase entry/completion and promise/width metrics")


if __name__ == "__main__":
    main()
