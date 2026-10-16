#!/usr/bin/env python3
"""Small fixed-bound game and command-interface differential for exact conversion."""

import csv
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def run(command, *, env=None):
    return subprocess.run(command, text=True, capture_output=True, timeout=30, env=env)


def check_unused_aps(replay, root):
    # The review fixture reaches the symmetry solver with three active outputs;
    # 13 unused outputs previously forced an "output letter cap" decline.
    hoa = Path(__file__).with_name("replay-unused-ap.hoa")
    inputs = [f"r_{index}" for index in range(3)]
    outputs = [f"g_{index}" for index in range(3)]
    unused_inputs = [f"unused_input{index}" for index in range(13)]
    unused_outputs = [f"dummy{index}" for index in range(13)]
    baseline = None
    for label, extra_inputs, extra_outputs in [
        ("active", [], []),
        ("unused-inputs", unused_inputs, []),
        ("unused-outputs", [], unused_outputs),
        ("unused-both", unused_inputs, unused_outputs),
    ]:
        phases = root / "unused-aps" / label
        phases.mkdir(parents=True)
        result = run(
            [
                replay,
                "--hoa",
                str(hoa),
                "--inputs",
                ",".join(inputs + extra_inputs),
                "--outputs",
                ",".join(outputs + extra_outputs),
                "--game-backend",
                "backward",
                "--spot-fast",
                "det",
                "-k",
                "2",
                "-K",
                "2",
            ],
            env=os.environ | {"ACACIA_PHASE_RECORDS": str(phases.resolve())},
        )
        decision = (result.returncode, result.stdout.strip())
        assert decision == (0, "REALIZABLE"), (label, decision, result.stderr)
        records = [
            json.loads(line)
            for file in phases.glob("*.jsonl")
            for line in file.read_text().splitlines()
        ]
        routing = [
            tuple(
                record.get(key)
                for key in (
                    "event",
                    "route",
                    "stage",
                    "reason",
                    "requested_backend",
                    "effective_backend",
                )
            )
            for record in records
            if record.get("event")
            in {
                "route_selected",
                "route_start",
                "decline",
                "equivariance_complete",
                "stage_entry",
                "stage_completion",
            }
        ]
        assert routing, (label, records)
        assert not any(r.get("reason") == "output letter cap" for r in records), label
        if baseline is None:
            baseline = (decision, routing)
        else:
            assert (decision, routing) == baseline, (label, baseline, decision, routing)
    print("unused interface APs: identical decisions and routing for all four partitions")


