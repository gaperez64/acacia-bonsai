#!/usr/bin/env python3
"""Assert the build's configuration frontends agree on every default.

Acacia has three places that state what an option defaults to:

  meson.options                 read by `meson setup`
  config/acacia-options.json    read by scripts/acacia-config.py
  src/configuration.hh          the #ifndef fallbacks

meson.build always generates acacia_build_config.hh (meson.build:408), so the
configuration.hh fallbacks never fire for a configured build; the two that can
actually disagree in a shipped binary are the first two.  They diverged once
already: meson.options defaulted acacia_local_certificate to true while the JSON
and configuration.hh said false, so a plain `meson setup build` silently produced
the S reference configuration while claiming to be B.  Nothing caught it because
scripts/acacia-config.py passes every option explicitly, which hides the
divergence from exactly the path that is used for measurement.

This test makes that class of drift fail loudly.  A divergence is allowed only if
it is listed in INTENTIONAL_DIVERGENCE with a reason, which forces the question
"is this a capability difference or a behaviour difference?" to be answered in
writing rather than discovered in a benchmark.
"""

from __future__ import annotations

import json
import pathlib
import re
import sys

# option name -> why the two frontends deliberately differ.
#
# The bar for an entry here: the divergence must change what the binary CAN DO,
# not how it SOLVES, and it must fail loudly when it matters.  A solver-behaviour
# option must never appear in this list -- that divergence is silent by nature,
# which is what made the local_certificate one survive as long as it did.
INTENTIONAL_DIVERGENCE = {
    "acacia_enable_tlsf_frontend":
        "meson=false keeps a plain `meson setup` free of the flex/bison "
        "dependency -- .github/workflows/main.yml covers the frontend in a "
        "separate job that sets the option explicitly.  json=true because every "
        "binary scripts/acacia-config.py builds drives the benchmark harness "
        "through -T.  A binary without it rejects -T outright, so the mismatch "
        "surfaces immediately instead of changing an answer.",
}


def meson_defaults(path: pathlib.Path) -> dict[str, object]:
    """Parse `option(...)` defaults out of meson.options."""
    text = path.read_text()
    out: dict[str, object] = {}
    for match in re.finditer(r"option\s*\(\s*'([^']+)'\s*,(.*?)\n(?=option|\Z)",
                             text, re.S):
        name, body = match.group(1), match.group(2)
        typ = re.search(r"type:\s*'([^']+)'", body)
        val = re.search(r"value:\s*(\[[^\]]*\]|'[^']*'|[^,\n\)]+)", body)
        if not typ or not val:
            continue
        raw = val.group(1).strip()
        if typ.group(1) == "boolean":
            out[name] = raw == "true"
        elif typ.group(1) == "integer":
            out[name] = int(raw)
        else:
            out[name] = raw.strip("'")
    return out


def json_key_to_meson_name(registry: pathlib.Path) -> dict[str, str]:
    """Read the Meson names from the shared configuration registry."""
    options = json.loads(registry.read_text())
    mapping = {name: option["meson"] for name, option in options["options"].items()}
    mapping.update(
        (name, family["meson"]) for name, family in options["families"].items()
    )
    return mapping


def main() -> int:
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    meson = meson_defaults(root / "meson.options")
    registry = root / "config" / "acacia-options.json"
    options = json.loads(registry.read_text())["options"]
    scalars = {name: option["default"] for name, option in options.items()}
    mapping = json_key_to_meson_name(registry)

    checked = 0
    failures: list[str] = []
    for json_key, meson_name in sorted(mapping.items()):
        if json_key not in scalars or meson_name not in meson:
            # Component families live under "families", not "options";
            # they carry their own defaults and are compared by the family test.
            continue
        checked += 1
        j, m = scalars[json_key], meson[meson_name]
        if j == m:
            continue
        if meson_name in INTENTIONAL_DIVERGENCE:
            print(f"ok (documented divergence) {meson_name}: meson={m!r} json={j!r}")
            continue
        failures.append(
            f"{meson_name}: meson.options={m!r} but acacia-options.json={j!r}.\n"
            f"    A binary from `meson setup` and one from scripts/acacia-config.py\n"
            f"    would disagree on this option.  Pick one value and set it in both,\n"
            f"    or add the option to INTENTIONAL_DIVERGENCE with the reason.")

    # A stale entry is its own bug: it documents a divergence that no longer
    # exists, so the next real one can be waved through by an obsolete comment.
    for name in INTENTIONAL_DIVERGENCE:
        if name not in meson:
            failures.append(f"{name} is listed as an intentional divergence but "
                            f"no longer exists in meson.options; drop the entry.")

    if failures:
        print(f"\nFAIL: {len(failures)} configuration frontend disagreement(s)\n",
              file=sys.stderr)
        for f in failures:
            print(f"  {f}\n", file=sys.stderr)
        return 1

    print(f"config frontends agree on all {checked} shared scalar options "
          f"({len(INTENTIONAL_DIVERGENCE)} documented divergence(s))")
    return 0


if __name__ == "__main__":
    sys.exit(main())
