#!/usr/bin/env python3
"""Prepare declaration-only controls for the existing coverage screen driver."""

import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import shlex


TOKEN = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|[A-Za-z_]\w*|[^\s]', re.S)


def shuffle_declarations(source):
    """Reverse whole declaration statements; preserve every other source byte."""
    tokens = [(match[0], match.start(), match.end()) for match in TOKEN.finditer(source)
              if not match[0].startswith(("//", "/*", '"'))]
    replacements = []
    for i, (token, _, _) in enumerate(tokens):
        if token not in ("INPUTS", "OUTPUTS"):
            continue
        if i + 1 == len(tokens) or tokens[i + 1][0] != "{":
            raise ValueError(f"missing declaration block after {token}")
        start = tokens[i + 1][2]
        statement_start = start
        statements = []
        depth = 1
        last_content_end = start
        trailing_semicolon = False
        for next_token, begin, end in tokens[i + 2:]:
            if next_token == "{":
                depth += 1
            elif next_token == "}":
                depth -= 1
                if not depth:
                    # TLSF permits the last declaration without a semicolon.
                    # Retain the original separator count and all trivia.
                    if last_content_end > statement_start:
                        statements.append(source[statement_start:last_content_end])
                        tail = source[last_content_end:begin]
                    else:
                        tail = source[statement_start:begin]
                    replacement = ";".join(reversed(statements))
                    if trailing_semicolon:
                        replacement += ";"
                    replacements.append((start, begin, replacement + tail))
                    break
            elif next_token == ";" and depth == 1:
                statements.append(source[statement_start:begin])
                statement_start = end
                last_content_end = end
                trailing_semicolon = True
                continue
            last_content_end = end
            trailing_semicolon = False
        else:
            raise ValueError("unterminated declaration block")
    if not replacements:
        raise ValueError("no IO declaration blocks")
    for start, end, replacement in reversed(replacements):
        source = source[:start] + replacement + source[end:]
    return source


def digest(data):
    return hashlib.sha256(data).hexdigest()


def prepare(args):
    rows = list(csv.DictReader(args.targets.open(), delimiter="\t"))
    selected = [row for row in rows if row["package"] == "P2a"]
    instances = sorted({name for row in selected for name in json.loads(row["cases_json"])})
    if not instances:
        raise ValueError("no P2a targets")
    mapping = {row["instance"]: row["tlsf"] for row in
               csv.DictReader(args.source_map.open(), delimiter="\t")}
    args.output.mkdir(parents=True, exist_ok=True)
    corpus = args.output / "declaration-shuffled"
    corpus.mkdir(exist_ok=True)
    records = []
    for instance in instances:
        name = mapping[instance]
        if Path(name).name != name:
            raise ValueError("source map must use flat TLSF filenames")
        original = args.corpus / name
        source = original.read_bytes()
        shuffled = shuffle_declarations(source.decode()).encode()
        control = corpus / name
        control.write_bytes(shuffled)
        records.append({"instance": instance, "original": str(original.resolve()),
                        "control": str(control.resolve()), "original_sha256": digest(source),
                        "control_sha256": digest(shuffled), "changed": shuffled != source})
    (args.output / "targets.list").write_text("".join(name + "\n" for name in instances))
    (args.output / "controls.json").write_text(json.dumps(records, indent=2) + "\n")
    # screen.py has a fixed source map/corpus. This exec-only adapter substitutes
    # the prepared twin for the current -T argument, after verifying both hashes.
    # It adds no solver flags, process, deadline, or worker membership changes.
    wrapper = args.output / "shuffled-acacia"
    wrapper.write_text(f'''#!{args.python.resolve()}
import hashlib
import json
import os
from pathlib import Path
import sys
records = json.loads(Path({str((args.output / "controls.json").resolve())!r}).read_text())
args = sys.argv[1:]
at = args.index("-T") + 1
source = Path(args[at]).resolve()
record = next(row for row in records if row["original"] == str(source))
control = Path(record["control"])
assert hashlib.sha256(source.read_bytes()).hexdigest() == record["original_sha256"]
assert hashlib.sha256(control.read_bytes()).hexdigest() == record["control_sha256"]
args[at] = str(control)
os.execv({str(args.binary.resolve())!r}, [{str(args.binary.resolve())!r}, *args])
''')
    wrapper.chmod(0o755)
    sha = args.acacia_sha
    binary = str(args.binary.resolve())
    treatments = [[order, binary, sha, f"--var-order {order}"] for order in
                  ("incumbent", "typed-interleaved", "role-grouped")]
    shuffled_treatments = [[order, str(wrapper.resolve()), sha, flags]
                          for order, _, sha, flags in treatments]
    for name, value in (("treatments.json", treatments),
                        ("shuffled-treatments.json", shuffled_treatments)):
        (args.output / name).write_text(json.dumps(value, indent=2) + "\n")
    screen = args.screen.resolve()
    commands = []
    for cap in (17, 60):
        for label, treatment_file in (("targets", "treatments.json"),
                                      ("shuffled", "shuffled-treatments.json")):
            # JSON is a single shell-quoted argument, never evaluated as code.
            values = json.loads((args.output / treatment_file).read_text())
            command = [str(args.python.resolve()), str(screen),
                       str((args.output / f"screen-{label}-{cap}").resolve()),
                       str((args.output / "targets.list").resolve()), str(cap), json.dumps(values)]
            commands.append(shlex.join(command))
    (args.output / "screen-commands.sh").write_text("#!/bin/sh\nset -eu\n" + "\n".join(commands) + "\n")
    manifest = {"targets_sha256": digest(args.targets.read_bytes()),
                "source_map_sha256": digest(args.source_map.read_bytes()),
                "screen_sha256": digest(screen.read_bytes()), "binary": binary,
                "binary_sha256": digest(args.binary.read_bytes()),
                "wrapper_sha256": digest(wrapper.read_bytes()), "acacia_sha": sha,
                "control": "reverse whole IO declarations; formulas and all other bytes preserved",
                "instances": len(instances), "changed": sum(row["changed"] for row in records)}
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Prepared {len(instances)} controls ({manifest['changed']} changed); {args.output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--targets", required=True, type=Path)
    parser.add_argument("--source-map", required=True, type=Path)
    parser.add_argument("--corpus", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--acacia-sha", required=True)
    parser.add_argument("--python", required=True, type=Path)
    parser.add_argument("--screen", required=True, type=Path)
    prepare(parser.parse_args())


if __name__ == "__main__":
    main()
