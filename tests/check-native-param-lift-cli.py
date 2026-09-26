#!/usr/bin/env python3
"""Exercise the opt-in lifting arm through Acacia's fork and checker boundary."""

from __future__ import annotations

from importlib.util import module_from_spec, spec_from_file_location
import json
import os
from pathlib import Path
import select
import subprocess
import sys
import tempfile
import time


ROOT = Path(__file__).resolve().parents[1]
spec = spec_from_file_location("obfuscate_tlsf", ROOT / "benchmarking" /
                               "gr1-par2-20260923" / "obfuscate-tlsf.py")
assert spec and spec.loader
obfuscator = module_from_spec(spec)
spec.loader.exec_module(obfuscator)
ARM = "real:param-lift:oxidd"
FORMULAS = ("G F {g}[i]", "G ({r}[i] -> F {g}[i])",
            "G F ({g}[i] || {r}[i])")


def source(formula: str, request: str, grant: str, *, count: int = 5,
           enum: bool = False) -> str:
    definitions = "DEFINITIONS { enum Mode = Idle: 00 Active: 01 Done: 10; }" if enum else ""
    enum_input = "Mode controllable_state; " if enum else ""
    return (f'INFO {{ TITLE: "unseen" SEMANTICS: Mealy TARGET: Mealy }}\n'
            f'GLOBAL {{ PARAMETERS {{ span = {count}; }} {definitions} }}\n'
            f'MAIN {{ INPUTS {{ {enum_input}{request}[span]; }} '
            f'OUTPUTS {{ {grant}[span]; }}\n'
            f'GUARANTEES {{ &&[0 <= i < span] '
            f'{formula.format(r=request, g=grant)}; }} }}\n')


def run(binary: Path, args: list[str], *, budget: float = 15,
        fault: str | None = None) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    env["ACACIA_OUTER_DEADLINE_MONOTONIC"] = str(time.monotonic() + budget)
    if fault:
        env["ACACIA_NATIVE_TEST_LIFT_FAULT"] = fault
    else:
        env.pop("ACACIA_NATIVE_TEST_LIFT_FAULT", None)
    process = subprocess.Popen([str(binary), *args], stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True, env=env,
                               start_new_session=True)
    stdout, stderr = process.communicate(timeout=budget + 3)
    try:
        os.killpg(process.pid, 0)
    except ProcessLookupError:
        pass
    else:
        raise AssertionError(f"child group survived: {args}")
    return subprocess.CompletedProcess(process.args, process.returncode, stdout, stderr)


def expect(binary: Path, args: list[str], code: int, marker: str,
           **kwargs: object) -> subprocess.CompletedProcess[str]:
    result = run(binary, args, **kwargs)
    assert result.returncode == code and marker in result.stdout + result.stderr, (
        args, code, marker, result)
    return result


def check_deadline_reap(binary: Path, source_path: Path, spot_hook: Path) -> None:
    env = os.environ.copy()
    env["ACACIA_OUTER_DEADLINE_MONOTONIC"] = str(time.monotonic() + 4)
    env["LD_PRELOAD"] = str(spot_hook)
    read_fd, write_fd = os.pipe()
    env["ACACIA_NATIVE_TEST_SPOT_FD"] = str(write_fd)
    try:
        process = subprocess.Popen([str(binary), "-T", str(source_path), "--arms", ARM],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   text=True, env=env, start_new_session=True,
                                   pass_fds=(write_fd,))
        os.close(write_fd)
        write_fd = -1
        ready, _, _ = select.select([read_fd], [], [], 3)
        assert ready, "lifting child never entered Spot translation"
        child_pid = int(os.read(read_fd, 32).strip())
        assert child_pid != process.pid and Path(f"/proc/{child_pid}").exists()
        stdout, stderr = process.communicate(timeout=6)
        assert process.returncode == 2 and '"stage":"deadline"' in stderr, (
            child_pid, process.returncode, stdout, stderr)
        assert not Path(f"/proc/{child_pid}").exists(), f"unreaped lifting child {child_pid}"
        try:
            os.killpg(child_pid, 0)
        except ProcessLookupError:
            pass
        else:
            raise AssertionError(f"surviving lifting child group {child_pid}")
    finally:
        os.close(read_fd)
        if write_fd >= 0:
            os.close(write_fd)
        if "process" in locals() and process.poll() is None:
            os.killpg(process.pid, 9)
            process.communicate(timeout=3)


