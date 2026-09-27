#!/usr/bin/env python3
"""Alpha-rename declared TLSF signals and give the result a seeded random name."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import random
import re

IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z_0-9]*")
TOKEN = re.compile(r"//[^\n]*|/\*[\s\S]*?\*/|\"(?:\\.|[^\"\\])*\"|[A-Za-z_][A-Za-z_0-9]*|[^A-Za-z_]", re.MULTILINE)


def lex(text: str) -> list[tuple[str, int, int]]:
    return [(match.group(), match.start(), match.end()) for match in TOKEN.finditer(text)
            if not match.group().isspace() and not match.group().startswith(("//", "/*"))]


def declaration_name(declaration: list[str]) -> str | None:
    """Return the signal from `name`, `name[...]`, or `enum_type name`."""
    identifiers = [part for part in declaration[:2] if IDENTIFIER.fullmatch(part)]
    return identifiers[1] if len(identifiers) == 2 else (
        identifiers[0] if identifiers else None)


def signal_names(text: str) -> list[str]:
    tokens = lex(text)
    names: list[str] = []
    for index, (value, _, _) in enumerate(tokens):
        if value.upper() not in {"INPUTS", "OUTPUTS"} or index + 1 >= len(tokens):
            continue
        if tokens[index + 1][0] != "{":
            continue
        depth = 1
        declaration: list[str] = []
        for token, _, _ in tokens[index + 2:]:
            if token == "{":
                depth += 1
            elif token == "}":
                depth -= 1
                if depth == 0:
                    name = declaration_name(declaration)
                    if name is not None and name not in names:
                        names.append(name)
                    break
            if depth == 1:
                if token == ";":
                    name = declaration_name(declaration)
                    if name is not None and name not in names:
                        names.append(name)
                    declaration = []
                else:
                    declaration.append(token)
    return names


def obfuscate(source: bytes, seed: int) -> tuple[bytes, dict[str, str]]:
    text = source.decode("utf-8")
    names = signal_names(text)
    rng = random.Random(seed)
    tokens = lex(text)
    used = {token for token, _, _ in tokens if IDENTIFIER.fullmatch(token)}
    mapping: dict[str, str] = {}
    for name in names:
        while True:
            candidate = "sig_" + format(rng.getrandbits(72), "018x")
            if candidate not in used:
                break
        mapping[name] = candidate
        used.add(candidate)
    pieces: list[str] = []
    cursor = 0
    for token, start, end in tokens:
        if token in mapping:
            pieces.extend((text[cursor:start], mapping[token]))
            cursor = end
    pieces.append(text[cursor:])
    return "".join(pieces).encode("utf-8"), mapping


def write_obfuscated(source: pathlib.Path, output_dir: pathlib.Path, seed: int) -> tuple[pathlib.Path, pathlib.Path]:
    data = source.read_bytes()
    changed, mapping = obfuscate(data, seed)
    declared = signal_names(data.decode("utf-8"))
    if set(mapping) != set(declared) or len(mapping) != len(declared):
        raise ValueError("sidecar does not map every declared signal")
    if set(signal_names(changed.decode("utf-8"))) != set(mapping.values()):
        raise ValueError("renamed declarations disagree with sidecar")
    rng = random.Random(seed ^ int.from_bytes(hashlib.sha256(data).digest()[:8], "big"))
    basename = "case_" + format(rng.getrandbits(96), "024x")
    output_dir.mkdir(parents=True, exist_ok=True)
    target = output_dir / f"{basename}.tlsf"
    sidecar = output_dir / f"{basename}.mapping.json"
    target.write_bytes(changed)
    sidecar.write_text(json.dumps({"seed": seed, "input_sha256": hashlib.sha256(data).hexdigest(),
                                   "output_sha256": hashlib.sha256(changed).hexdigest(),
                                   "signals": mapping}, indent=2, sort_keys=True) + "\n")
    return target, sidecar


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("output_dir", type=pathlib.Path)
    parser.add_argument("--seed", type=int, default=0)
    args = parser.parse_args()
    target, sidecar = write_obfuscated(args.source, args.output_dir, args.seed)
    print(target)
    print(sidecar)


if __name__ == "__main__":
    main()
