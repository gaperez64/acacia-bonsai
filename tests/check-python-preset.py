#!/usr/bin/env python3
"""Check the actual Meson options of a Python build against the shipped preset."""

import argparse
import json
from pathlib import Path
import re
import shlex
import subprocess
import sys


def target_isa_flag(argument):
    """Fail closed on machine flags, allowing ABI/deployment and fixed tuning.

    An ISA denylist misses new extensions (and non-x86 targets). These exceptions
    do not enable instructions beyond the compiler's default target ISA.
    """
    if not argument.startswith("-m"):
        return False
    if argument in {"-m32", "-m64", "-mx32"}:
        return False
    if re.fullmatch(r"-mmacosx-version-min=\d+(?:\.\d+)*", argument):
        return False
    if argument.startswith("-mtune=") and argument != "-mtune=native":
        return False
    return True


def check_portable_commands(path):
    flags = set()
    for command in json.loads(path.read_text()):
        arguments = command.get("arguments")
        if arguments is None:
            arguments = shlex.split(command["command"])
        flags.update(argument for argument in arguments if target_isa_flag(argument))
    if flags:
        raise SystemExit("Portable wheel build contains target ISA flags: "
                         + ", ".join(sorted(flags)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path)
    parser.add_argument("--portable", action="store_true")
    args = parser.parse_args()
    helper = [sys.executable, str(Path(__file__).resolve().parents[1] / "scripts/acacia-config.py")]
    first = subprocess.check_output(
        [*helper, "list-group", "docker_default"], text=True,
    ).splitlines()[0]
    expected = dict(arg[2:].split("=", 1) for arg in shlex.split(subprocess.check_output(
        [*helper, "meson-args", first], text=True,
    )))
    expected["build_python"] = "true"
    expected["tlsf-tools:cpu"] = "baseline"
    if args.portable:
        expected["acacia_compiler_profile"] = "debug"
    actual = {
        option["name"]: str(option["value"]).lower() if isinstance(option["value"], bool)
        else str(option["value"])
        for option in json.loads(
            (args.build_dir / "meson-info/intro-buildoptions.json").read_text()
        )
    }
    mismatches = [f"{key}: expected {value!r}, got {actual.get(key)!r}"
                  for key, value in expected.items() if actual.get(key) != value]
    if mismatches:
        raise SystemExit("Python build differs from docker_default[0]:\n" + "\n".join(mismatches))
    if args.portable:
        check_portable_commands(args.build_dir / "compile_commands.json")
    print(f"Python build matches docker_default[0] ({len(expected)} options)")


if __name__ == "__main__":
    main()