def main() -> None:
    binary = Path(sys.argv[1]).resolve()
    build_dir = Path(sys.argv[2]).resolve()
    with tempfile.TemporaryDirectory(dir=build_dir) as location:
        directory = Path(location)
        paths = []
        for index, formula in enumerate(FORMULAS):
            path = directory / f"family-{index}.tlsf"
            path.write_text(source(formula, "controllable_req", "uncontrollable_grant"))
            paths.append(path)
            expect(binary, ["-T", str(path), "--arms", ARM], 0, "REALIZABLE")
            twin, _ = obfuscator.write_obfuscated(path, directory / f"twin-{index}",
                                                   0x461 + index)
            expect(binary, ["--arms", ARM, "-T", str(twin)], 0, "REALIZABLE")

        plain = directory / "plain.tlsf"
        plain.write_text('INFO { TITLE: "plain" SEMANTICS: Mealy TARGET: Mealy }\n'
                         'MAIN { INPUTS { a; } OUTPUTS { b; } GUARANTEES { G F b; } }\n')
        started = time.monotonic()
        declined = expect(binary, ["-T", str(plain), "--arms", ARM],
                          2, '"stage":"parameters"')
        assert time.monotonic() - started < 2, declined
        diagnostic = json.loads(declined.stderr.splitlines()[0])
        assert diagnostic["arm"] == ARM and diagnostic["message"] == "absent"

        width = directory / "encoded_width.tlsf"
        width.write_text('INFO { TITLE: "width" SEMANTICS: Mealy TARGET: Mealy }\n'
                         'GLOBAL { PARAMETERS { span = 8; } DEFINITIONS { '
                         'bits(x) = x <= 1 : 1 otherwise : 1 + bits(x / 2); } }\n'
                         'MAIN { INPUTS { request[bits(span)]; } '
                         'OUTPUTS { selector[bits(span)]; } '
                         'GUARANTEES { G F selector[0]; } }\n')
        expect(binary, ["-T", str(width), "--arms", ARM],
               2, '"stage":"seed_window"')

        # A legacy sibling may win, but a standalone declined lifting child
        # never calls that sibling or direct GR(1) internally.
        expect(binary, ["-T", str(paths[0]), "--arms",
                        f"{ARM},unreal:formula:backward"], 0, "REALIZABLE")
        expect(binary, ["-T", str(plain), "--arms",
                        f"{ARM},real:small:backward"], 0, "REALIZABLE")

        enum_bus = directory / "enum_and_prefixed.tlsf"
        enum_bus.write_text(source(FORMULAS[0], "controllable_request",
                                   "uncontrollable_output", enum=True))
        expect(binary, ["-T", str(enum_bus), "--arms", ARM], 0, "REALIZABLE")

        if "--spot-hook" in sys.argv[3:]:
            spot_hook = Path(sys.argv[sys.argv.index("--spot-hook") + 1])
            check_deadline_reap(binary, paths[0], spot_hook)

        for arms, marker in (("unreal:param-lift:oxidd", "realizability only"),
                             (f"{ARM},{ARM}", "duplicate arm"),
                             (f"{ARM}:frozen-graph", "does not accept a provider"),
                             ("real:param-lift:backward", "requires the oxidd backend")):
            expect(binary, ["-T", str(paths[0]), "--arms", arms], 3, marker)
        expect(binary, ["--arms", ARM, "-f", "G o", "-i", "i", "-o", "o"],
               3, "require -T")
        expect(binary, ["-T", str(paths[0]), "--arms", ARM,
                        "-s", str(directory / "controller")], 3, "do not support -s")
        expect(binary, ["-h"], 0, ARM)
        if "--hook-binary" in sys.argv[3:]:
            hook_binary = Path(sys.argv[sys.argv.index("--hook-binary") + 1])
            expect(hook_binary, ["-T", str(paths[0]), "--arms", ARM],
                   0, "REALIZABLE", fault="region-method")
            for fault, marker in (("source-hash", '"stage":"source"'),
                                  ("swap-certificate", '"stage":"certificate"'),
                                  ("swap-game", '"stage":"game"'),
                                  ("swapped-game-with-matching-hash",
                                   '"stage":"source_game"'),
                                  ("wrong-side", '"stage":"metadata"'),
                                  ("wrong-method", '"stage":"metadata"'),
                                  ("missing-policy-hash", '"stage":"policy"'),
                                  ("policy-hash", '"stage":"policy"'),
                                  ("unexpected-region-policy-hash", '"stage":"policy"'),
                                  ("corrupt-proof", "UNKNOWN")):
                expect(hook_binary, ["-T", str(paths[0]), "--arms", ARM],
                       2, marker, fault=fault)

    print("native parameter lifting CLI checks passed")


if __name__ == "__main__":
    main()
