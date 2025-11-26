#! /usr/bin/env python3

import argparse
import glob
from pathlib import Path


def main():
    """
        Utility to retrieve a list of files that match a pattern.
    """
    parser = argparse.ArgumentParser(
        prog="Glob utility",
        description="Utility to retrieve a list of files that match a pattern."
    )

    parser.add_argument("--directory", type=str, required=True,
                        help="Directory with ltl and part files.")

    args = parser.parse_args()

    directory = Path(args.directory)

    ltl_files = sorted(directory.glob("*.ltl"))
    part_files = sorted(directory.glob("*.part"))

    # we are going to match all files pairwise and feed the contents to Acacia, so the files need to correspond
    #   when they are traversed in sorted order.
    for ltl_file, part_file in zip(ltl_files, part_files):
        assert ltl_file.stem == part_file.stem, \
            ("Error: all files in the directory need to pair-wise have the name basename. "
             f"Instead got '{ltl_file}' and '{part_file}'")

    ltl_formulas = []
    inputs = []
    outputs = []

    for file in ltl_files:
        with open(file, "r") as in_file:
            contents = in_file.read().strip()
            ltl_formulas.append(contents)

    for file in part_files:
        with open(file, "r") as in_file:
            for line in in_file:
                line = line.strip()
                if line.startswith(".inputs"):
                    line = line.replace(".inputs ", "")
                    line = ",".join(line.split(" "))
                    inputs.append(line)
                elif line.startswith(".outputs"):
                    line = line.replace(".outputs ", "")
                    line = ",".join(line.split(" "))
                    outputs.append(line)
                else:
                    raise Exception(f"Error: invalid line '{line}'.")

    for circ_in, circ_out, ltl_formula in zip(inputs, outputs, ltl_formulas):
        # NOTE: some values contain spaces!
        print(f"-i \"{circ_in}\" -o \"{circ_out}\" -f \"{ltl_formula}\"")


if __name__ == "__main__":
    main()