def main():
    generator, converter, replay, scratch = sys.argv[1:]
    Path(scratch).mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=scratch) as temporary:
        root = Path(temporary)
        made = run([generator, "--fixtures", temporary])
        assert made.returncode == 0, made.stderr
        check_unused_aps(replay, root)
        answers = {}
        tier_counts = {tier: 0 for tier in range(1, 5)}
        fast_claims = {tier: 0 for tier in range(1, 4)}
        for stem, orientation, inputs, outputs, expected in csv.reader(
            (root / "games.tsv").read_text().splitlines(), delimiter="\t"
        ):
            output = root / f"{stem}-cli.hoa"
            metrics = root / f"{stem}.jsonl"
            converted = run(
                [
                    converter,
                    "--hoa",
                    str(root / f"{stem}-tela.hoa"),
                    "--output",
                    str(output),
                    "--metrics",
                    str(metrics),
                ]
            )
            assert converted.returncode == 0, converted.stderr
            records = [json.loads(line) for line in metrics.read_text().splitlines()]
            complete = {r["stage"]: r for r in records if r["event"] == "complete"}
            tier = complete["tier"]["tier"]
            tier_counts[tier] += 1
            final = complete["write"]
            assert ("generalized" in complete) == (tier == 4), (stem, tier, complete)
            if tier <= 3:
                assert final["determinism_preserved"], (stem, tier)
                assert "state-acceptance" not in complete
            else:
                assert final["acceptance_sets"] == 1 and final["state_acceptance"]
            if stem.endswith("-rabin"):
                assert tier == 3, (stem, tier)
            assert all(r["seconds"] >= 0 for r in complete.values())
            for fast in ["off", "det"]:
                verdicts = []
                for kind, hoa in [("ba", root / f"{stem}-ba.hoa"), ("routed", output)]:
                    phases = root / "phases" / f"{stem}-{fast}-{kind}"
                    phases.mkdir(parents=True)
                    env = os.environ | {"ACACIA_PHASE_RECORDS": str(phases)}
                    result = run(
                        [
                            replay,
                            "--hoa",
                            str(hoa),
                            "--inputs",
                            inputs,
                            "--outputs",
                            outputs,
                            "--orientation",
                            orientation,
                            "--game-backend",
                            "backward",
                            "--spot-fast",
                            fast,
                            "-k",
                            "16",
                            "-K",
                            "16",
                        ],
                        env=env,
                    )
                    assert result.returncode in (0, 1, 2), result.stderr
                    verdicts.append(result.stdout.strip())
                    if fast == "det" and kind == "routed" and tier <= 3:
                        records = [
                            json.loads(line)
                            for file in phases.glob("*.jsonl")
                            for line in file.read_text().splitlines()
                        ]
                        claimed = any(
                            r.get("stage") == "spot-fast"
                            and r.get("event") == "stage_completion"
                            and r.get("reason") == "conclusive"
                            for r in records
                        )
                        if orientation != "unreal-automaton":
                            assert claimed, (stem, tier, records)
                        fast_claims[tier] += claimed
                assert verdicts[0] == verdicts[1], (stem, fast, verdicts)
                assert verdicts[0] in (expected, "UNKNOWN"), (stem, expected, verdicts)
                answers.setdefault(stem.split("-", 1)[0], []).append(verdicts[0])
        assert all(tier_counts.values()), tier_counts
        assert all(fast_claims.values()), fast_claims
        print(json.dumps({"tier_counts": tier_counts, "spot_fast_claims": fast_claims}))
        for index, verdicts in answers.items():
            assert len(set(verdicts) - {"UNKNOWN"}) == 1, (index, verdicts)
        phases = [
            json.loads(line)
            for f in (root / "phases").rglob("*.jsonl")
            for line in f.read_text().splitlines()
        ]
        assert any(r.get("stage") == "search" for r in phases)
        assert any(r.get("reason") == "inapplicable-fixed-point" for r in phases)
        budgets = [r for r in phases if r.get("phase") == "equivariance_budget"]
        if budgets:  # Builds without symmetry support do not enter this pre-pass.
            assert all(r["total_ns"] == 3_000_000_000 for r in budgets)
            assert all(r["outer_deadline_ns"] == 0 for r in budgets)
        # The driver's separate processes must preserve the same language/game.
        source = root / "11-real-tela.hoa"
        generalized = root / "split-gba.hoa"
        split_sba = root / "split-sba.hoa"
        for stage, src, dst in [
            ("route", source, generalized),
            ("state", generalized, split_sba),
        ]:
            result = run(
                [
                    converter,
                    "--hoa",
                    str(src),
                    "--output",
                    str(dst),
                    "--metrics",
                    str(root / f"split-{stage}.jsonl"),
                    "--stage",
                    stage,
                ]
            )
            assert result.returncode == 0, result.stderr
        split_game = run(
            [
                replay,
                "--hoa",
                str(split_sba),
                "--inputs",
                "i",
                "--outputs",
                "o",
                "--game-backend",
                "backward",
                "--spot-fast",
                "off",
                "-k",
                "16",
                "-K",
                "16",
            ]
        )
        assert split_game.returncode == 0 and split_game.stdout.strip() == "REALIZABLE"
        # Reject bad boundaries before conversion or game construction.
        nondet = root / "nondet.hoa"
        nondet.write_text(
            "HOA: v1\nStates: 2\nStart: 0\nAP: 0\nAcceptance: 0 t\n"
            "--BODY--\nState: 0\n[t] 0\n[t] 1\nState: 1\n[t] 1\n--END--\n"
        )
        invalid = run(
            [
                converter,
                "--hoa",
                str(nondet),
                "--output",
                str(root / "bad.hoa"),
                "--metrics",
                str(root / "bad.jsonl"),
            ]
        )
        assert invalid.returncode == 2 and "deterministic" in invalid.stderr, invalid
        alternating = root / "alternating.hoa"
        alternating.write_text(
            "HOA: v1\nStates: 2\nStart: 0&1\nAP: 0\nAcceptance: 1 Inf(0)\n"
            "properties: state-acc\n--BODY--\nState: 0 {0}\n[t] 0\n"
            "State: 1 {0}\n[t] 1\n--END--\n"
        )
        invalid = run([replay, "--hoa", str(alternating)])
        assert invalid.returncode == 3 and "existential" in invalid.stderr, invalid
        alias = root / "alias.hoa"
        alias.symlink_to(source)
        original = source.read_bytes()
        invalid = run(
            [
                converter,
                "--hoa",
                str(source),
                "--output",
                str(alias),
                "--metrics",
                str(root / "alias.jsonl"),
            ]
        )
        assert invalid.returncode == 2 and source.read_bytes() == original, invalid
        invalid = run([replay, "--hoa", str(root / "2-real-ba.hoa"), "--inputs", "i"])
        assert invalid.returncode == 3 and "partition" in invalid.stderr, invalid
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
