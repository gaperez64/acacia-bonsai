#!/usr/bin/env python3
"""Acacia compile-time configuration registry helper."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import sys
from typing import Any


ROOT = pathlib.Path(__file__).resolve().parents[1]
OPTIONS_PATH = ROOT / "config" / "acacia-options.json"
PRESETS_PATH = ROOT / "config" / "acacia-presets.json"
PRESET_METADATA_KEYS = {"description", "role"}
PRESET_ROLES = {"shipping", "reference", "sweep", "diagnostic", "legacy"}


def load_json(path: pathlib.Path) -> dict[str, Any]:
    return json.loads(path.read_text())


def load_registry() -> tuple[dict[str, Any], dict[str, Any]]:
    return load_json(OPTIONS_PATH), load_json(PRESETS_PATH)


def defaults(options: dict[str, Any]) -> dict[str, Any]:
    values = {name: option["default"] for name, option in options["options"].items()}
    for name, family in options["families"].items():
        values[name] = family["default"]
    return values


def resolve_preset_name(presets: dict[str, Any], name: str) -> str:
    if name in presets["presets"]:
        return name
    target = presets.get("aliases", {}).get(name)
    if target is None:
        raise SystemExit(f"unknown Acacia preset: {name}")
    if not isinstance(target, str) or target not in presets["presets"]:
        raise SystemExit(f"{name}: alias target is not an existing preset: {target}")
    print(f"note: Acacia preset alias {name} resolves to {target}", file=sys.stderr)
    return target


def preset_data(presets: dict[str, Any], name: str) -> dict[str, Any]:
    return presets["presets"][resolve_preset_name(presets, name)]


def tool_data(presets: dict[str, Any], name: str) -> dict[str, Any]:
    try:
        return presets.get("tool_baselines", {})[name]
    except KeyError as exc:
        raise SystemExit(f"unknown benchmark tool baseline: {name}") from exc


def normalize_preset(options: dict[str, Any], presets: dict[str, Any], name: str,
                     stack: tuple[str, ...] = ()) -> dict[str, Any]:
    name = resolve_preset_name(presets, name)
    if name in stack:
        raise SystemExit("preset inheritance cycle: " + " -> ".join(stack + (name,)))

    values = defaults(options)
    data = preset_data(presets, name)
    parent = data.get("inherits")
    if parent:
        values.update(normalize_preset(options, presets, parent, stack + (name,)))

    for key, value in data.items():
        if key != "inherits" and key not in PRESET_METADATA_KEYS:
            values[key] = value

    values["_preset"] = name
    return values


def validate_preset(options: dict[str, Any], presets: dict[str, Any], name: str) -> None:
    values = normalize_preset(options, presets, name)
    valid_keys = set(options["options"]) | set(options["families"]) | {"_preset"}
    for key in values:
        if key not in valid_keys:
            raise SystemExit(f"{name}: unknown option {key}")

    for family_name, family in options["families"].items():
        value = values[family_name]
        if value not in family["choices"]:
            raise SystemExit(f"{name}: invalid {family_name}={value}")

    for option_name, option in options["options"].items():
        value = values[option_name]
        if option["type"] == "combo" and value not in option["choices"]:
            raise SystemExit(f"{name}: invalid {option_name}={value}")

    if values["actioner"] == "no_ios_precomputation" and values["ios_precomputer"] != "delegate":
        raise SystemExit(f"{name}: no_ios_precomputation requires delegate ios_precomputer")


def validate_preset_metadata(presets: dict[str, Any], name: str) -> None:
    data = presets["presets"][name]
    description = data.get("description")
    if (not isinstance(description, str) or not description.strip()
            or description.splitlines() != [description]):
        raise SystemExit(f"{name}: description must be a non-empty single line")
    role = data.get("role")
    if not isinstance(role, str) or role not in PRESET_ROLES:
        raise SystemExit(f"{name}: role must be one of {', '.join(sorted(PRESET_ROLES))}")


def validate_naming(presets: dict[str, Any]) -> None:
    naming = presets.get("naming", {})
    if not isinstance(naming, dict):
        raise SystemExit("naming must be an object")
    max_length = naming.get("max_length")
    if type(max_length) is not int or max_length < 1:
        raise SystemExit("naming.max_length must be a positive integer")
    for key in ("forbidden_tokens", "grandfathered"):
        entries = naming.get(key)
        if not isinstance(entries, list) or not all(
            isinstance(entry, str) and entry for entry in entries
        ):
            raise SystemExit(f"naming.{key} must be a list of non-empty strings")

    grandfathered = set(naming["grandfathered"])
    forbidden = {token.lower() for token in naming["forbidden_tokens"]}
    for name in presets["presets"]:
        if name in grandfathered:
            continue
        # Tokens are case-insensitive words separated by punctuation (including
        # underscores and hyphens); an unrelated word such as 'bestow' is fine.
        tokens = set(re.findall(r"[a-z0-9]+", name.lower()))
        reasons = []
        if len(name) > max_length:
            reasons.append(f"length {len(name)} exceeds max_length {max_length}")
        if tokens & forbidden:
            reasons.append("forbidden token(s): " + ", ".join(sorted(tokens & forbidden)))
        if reasons:
            raise SystemExit(
                f"{name}: invalid new preset name ({'; '.join(reasons)}). "
                "Keep identifiers short; describe mechanisms and purpose in metadata. "
                "A superlative in an identifier is a claim no test can keep honest; "
                "groups (such as docker_default) are where 'what we currently ship' "
                "belongs, because a group can be repointed."
            )


def validate_aliases(presets: dict[str, Any]) -> None:
    aliases = presets.get("aliases", {})
    if not isinstance(aliases, dict):
        raise SystemExit("aliases must be an object")
    for alias, target in aliases.items():
        if not isinstance(alias, str) or not alias:
            raise SystemExit("alias names must be non-empty strings")
        if alias in presets["presets"]:
            raise SystemExit(f"{alias}: alias collides with a real preset name")
        if not isinstance(target, str) or target not in presets["presets"]:
            raise SystemExit(f"{alias}: alias target is not an existing preset: {target}")


def validate_tool(presets: dict[str, Any], name: str) -> None:
    data = tool_data(presets, name)
    valid_keys = {"description", "suite_prefix", "env"}
    for key in data:
        if key not in valid_keys:
            raise SystemExit(f"{name}: unknown tool-baseline key {key}")
    env = data.get("env", {})
    if not isinstance(env, dict):
        raise SystemExit(f"{name}: tool-baseline env must be an object")
    for key, value in env.items():
        if not isinstance(key, str) or not key:
            raise SystemExit(f"{name}: tool-baseline env keys must be non-empty strings")
        if not isinstance(value, str):
            raise SystemExit(f"{name}: tool-baseline env values must be strings")


def normalized_json(values: dict[str, Any]) -> str:
    return json.dumps(values, sort_keys=True, separators=(",", ":"))


def stable_hash(values: dict[str, Any]) -> str:
    return hashlib.sha256(normalized_json(values).encode()).hexdigest()[:12]


def bool_literal(value: bool) -> str:
    return "true" if value else "false"


def macro_flag(macro: dict[str, Any], value: Any) -> str | None:
    """Apply a registry macro's explicit emission condition and value encoding."""
    emit = macro["emit"]
    if emit in {"true", "nonempty"}:
        if not value:
            return None
    elif emit == "not_equal":
        if value == macro["skip_value"]:
            return None
    elif emit != "always":
        raise ValueError(f"unknown macro emission condition: {emit}")

    flag = f"-D{macro['name']}"
    encoding = macro["encoding"]
    if encoding == "bare":
        return flag
    if encoding == "bool_int":
        value = int(value)
    elif encoding == "bool_literal":
        value = bool_literal(value)
    elif encoding == "map":
        value = macro["map"][value]
    elif encoding == "prefix":
        value = macro["prefix"] + value
    elif encoding == "constant":
        value = macro["value"]
    elif encoding == "c_string":
        # Preserve the historical shell-escaped quotes without altering the value.
        value = f'\\"{value}\\"'
    elif encoding != "value":
        raise ValueError(f"unknown macro encoding: {encoding}")
    return f"{flag}={value}"


