#!/usr/bin/env python3
"""Check production CPre snapshots against independent integer/downset arithmetic."""
import csv
from pathlib import Path
import subprocess
import sys
import tempfile


def maximal(vectors):
    values = set(vectors)
    return {v for v in values if not any(v != w and all(a <= b for a, b in zip(v, w))
                                        for w in values)}


def check_event(path, states, threshold):
    lines = path.read_text().splitlines()
    header = dict(item.split("=", 1) for item in lines[0].removeprefix("# ").split()
                  if "=" in item)
    assert header["schema_version"] == "2", path
    before, after, actions = [], [], []
    section = None
    after_count = None
    for line in lines[1:]:
        if line == "[before]":
            section = before
        elif line == "[actions]":
            section = actions
        elif line.startswith("[after]\t"):
            after_count = int(line.split("\t")[1])
            section = after
        elif line.startswith("action\t"):
            assert section is actions and int(line.split("\t")[1]) == len(actions)
            actions.append([])
        elif section is actions:
            destination, source, increment = map(int, line.split("\t"))
            assert 0 <= destination < states and 0 <= source < states and increment in (0, 1)
            actions[-1].append((destination, source, increment))
        else:
            vector = tuple(map(int, line.split("\t")))
            assert len(vector) == states
            assert all(-1 <= x <= (int(header["k"]) - 1 if i < threshold else 0)
                       for i, x in enumerate(vector))
            section.append(vector)
    assert len(actions) == int(header["actions"]) > 0
    assert len(before) == int(header["before"]) > 0
    assert after_count is not None and len(after) == after_count
    # Preimages reset counting coordinates to K-1 and Boolean ones to zero.
    # An absent destination (-1) constrains its predecessors to absence too.
    preimages = []
    for action in actions:
        for bound in before:
            image = [int(header["k"]) - 1 if i < threshold else 0 for i in range(states)]
            for destination, source, increment in action:
                image[source] = min(image[source], max(-1, bound[destination] - increment))
            preimages.append(tuple(image))
    expected = maximal(tuple(min(a, b) for a, b in zip(v, w))
                       for v in before for w in preimages)
    assert expected == set(after) and len(expected) == len(after), (
        f"{path}: {expected} != {after}")
    return before != after, len(actions) > 1


def main():
    with tempfile.TemporaryDirectory(prefix="cpre-snapshot-") as temporary:
        subprocess.run([sys.argv[1], temporary], check=True, timeout=30)
        checked = changed = multiple = 0
        thresholds = set()
        for directory in sorted(Path(temporary).glob("aut-*")):
            with (directory / "meta.tsv").open() as stream:
                meta, = csv.DictReader(stream, delimiter="\t")
            assert meta["schema_version"] == "3"
            assert (directory / "automaton.hoa").is_file()
            states, threshold = int(meta["states"]), int(meta["bool_threshold"])
            thresholds.add(threshold)
            for event in sorted(directory.glob("cpre-*.tsv")):
                different, many = check_event(event, states, threshold)
                checked += 1
                changed += different
                multiple += many
        assert thresholds == {2, 3} and checked and changed and multiple, (
            thresholds, checked, changed, multiple)
        print(f"checked {checked} production CPre snapshots ({changed} changed regions)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
