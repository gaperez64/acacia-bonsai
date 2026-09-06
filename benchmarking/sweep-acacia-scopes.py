#!/usr/bin/env python3
"""Check for benchmark contention or clean up surviving Acacia user scopes."""

import argparse
import pathlib

from benchlib import check_campaign_scopes, sweep_acacia_scopes, _clean_acacia_scope


def read_snapshot(path: pathlib.Path | None) -> set[str]:
    if path is None:
        return set()
    try:
        return {line.strip() for line in path.read_text().splitlines() if line.strip()}
    except OSError:
        return set()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true",
                     help="report running scopes and exit nonzero; reset failed residue")
    mode.add_argument("--stop", action="store_true",
                     help="stop running scopes and reset failed residue, reporting each action")
    parser.add_argument(
        "--snapshot", type=pathlib.Path,
        help="with --check, record the scopes running now; with --stop, spare exactly those. "
             "A campaign asks whether anything IT started outlived it, so scopes that were "
             "already there -- another campaign's -- must not be stopped on its way out.",
    )
    args = parser.parse_args(argv)

    scopes = sweep_acacia_scopes()
    if args.check:
        if args.snapshot is not None:
            running = [scope.unit for scope in scopes if scope.state == "running"]
            args.snapshot.write_text("".join(f"{unit}\n" for unit in running))
        return 0 if check_campaign_scopes("scope check", scopes=scopes) else 1

    inherited = read_snapshot(args.snapshot)
    cleaned = 0
    for scope in scopes:
        if scope.unit in inherited:
            continue
        _clean_acacia_scope(scope, "scope sweep")
        if scope.state == "running":
            cleaned += 1
    if cleaned:
        import sys
        print(f"scope sweep: cleaned up {cleaned} scope(s) that outlived this campaign",
              file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