def preprocessor_flags(options: dict[str, Any], values: dict[str, Any]) -> list[str]:
    # Registry order is emission order.  Which -D comes first is not semantic --
    # no macro is defined twice -- so the registry does not carry a second,
    # hand-maintained ordering for a new option to have to slot into.
    flags: list[str] = []
    for name, option in options["options"].items():
        for macro in option["macros"]:
            flag = macro_flag(macro, values[name])
            if flag is not None:
                flags.append(flag)

    for family_name, family in options["families"].items():
        selected = values[family_name]
        selected_data = family["choices"][selected]
        flags.append(f"-D{family['macro']}={selected_data['type']}")
        for choice_name, choice_data in family["choices"].items():
            flags.append(f"-D{choice_data['gate']}={int(choice_name == selected)}")

    return flags


def meson_args(options: dict[str, Any], values: dict[str, Any]) -> list[str]:
    # Every Meson option name comes from the registry, families included; none
    # is derived from the option key by convention.
    names = {name: option["meson"] for name, option in options["options"].items()}
    names.update((name, family["meson"]) for name, family in options["families"].items())
    args: list[str] = []
    for key, meson_name in names.items():
        value = values[key]
        if isinstance(value, bool):
            value = bool_literal(value)
        args.append(f"-D{meson_name}={value}")
    return args


