#!/usr/bin/env python3
"""Exercise the live TLSF/worker boundary and fresh C4/C5 construction."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = str(Path(sys.argv[1]).resolve())


def captured_run(root, name, formula, *, fast="det", backend="spot-guarded-sparse",
                 extra=(), expected=0, polarity="real:small"):
    captures = root / name
    env = dict(os.environ, ACACIA_SPOT_CAPTURE_DIR=str(captures),
               ACACIA_SPOT_CAPTURE_HISTORY="1")
    command = [binary, "-f", formula, "-i", "i", "-o", "o,p", "-K", "5",
               "--arms", f"{polarity}:{backend}", "--spot-fast", fast, *extra]
    result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=10)
    assert result.returncode == expected, (command, result.stdout, result.stderr)
    records = [json.loads(p.read_text()) for p in captures.glob("*.json")]
    histories = [[json.loads(line) for line in p.read_text().splitlines()]
                 for p in captures.glob("*.history.jsonl")]
    return records, histories


cases = [
    ("Mealy", "Mealy", "G(i <-> X(o))", "real", 0),
    ("Mealy", "Moore", "G(i <-> X(o))", "real", 0),
    ("Moore", "Mealy", "G(i <-> o)", "unreal", 1),
    ("Moore", "Moore", "G(i <-> o)", "unreal", 1),
    ("Mealy,Strict", "Mealy", "G(o <-> X(i))", "unreal", 1),
]
with tempfile.TemporaryDirectory(prefix="acacia-spot-worker-") as directory:
    root = Path(directory)
    for policy in ("verify-all", "scheduling-hint"):
        for index, (semantics, target, formula, polarity, expected) in enumerate(cases):
            spec = root / f"{index}.tlsf"
            spec.write_text(f'''INFO {{ TITLE: "worker boundary" SEMANTICS: {semantics} TARGET: {target} }}
MAIN {{ INPUTS {{ i; }} OUTPUTS {{ o; }} GUARANTEES {{ {formula}; }} }}
''')
            pair = []
            for provider in ("spot-eager", "spot-lazy"):
                captures = root / f"{policy}-{index}-{provider}"
                arm = "real:small" if polarity == "real" else "unreal:formula"
                env = dict(os.environ, ACACIA_SPOT_CAPTURE_DIR=str(captures),
                           ACACIA_SPOT_CAPTURE_HISTORY="1")
                command = [binary, "-T", str(spec), "--spot-fast", "off", "-K", "5",
                           "--arms", f"{arm}:spot-guarded:{provider}",
                           "--loss-check-policy", policy]
                result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=10)
                assert result.returncode == expected, (command, result.stdout, result.stderr)
                records = [json.loads(p.read_text()) for p in captures.glob("*.json")]
                assert len(records) == 1, records
                record = records[0]
                assert record["status"] == "WIN_K" and float(record["verification_ms"]) >= 0
                assert record["record_version"] == "2" and record["worker_id"]
                assert record["worker_end"] == "returned" and record["attempt_end"] == "true"
                assert record["search_started"] == "true" and record["evidence"] == "verified-win"
                history = [json.loads(line) for path in captures.glob("*.history.jsonl")
                           for line in path.read_text().splitlines()]
                attempts = {r["attempt_id"]: r for r in history if r.get("attempt_end") == "true"}
                for attempt in attempts.values():
                    assert float(attempt.get("attempt_row_generation_ms", 0)) <= (
                        float(attempt["search_ms"]) + float(attempt["verification_ms"]))
                assert abs(float(record["cumulative_search_ms"]) - sum(
                    float(r["search_ms"]) for r in attempts.values())) < 0.001
                assert record["inputs"] == (["i"] if polarity == "real" else ["o"])
                assert record["outputs"] == (["o"] if polarity == "real" else ["i"])
                assert record["requested_provider"] == provider and record["provider"] == provider
                pair.append(record)
            for field in ("worker_formula", "inputs", "outputs", "ap_order", "partition", "k", "status"):
                assert pair[0][field] == pair[1][field], (field, pair)
            assert pair[0]["worker_pid"] != pair[1]["worker_pid"]
            assert int(pair[1]["wrapper_rows_generated"]) <= int(pair[0]["total_wrapper_rows"])
    print("PASS native TLSF boundary: 5 eager/lazy pairs per policy and verified attempt histories")

    # Both wrappers and both polarities: losses/hints only schedule, wins alone
    # map to REALIZABLE/UNREALIZABLE. Real attempts use the original job and
    # unreal attempts use the existing swapped/shifted/negated job. Omitting the
    # policy must verify losses too: scheduling hints are strictly opt-in.
    for backend in ("spot-guarded-sparse", "spot-guarded:spot-lazy", "spot-guarded:spot-eager"):
        for polarity in ("real:small", "unreal:formula"):
            for policy in (None, "verify-all", "scheduling-hint"):
                policy_args = () if policy is None else ("--loss-check-policy", policy)
                for winning in (False, True):
                    real_wins = (polarity == "real:small") == winning
                    formula = "G(i <-> X(o))" if real_wins else "G F i & G F o"
                    expected = (0 if polarity == "real:small" else 1) if winning else 2
                    name = f"policy-{backend}-{polarity}-{policy or 'default'}-{winning}"
                    records, histories = captured_run(
                        root, name, formula, fast="off", backend=backend, polarity=polarity,
                        extra=(*policy_args, "-M", "1", "-I", "1", "-K", "3"),
                        expected=expected)
                    assert records and histories, name
                    histories_by_worker = {history[0]["worker_id"]: history for history in histories}
                    for record in records:
                        history = histories_by_worker[record["worker_id"]]
                        attempts = {r["attempt_id"]: r for r in history
                                    if r.get("attempt_end") == "true"}
                        assert attempts, (name, record)
                        ended = list(attempts.values())
                        for attempt in ended:
                            is_win = attempt["evidence"] == "verified-win"
                            if not winning:
                                assert not is_win, (name, attempt)
                            hint = not is_win and policy == "scheduling-hint"
                            assert attempt["evidence"] == (
                                "verified-win" if is_win else "loss-hint" if hint else "verified-loss"
                            ), (name, attempt)
                            assert attempt["status"] == ("WIN_K" if is_win else "LOSS_HINT" if hint else "LOSE_K")
                            assert int(attempt["loss_hints"]) == int(hint)
                            assert int(attempt["win_verification_calls"]) == int(is_win)
                            assert int(attempt["loss_verification_calls"]) == int(not is_win and not hint)
                        if not winning:
                            assert record["worker_result"] == "unknown" and record["k"] == "3"
                            assert len(ended) == 3, (name, ended)
                        else:
                            assert record["evidence"] == "verified-win"
                        for field in ("loss_hints", "loss_hint_ms", "loss_verification_calls",
                                      "loss_verification_ms", "win_verification_calls", "win_verification_ms"):
                            assert abs(float(record[f"cumulative_{field}"]) - sum(
                                float(r[field]) for r in ended)) < 0.001, (name, field, record)
    print("PASS runtime policies (omitted/verify-all/scheduling-hint): frozen/lazy/eager, "
          "both polarities, UNKNOWN caps, checker counts and metadata")

    records, histories = captured_run(root, "fast-enabled", "G(i <-> X(o))")
    assert len(records) == 1
    record = records[0]
    assert record["worker_reason"] == "spot-fast-path"
    assert record["requested_backend"] == "spot-guarded-sparse"
    assert record["backend"] == "spot-fast-det" and record["provider"] == "frozen-graph"
    assert record["stage"] == "attempt-end" and record["attempt_end"] == "true"
    assert record["worker_end"] == "returned" and record["search_started"] == "true"
    assert record["status"] == "WIN" and record["evidence"] == "spot-game"
    assert "k" not in record and "verification_ms" not in record
    assert float(record["search_ms"]) >= 0
    assert record["search_ms"] == record["cumulative_search_ms"]
    stages = [r["stage"] for r in histories[0]]
    assert stages.index("attempt-start") < stages.index("action-construction")
    assert stages.index("action-construction") < stages.index("search") < stages.index("attempt-end")
    print("PASS spot-fast-enabled: effective backend, unbounded attempt, action/search/end")

    records, histories = captured_run(root, "fast-declined", "G F (i <-> X(o))")
    assert len(records) == 1
    record = records[0]
    assert record["backend"] == "spot-guarded-sparse" and record["fallback"] == "true"
    assert record["status"] == "WIN_K" and record["search_started"] == "true"
    assert record["attempt_end"] == "true" and record["worker_end"] == "returned"
    history = histories[0]
    declined = next(i for i, r in enumerate(history) if r.get("evidence") == "declined")
    assert history[declined]["backend"] == "spot-fast"
    assert history[declined]["attempt_end"] == "true"
    resumed = history[declined + 1:]
    preprocessing = next(r for r in resumed if r["stage"] == "preprocessing")
    assert preprocessing["backend"] == "spot-guarded-sparse"
    assert preprocessing["search_started"] == "false" and "attempt_id" not in preprocessing
    assert int(preprocessing["segment_id"]) > int(history[declined]["segment_id"])
    assert int(record["attempt_id"]) > int(history[declined]["attempt_id"])
    print("PASS spot-fast-declined: completed decline, new segment, preprocessing resumed")

    # Every component must finish its own attempt (one if decomposition is disabled).
    records, _ = captured_run(root, "decomposition", "G(i <-> X(o)) & (G F p | F G !p)")
    assert records
    assert len({r["worker_id"] for r in records}) == len(records)
    assert all(r["stage"] == "attempt-end" and r["search_started"] == "true"
               and r["worker_end"] == "returned" for r in records)
    print("PASS decomposition: completed search records for every captured component")
print("10 native TLSF boundary pairs: semantics, polarity, exact construction, and certificates agree")
