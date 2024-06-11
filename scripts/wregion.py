import aiger
from aiger_dfa import aig2dfa
import argparse
from dfa.draw import write_dot


def mealy(fname, outname):
    aig = aiger.load(fname)
    # aig2dfa dislikes outputs not named "output"
    aig = aig.relabel("output", {"_ab_single_output": "output"})
    dfa = aig2dfa(aig)
    write_dot(dfa, outname)
    return 0


parser = argparse.ArgumentParser(description="Process winning region")
parser.add_argument("filename", type=str,
                    help="name of file with winning region circuit")
parser.add_argument("output", type=str,
                    help="dot file to output depiction of region")
if __name__ == "__main__":
    args = parser.parse_args()
    exit(mealy(args.filename, args.output))
