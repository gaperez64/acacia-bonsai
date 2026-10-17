#!/usr/bin/env python3
"""Exercise TLSF conversion and the shipping portfolio under a real RLIMIT_AS."""

import json
import os
from pathlib import Path
import resource
import subprocess
import sys
import tempfile


def main():
    binary = Path(sys.argv[1]).resolve()
    scratch = Path(sys.argv[2]).resolve()
    root = Path(__file__).resolve().parents[1]
    helper = root / "scripts/acacia-config.py"
    shipping = subprocess.check_output(
        [sys.executable, str(helper), "list-group", "docker_default"], text=True,
    ).splitlines()
    preset = shipping[0]
    config = json.loads(subprocess.check_output(
        [sys.executable, str(helper), "show", preset], text=True,
    ))
    arms = config["default_arms"]
    env = dict(os.environ)
    env.pop("OXIDD_STACK_SIZE", None)
    with tempfile.TemporaryDirectory(prefix="tlsf-address-space-", dir=scratch) as temporary:
        spec = Path(temporary) / "trivial.tlsf"
        spec.write_text('''INFO { TITLE: "address space" SEMANTICS: Mealy TARGET: Mealy }
MAIN { INPUTS { i; } OUTPUTS { o; } GUARANTEE { G o; } }
''')
        def run(mib, *, stack=None, selected_arms=None, records=None):
            def limit():
                bound = mib * 1024 * 1024
                resource.setrlimit(resource.RLIMIT_AS, (bound, bound))
                resource.setrlimit(resource.RLIMIT_CORE, (0, 0))

            child_env = dict(env)
            if stack is not None:
                child_env["OXIDD_STACK_SIZE"] = str(stack)
            if records is not None:
                child_env["ACACIA_PHASE_RECORDS"] = str(records)
            args = [str(binary), "-T", str(spec)]
            if selected_arms is not None:
                args += ["--arms", selected_arms]
            return subprocess.run(
                args, env=child_env, preexec_fn=limit,
                capture_output=True, text=True, timeout=60,
            )

        def realizable(result):
            assert result.returncode == 0, result
            assert result.stdout.strip() == "REALIZABLE", result

        # Use the compiled default portfolio, with no environment stack override.
        for mib in (512, 1024, 2048):
            result = run(mib)
            realizable(result)
            assert "panicked" not in result.stderr, (mib, result.stderr)
            print(f"{preset}: RLIMIT_AS={mib} MiB: REALIZABLE (no stack override)")

        result = run(512, stack=8388608, selected_arms="both:gr1:oxidd")
        realizable(result)
        assert "panicked" not in result.stderr, result
        print("RLIMIT_AS=512 MiB, OXIDD_STACK_SIZE=8 MiB, both:gr1:oxidd: REALIZABLE")

        # The infallible Rust constructor aborts when its stack cannot fit.
        # The inherited child SIGABRT handler exits UNKNOWN (code 2); a lone
        # native arm must contribute no verdict. The portfolio-deadline test
        # separately covers workers killed by an unhandled signal.
        impossible_stack = 1024 * 1024 * 1024
        result = run(512, stack=impossible_stack, selected_arms="both:gr1:oxidd")
        assert result.returncode == 2, result
        assert result.stdout.strip() == "", result
        assert result.stderr.rstrip().endswith("UNKNOWN"), result
        assert "panicked" in result.stderr, result
        print("native-only OxiDD abort: parent exits normally with UNKNOWN")

        # Under the same conditions, the shipping race must accept a clean
        # sibling's verdict. A fast sibling may cancel native work before it
        # reaches the constructor, so require the abort in the lone-arm case
        # above and identify the actual default-portfolio winner here.
        assert "oxidd" in arms, arms
        records = Path(temporary) / "worker-records"
        records.mkdir()
        result = run(512, stack=impossible_stack, records=records)
        realizable(result)
        rows = [json.loads(line) for path in records.glob("*.jsonl")
                for line in path.read_text().splitlines()]
        terminal = [row for row in rows if row.get("event") == "parent_terminal"]
        assert any(row["exit_code"] == 0 and row["signal"] == 0 and
                   row["requested_backend"] != "oxidd" for row in terminal), (result, terminal)
        print("default portfolio with infeasible OxiDD stack: sibling REALIZABLE, parent exit 0")


if __name__ == "__main__":
    main()
