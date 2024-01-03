#!/usr/bin/env python3

import json
import matplotlib.pyplot as plt
import numpy as np
import os
import re
import sys


acsize = re.compile(r"\|VEKD: downset_size=(\d*),(\d*)\|")
dims = []
sizs = []


def findAntichains(line):
    for match in acsize.finditer(line):
        dims.append(int(match.group(1)))
        sizs.append(int(match.group(2)))


def parse(fname):
    global dims, sizs

    cache = fname + ".cache"
    if os.path.isfile(cache):
        with open(cache, 'r') as f:
            data = json.load(f)
            dims = data["dims"]
            sizs = data["sizs"]
    else:
        with open(fname, 'r') as f:
            while True:
                line = f.readline()
                if not line:
                    break
                else:
                    findAntichains(line)
        with open(cache, 'w') as f:
            json.dump({"dims": dims,
                       "sizs": sizs}, f)


def graph():
    mx = 600
    # plot x vs y using blue circle markers
    plt.plot(dims, sizs, "bo")
    # plt.yscale("log")
    plt.xlim(0, mx)
    plt.xlabel("Dimension")
    plt.ylabel("Size of antichain")
    # for reference, we add a solid gray exponential line
    y = np.linspace(1, max(sizs))
    x = np.log(y)
    plt.plot(x, y, color="gray", label=r"$\exp(d)$")
    # for other reference, we add a solid line
    y = np.linspace(1, max(sizs))
    x = np.array(list(map(lambda a: a / 2., y)))
    plt.plot(x, y, color="pink", label="$m = 2d$")
    # show plots
    plt.legend(loc="upper right")
    plt.show()


if __name__ == "__main__":
    assert len(sys.argv) > 1
    parse(sys.argv[1])
    graph()
    exit(os.EX_OK)
