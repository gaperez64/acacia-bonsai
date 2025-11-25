#! /usr/bin/env python3

import argparse
from pathlib import Path


def main():
    """
        Utility to take a file of the format:

        > .inputs x y z
        > .outputs a b c

        And produce two files with contents
        > x,y,z
        and
        > a,b,c
    """
    parser = argparse.ArgumentParser(
        prog="IO Extractor",
        description="Extract the argument values for -i and -o from the specified file."
    )

    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--ex_i", action='store_true', help="Extract inputs")
    group.add_argument("--ex_o", action='store_true', help="Extract outputs")

    parser.add_argument("--input_file", type=str, required=True,
                        help="The file that contains the '.inputs' and '.outputs' lines.")
    parser.add_argument("--output_file", type=str, required=True,
                        help="The file that will contain a comma-separated list of inputs or outputs.")

    args = parser.parse_args()

    contents = None

    # TODO: make this two files in one.
    io_file_path = Path(args.file)
    with open(io_file_path, "r") as in_file:
        for line in in_file:
            if line.startswith(".inputs") and args.ex_i:
                line = line.strip()
                line = line.replace(".inputs ", "").strip()
                contents = line
                break
            elif line.startswith(".outputs") and args.ex_o:
                line = line.strip()
                line = line.replace(".outputs ", "").strip()
                contents = line
                break

    if not contents:
        print(f"Error: did not find the correct inputs/outputs in the file '{io_file_path}'.")
        exit(1)

    # we need comma-separated inputs or outputs
    contents = ",".join(contents.split(" "))

    output_file_path = Path(args.output_file)
    with open(output_file_path, "w") as output_file:
        output_file.write(contents)


if __name__ == "__main__":
    main()