def emit_config_header(options: dict[str, Any], values: dict[str, Any], path: pathlib.Path) -> None:
    lines = ["#pragma once", ""]
    for flag in preprocessor_flags(options, values):
        if not flag.startswith("-D"):
            continue
        define = flag[2:]
        if "=" in define:
            key, value = define.split("=", 1)
            lines.append(f"#ifndef {key}")
            lines.append(f"# define {key} {value}")
            lines.append("#endif")
        else:
            lines.append(f"#ifndef {define}")
            lines.append(f"# define {define} 1")
            lines.append("#endif")
        lines.append("")
    path.write_text("\n".join(lines))


def validate_constants(options: dict[str, Any]) -> None:
    """Check documentation entries without treating constants as options."""
    constants = options.get("constants")
    if not isinstance(constants, dict):
        raise SystemExit("constants must be an object")
    for name, constant in constants.items():
        if not isinstance(name, str) or not name or not isinstance(constant, dict):
            raise SystemExit("constants must contain named objects")
        if type(constant.get("value")) not in {str, int}:
            raise SystemExit(f"{name}: constant value must be a string or integer")
        description = constant.get("description")
        if not isinstance(description, str) or not description:
            raise SystemExit(f"{name}: constant description must be a non-empty string")
        sources = constant.get("sources")
        if not isinstance(sources, list) or not sources or not all(
            isinstance(source, str) and source for source in sources
        ):
            raise SystemExit(f"{name}: constant sources must be non-empty strings")


def command_validate(options: dict[str, Any], presets: dict[str, Any]) -> None:
    validate_constants(options)
    validate_naming(presets)
    validate_aliases(presets)
    for name in presets["presets"]:
        validate_preset_metadata(presets, name)
        validate_preset(options, presets, name)
    for name in presets.get("tool_baselines", {}):
        validate_tool(presets, name)
    for group_name, group in presets.get("groups", {}).items():
        for name in group:
            if name not in presets["presets"] and name not in presets.get("aliases", {}):
                raise SystemExit(f"{group_name}: unknown preset {name}")
    shipping = {
        resolve_preset_name(presets, name)
        for name in presets.get("groups", {}).get("docker_default", [])
    }
    for name, data in presets["presets"].items():
        if (data["role"] == "shipping") != (name in shipping):
            raise SystemExit(f"{name}: role shipping must match docker_default membership")


