#!/usr/bin/env python3
"""Inspect route records for trusted, verified, fallback and interrupted runs."""

import argparse
import json
from pathlib import Path
import random
import subprocess
import tempfile
import threading


def records(path):
    return [json.loads(line) for line in path.read_text().splitlines()]


def invoke(binary, directory, name, *options, expected=0):
    metrics = directory / f"{name}.jsonl"
    game = directory / f"{name}.json"
    controller = directory / f"{name}.aag"
    existing = set(directory.iterdir())
    process = subprocess.run(
        [str(binary), "--formula", "GF i -> GF o", "--inputs", "i", "--outputs", "o",
         "--metrics", str(metrics), "--game-output", str(game),
         "--controller-output", str(controller), *options],
        capture_output=True, text=True, timeout=20, check=False,
    )
    assert process.returncode == expected, process.stderr
    outputs = {metrics, game} | ({controller} if expected == 0 else set())
    assert set(directory.iterdir()) == existing | outputs, "temporary artifact was retained"
    events = records(metrics)
    assert events == [json.loads(line) for line in process.stdout.splitlines()
                      if line.startswith("{")]
    return events, game, controller


def summary(events):
    return next(row for row in events if row.get("event") == "result")


def check_parameters(events):
    parameters = [row for row in events if row.get("event") == "parameters"]
    assert parameters
    assert [row["pairs"] for row in parameters] == list(range(1, len(parameters) + 1))
    final = summary(events)
    assert (parameters[-1]["k"], parameters[-1]["t"], parameters[-1]["pairs"]) == (
        final["k"], final["t"], final["pairs"])
    assert events.index(parameters[0]) > next(index for index, row in enumerate(events)
                                            if row.get("stage") == "solve")
    return final


def interrupted(binary, directory):
    # A generated deterministic, strongly connected parity graph gives the reader
    # time to interrupt lifting. Its structure is independent of any corpus input.
    hoa = directory / "interrupt.hoa"
    randomizer = random.Random(1729)
    count = 16384
    with hoa.open("w") as output:
        output.write(f'HOA: v1\nStates: {count}\nStart: 0\nAP: 2 "i" "o"\n'
                     'acc-name: parity max even 5\n'
                     'Acceptance: 5 Inf(4) | (Fin(4) & Fin(3) & Inf(2)) | '
                     '(Fin(4) & Fin(3) & Fin(2) & Fin(1) & Inf(0))\n'
                     'properties: trans-labels explicit-labels trans-acc deterministic complete\n'
                     '--BODY--\n')
        for vertex in range(count):
            output.write(f"State: {vertex}\n")
            for index, label in enumerate(("!0 & !1", "!0 & 1", "0 & !1", "0 & 1")):
                target = (vertex + 1) % count if index == 0 else randomizer.randrange(count)
                output.write(f"[{label}] {target} {{{vertex % 5}}}\n")
        output.write("--END--\n")
    metrics = directory / "interrupt.jsonl"
    process = subprocess.Popen(
        [str(binary), "--hoa", str(hoa), "--inputs", "i", "--outputs", "o",
         "--metrics", str(metrics), "--verify", "off"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
    )
    timer = threading.Timer(30, process.kill)
    timer.start()
    try:
        reached = None
        for line in process.stdout:
            row = json.loads(line) if line.startswith("{") else {}
            if row.get("event") == "parameters":
                reached = row
                process.kill()
                break
        _, errors = process.communicate(timeout=10)
        assert reached is not None, errors
        assert process.returncode < 0, "solver completed before interruption"
        events = records(metrics)
        assert reached in events, "interruption lost reached parameters"
        assert not any(row.get("event") == "result" for row in events)
        assert not any(row.get("stage") == "solve" and row["event"] == "complete"
                       for row in events)
        assert reached["k"] == reached["t"] == reached["pairs"] == 1
    finally:
        timer.cancel()
        if process.poll() is None:
            process.kill()
            process.communicate()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="parity-events-") as folder:
        directory = Path(folder)
        events, game, controller = invoke(args.binary, directory, "default")
        final = check_parameters(events)
        assert final["tier"] == 4 and not final["fallback"]
        assert not final["checked"] and final["verification"] == "none"
        assert events[0]["verify"] == "off"
        assert not any(row.get("stage", "").startswith("verify-") for row in events)
        assert all(vertex["winner"] in (0, 1) for vertex in json.loads(game.read_text())["vertices"])
        assert controller.read_text().startswith("aag ")

        events, _, _ = invoke(args.binary, directory, "off", "--verify", "off",
                              "--specification", "0")
        assert not summary(events)["checked"]
        assert not any(row.get("stage", "").startswith("verify-") for row in events)
        events, _, _ = invoke(args.binary, directory, "on", "--verify", "on")
        assert summary(events)["checked"] and events[0]["verify"] == "on"
        assert {row.get("stage") for row in events} >= {"verify-game", "verify-controller"}
        events, _, _ = invoke(args.binary, directory, "wrong", "--verify", "on",
                              "--specification", "0", expected=2)
        assert not any(row.get("event") == "result" for row in events)
        assert any(row.get("stage") == "verify-controller" for row in events)

        events, _, _ = invoke(args.binary, directory, "fallback", "--operation-budget", "1",
                              "--verify", "on")
        final = check_parameters(events)
        assert final["fallback"]
        fallback = [row for row in events if row.get("stage") == "fallback"]
        assert [row["event"] for row in fallback] == ["begin", "complete"]
        assert fallback[0]["seconds"] is None
        assert fallback[1]["seconds"] == final["fallback_seconds"] >= 0
        for row in fallback:
            assert (row["k"], row["t"], row["vertices"], row["reason"]) == (
                final["k"], final["t"], final["fallback_vertices"], final["fallback_reason"])

        # Rename failure must preserve the destination and remove its temporary file.
        destination = directory / "preserved"
        destination.mkdir()
        sentinel = destination / "sentinel"
        sentinel.write_text("preserved")
        existing = set(directory.iterdir())
        process = subprocess.run(
            [str(args.binary), "--formula", "1", "--inputs", "i", "--outputs", "o",
             "--game-output", str(destination)],
            capture_output=True, text=True, timeout=20, check=False,
        )
        assert process.returncode == 2
        assert sentinel.read_text() == "preserved"
        assert set(directory.iterdir()) == existing, "failed rename left a temporary artifact"
        interrupted(args.binary, directory)
    print("Verification modes, atomic artifacts and normal/interrupted/fallback records passed")


if __name__ == "__main__":
    main()
