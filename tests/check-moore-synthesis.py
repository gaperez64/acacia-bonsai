#!/usr/bin/env python3
"""Synthesize small TLSF specs, then verify emitted AIGs by explicit products."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

from moore_aiger_oracle import Aiger, check, delayed_table_monitor
from obligation_test_support import LegacyGate, records as phase_records


def boolean_formula(inputs, table):
    terms = []
    for index, value in enumerate(table):
        if value:
            terms.append("(" + " && ".join(
                name if (index >> bit) & 1 else "!" + name
                for bit, name in enumerate(inputs)) + ")")
    return " || ".join(terms) if terms else "false"


def cases(exhaustive):
    for semantics in ("Moore", "Mealy", "Strict,Moore", "Strict,Mealy"):
        for target in ("Moore", "Mealy"):
            tables = [("request", (False, True)),
                      ("request,event", (False, True, True, False))]
            if exhaustive:
                tables = [("request", tuple(bool(mask & (1 << i)) for i in range(2)))
                          for mask in range(4)]
                tables += [("request,event", tuple(bool(mask & (1 << i)) for i in range(4)))
                           for mask in range(16)]
            for names, table in tables:
                inputs = names.split(",")
                for delay in ((1, 2, 3) if exhaustive else (1, 2)):
                    for preset in (None, False, True):
                        if exhaustive and len(inputs) == 2 and preset is not None:
                            continue
                        actual_delay = delay + (semantics.endswith("Mealy") and target == "Moore")
                        actual_delay -= semantics.endswith("Moore") and target == "Mealy"
                        preset_step = int(semantics.endswith("Mealy") and target == "Moore")
                        start_step = int(semantics.endswith("Moore") and target == "Mealy")
                        monitor = delayed_table_monitor(inputs, "grant", table, actual_delay,
                                                        preset, preset_step, start_step)
                        relation = boolean_formula(inputs, table)
                        guarantee = f"G ({'X ' * delay}grant <-> ({relation}));"
                        preset_text = "" if preset is None else (
                            "PRESET { " + ("" if preset else "!") + "grant; }")
                        text = f'''
INFO {{ TITLE: "generated timing" DESCRIPTION: "exact safety product"
        SEMANTICS: {semantics} TARGET: {target} }}
MAIN {{ INPUTS {{ {"; ".join(inputs)}; }} OUTPUTS {{ grant; }}
        {preset_text} GUARANTEE {{ {guarantee} }} }}
'''
                        yield (semantics, target, names, delay, preset, table), text, monitor
            if semantics.startswith("Strict") and target == semantics.split(",")[-1]:
                # Strict safety must hold until the first REQUIRE violation;
                # future assumption failure cannot forgive an earlier bad output.
                def strict_transition(released, letter):
                    if released or not letter["request"]:
                        return True
                    return False if letter["grant"] else None

                text = f'''
INFO {{ TITLE: "strict safety timing" SEMANTICS: {semantics} TARGET: {target} }}
MAIN {{ INPUTS {{ request; }} OUTPUTS {{ grant; }}
        REQUIRE {{ request; }} ASSERT {{ grant; }} }}
'''
                yield (semantics, target, "request", "strict-assumptions"), text, (
                    False, strict_transition)
            # No-input synthesis uses a separate lasso-to-AIG emission path.
            for initial_output in (False, True):
                output_offset = int(semantics.endswith("Mealy") and target == "Moore")

                def toggle_transition(state, letter, first=initial_output,
                                      offset=output_offset):
                    expected, skipped = state
                    if skipped < offset:
                        return expected, skipped + 1
                    return (not expected, skipped) if letter["grant"] == expected else None

                text = f'''
INFO {{ TITLE: "autonomous timing" DESCRIPTION: "exact safety product"
        SEMANTICS: {semantics} TARGET: {target} }}
MAIN {{ OUTPUTS {{ grant; }} PRESET {{ {"" if initial_output else "!"}grant; }}
        GUARANTEE {{ G (X grant <-> !grant); }} }}
'''
                yield (semantics, target, "no-input", initial_output), text, (
                    (initial_output, 0), toggle_transition)

    # Immediate Mealy equality and its Moore-target lowering provide controllers
    # that must fail when the offline oracle deliberately swaps target semantics.
    for target in ("Mealy", "Moore"):
        text = f'''
INFO {{ TITLE: "generated immediate timing" SEMANTICS: Mealy TARGET: {target} }}
MAIN {{ INPUTS {{ request; }} OUTPUTS {{ grant; }}
        GUARANTEE {{ G (grant <-> request); }} }}
'''
        yield ("Mealy", target, "request", 0, None, (False, True), "swap-semantics"), text, (
            delayed_table_monitor(["request"], "grant", (False, True), int(target == "Moore")))


def run(binary, directory, exhaustive=False, extra=(), require_obligation=False):
    directory.mkdir(parents=True, exist_ok=True)
    rows = []
    for index, (description, text, monitor) in enumerate(cases(exhaustive)):
        spec = directory / f"case-{index}.tlsf"
        aig = directory / f"case-{index}.aag"
        spec.write_text(text)
        command = [str(binary), "-T", str(spec), "-s", str(aig), *extra]
        env = os.environ.copy()
        phases = directory
        if require_obligation:
            phases = directory / f"case-{index}-phases"
            phases.mkdir()
            env["ACACIA_PHASE_RECORDS"] = str(phases)
            env["ACACIA_TEST_OBLIGATION_RECORD_ACK"] = "1"
            env["ACACIA_TEST_OBLIGATION_OUTPUT"] = str(aig)
            # Exercise a helper slower than the former 300 ms race-steering delay.
            env["ACACIA_TEST_OBLIGATION_HELPER_DELAY_MS"] = "500" if index == 0 else "0"
        try:
            with LegacyGate(env, require_obligation) as legacy:
                with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                      text=True, env=env, pass_fds=legacy.descriptors) as process:
                    try:
                        stdout, stderr = legacy.communicate(process, phases, timeout=20)
                    except subprocess.TimeoutExpired:
                        process.terminate()
                        try:
                            process.communicate(timeout=3)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.communicate()
                        raise
                    result = subprocess.CompletedProcess(
                        command, process.returncode, stdout, stderr)
        except subprocess.TimeoutExpired:
            rows.append({"case": description, "error": "synthesis timeout",
                         "records": phase_records(phases, complete_only=True)})
            continue
        if result.returncode != 0 or not aig.exists():
            rows.append({"case": description, "error": result.stderr[-2000:],
                         "exit": result.returncode,
                         "records": phase_records(phases, complete_only=True)})
            continue
        if require_obligation:
            records = phase_records(phases)
            winners = [row for row in records if row.get("event") == "parent_winner"]
            checks = [row for row in records if row.get("event") == "obligation_completion"]
            if (len(winners) != 1 or winners[0]["effective_backend"] != "obligation"
                    or len(checks) != 1 or checks[0]["independent_checked"]
                    or checks[0]["trust"] != "spot_exact"):
                rows.append({"case": description, "error": "obligation did not produce controller",
                             "records": records})
                continue
        circuit = Aiger.read(aig)
        expected_inputs = () if description[2] == "no-input" else tuple(
            reversed(description[2].split(",")))
        if (sorted(circuit.input_names) != sorted(expected_inputs)
                or circuit.output_names != ("grant",)):
            rows.append({"case": description, "error": "wrong AIGER signal names"})
            continue
        oracle = check(circuit, *monitor, moore=description[1] == "Moore")
        if description[-1] == "swap-semantics":
            swapped_moore = description[1] == "Mealy"
            swapped_monitor = delayed_table_monitor(["request"], "grant", (False, True),
                                                    int(swapped_moore))
            swapped = check(circuit, *swapped_monitor, moore=swapped_moore)
            if swapped.valid:
                rows.append({"case": description, "error": "swapped semantics escaped oracle"})
                continue
        rows.append({"case": description, "valid": oracle.valid, "states": oracle.states,
                     "transitions": oracle.transitions, "counterexample": oracle.counterexample,
                     "input_dependent": oracle.input_dependent})
    (directory / "results.json").write_text(json.dumps(rows, indent=2) + "\n")
    failures = [row for row in rows if not row.get("valid", False)]
    print(f"{len(rows) - len(failures)}/{len(rows)} exact controller checks passed")
    for row in failures:
        print(json.dumps(row))
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--exhaustive", action="store_true")
    parser.add_argument("--require-obligation", action="store_true")
    parser.add_argument("--ab-option", action="append", default=[])
    args = parser.parse_args()
    if args.output_dir:
        rows = run(args.binary, args.output_dir, args.exhaustive, args.ab_option,
                   args.require_obligation)
    else:
        # All scratch lives in the caller's task-specific TMPDIR.
        with tempfile.TemporaryDirectory(dir=os.environ.get("TMPDIR")) as temporary:
            rows = run(args.binary, Path(temporary), args.exhaustive, args.ab_option,
                   args.require_obligation)
    return int(any(not row.get("valid", False) for row in rows))


if __name__ == "__main__":
    raise SystemExit(main())