def tool_env_lines(data: dict[str, Any]) -> list[str]:
    return [f"{key}={value}" for key, value in sorted(data.get("env", {}).items())]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="cmd", required=True)
    sub.add_parser("validate")
    list_p = sub.add_parser("list-presets")
    list_p.add_argument("--long", action="store_true", help="include role and description")
    sub.add_parser("list-tools")
    group_p = sub.add_parser("list-group")
    group_p.add_argument("group")
    show_p = sub.add_parser("show")
    show_p.add_argument("preset")
    show_p.add_argument("--describe", action="store_true",
                        help="wrap options with sibling role and description metadata")
    show_tool_p = sub.add_parser("show-tool")
    show_tool_p.add_argument("tool")
    tool_desc_p = sub.add_parser("tool-description")
    tool_desc_p.add_argument("tool")
    tool_prefix_p = sub.add_parser("tool-suite-prefix")
    tool_prefix_p.add_argument("tool")
    tool_env_p = sub.add_parser("tool-env")
    tool_env_p.add_argument("tool")
    hash_p = sub.add_parser("hash")
    hash_p.add_argument("preset")
    meson_p = sub.add_parser("meson-args")
    meson_p.add_argument("preset")
    emit_p = sub.add_parser("emit-config-header")
    emit_p.add_argument("preset")
    emit_p.add_argument("path", type=pathlib.Path)
    args = parser.parse_args()

    options, presets = load_registry()

    if args.cmd == "validate":
        command_validate(options, presets)
        return 0
    if args.cmd == "list-presets":
        names = sorted(presets["presets"])
        if args.long:
            for name in names:
                validate_preset_metadata(presets, name)
            name_width = max(map(len, names), default=0)
            role_width = max((len(presets["presets"][name]["role"]) for name in names),
                             default=0)
            for name in names:
                data = presets["presets"][name]
                print(f"{name:<{name_width}}  {data['role']:<{role_width}}  {data['description']}")
        else:
            for name in names:
                print(name)
        return 0
    if args.cmd == "list-tools":
        for name in sorted(presets.get("tool_baselines", {})):
            validate_tool(presets, name)
            print(name)
        return 0
    if args.cmd == "list-group":
        try:
            group = presets["groups"][args.group]
        except KeyError as exc:
            raise SystemExit(f"unknown preset group: {args.group}") from exc
        for name in group:
            validate_preset(options, presets, name)
            print(name)
        return 0
    if args.cmd in {"show-tool", "tool-description", "tool-suite-prefix", "tool-env"}:
        validate_tool(presets, args.tool)
        data = tool_data(presets, args.tool)
        if args.cmd == "show-tool":
            print(json.dumps(data, sort_keys=True, indent=2))
        elif args.cmd == "tool-description":
            print(data.get("description", "external benchmark backend"))
        elif args.cmd == "tool-suite-prefix":
            print(data.get("suite_prefix", args.tool))
        elif args.cmd == "tool-env":
            for line in tool_env_lines(data):
                print(line)
        return 0

    name = resolve_preset_name(presets, args.preset)
    validate_preset(options, presets, name)
    values = normalize_preset(options, presets, name)
    if args.cmd == "show":
        if args.describe:
            validate_preset_metadata(presets, name)
            data = preset_data(presets, name)
            values = {"description": data["description"], "role": data["role"],
                      "options": values}
        print(json.dumps(values, sort_keys=True, indent=2))
    elif args.cmd == "hash":
        print(stable_hash(values))
    elif args.cmd == "meson-args":
        print(" ".join(meson_args(options, values)))
    elif args.cmd == "emit-config-header":
        emit_config_header(options, values, args.path)
    else:
        parser.error(f"unknown command {args.cmd}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
