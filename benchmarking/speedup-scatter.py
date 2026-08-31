#!/usr/bin/env python3
"""Plot per-instance wall-clock time of one solver against another.

A cactus plot compares two configurations by their sorted running times, which
hides what happened to any individual instance.  When a change wins big on a
few instances and is neutral elsewhere -- the usual shape for a heuristic that
either fires or does not -- the two cactus curves overlap and the plot says
almost nothing.  This scatter says it directly: every instance is one point,
points below the diagonal are faster, points above are slower, and an instance
only one side solved is drawn on the cap line so a gained answer cannot be
mistaken for a fast one.
"""
import argparse
import csv
import pathlib

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

SOLVED = {"REALIZABLE", "UNREALIZABLE"}


def load(path):
    with open(path, newline="") as handle:
        return {
            row["instance"]: (row["result"], float(row["seconds"]))
            for row in csv.DictReader(handle)
        }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", required=True, type=pathlib.Path)
    parser.add_argument("--candidate", required=True, type=pathlib.Path)
    parser.add_argument("--baseline-label", default="baseline")
    parser.add_argument("--candidate-label", default="candidate")
    parser.add_argument("--title", required=True)
    parser.add_argument("--timeout", type=float, default=17.0)
    parser.add_argument("--out-prefix", required=True, type=pathlib.Path)
    args = parser.parse_args(argv)

    base, cand = load(args.baseline), load(args.candidate)
    both, gained, lost, floor = [], [], [], 1e-3

    for instance in sorted(base):
        if instance not in cand:
            continue
        (bv, bs), (cv, cs) = base[instance], cand[instance]
        bs, cs = max(bs, floor), max(cs, floor)
        b_ok, c_ok = bv in SOLVED, cv in SOLVED
        if b_ok and c_ok:
            both.append((bs, cs))
        elif c_ok and not b_ok:
            gained.append((args.timeout, cs, instance))
        elif b_ok and not c_ok:
            lost.append((bs, args.timeout, instance))

    fig, ax = plt.subplots(figsize=(7.2, 7.0))
    lo, hi = floor, args.timeout * 1.8
    ax.plot([lo, hi], [lo, hi], color="0.4", lw=1, zorder=1)
    for factor, style in ((2.0, ":"), (10.0, "--")):
        ax.plot([lo, hi], [lo / factor, hi / factor], color="0.75", lw=0.8,
                ls=style, zorder=1)
    ax.axvline(args.timeout, color="0.85", lw=0.8, zorder=0)
    ax.axhline(args.timeout, color="0.85", lw=0.8, zorder=0)

    if both:
        ax.scatter(*zip(*both), s=18, alpha=0.55, color="#40004b",
                   edgecolors="none", label=f"solved by both ({len(both)})",
                   zorder=2)
    if gained:
        ax.scatter([g[0] for g in gained], [g[1] for g in gained], s=95,
                   marker="*", color="#1a9850", edgecolors="black",
                   linewidths=0.5, zorder=4,
                   label=f"gained: only {args.candidate_label} solves ({len(gained)})")
        for x, y, name in gained:
            ax.annotate(name.removesuffix(".ltl"), (x, y),
                        textcoords="offset points", xytext=(-8, 8),
                        ha="right", fontsize=8, color="#1a9850")
    if lost:
        ax.scatter([l[0] for l in lost], [l[1] for l in lost], s=95,
                   marker="X", color="#d73027", edgecolors="black",
                   linewidths=0.5, zorder=4,
                   label=f"lost: only {args.baseline_label} solves ({len(lost)})")
        for x, y, name in lost:
            ax.annotate(name.removesuffix(".ltl"), (x, y),
                        textcoords="offset points", xytext=(8, -12),
                        ha="left", fontsize=8, color="#d73027")

    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlim(lo, hi)
    ax.set_ylim(lo, hi)
    ax.set_xlabel(f"{args.baseline_label} wall-clock seconds "
                  f"(timeouts on the {args.timeout:g} s line)")
    ax.set_ylabel(f"{args.candidate_label} wall-clock seconds "
                  f"(timeouts on the {args.timeout:g} s line)")
    ax.set_title(args.title)
    ax.grid(True, which="both", ls=":", lw=0.4, alpha=0.5)
    ax.text(0.03, 0.95, "below the diagonal = faster", transform=ax.transAxes,
            fontsize=9, color="0.3", va="top")
    ax.legend(loc="lower right", fontsize=9, framealpha=0.95)
    fig.tight_layout()

    args.out_prefix.parent.mkdir(parents=True, exist_ok=True)
    for suffix in ("png", "pdf"):
        target = args.out_prefix.with_suffix(f".{suffix}")
        fig.savefig(target, dpi=140)
        print(f"wrote {target}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
