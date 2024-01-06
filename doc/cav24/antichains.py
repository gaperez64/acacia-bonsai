#!/usr/bin/env python3

import matplotlib.pyplot as plt
import numpy as np
import os
import re
import sys


acsize = re.compile(r"\|VEKD: downset_size=(\d*),(\d*)\|")
bchnam = re.compile(r"{\"name\": .* / ab/(.*).ltl\", "
                    r"\"stdout\":")
dims = []
sizs = []
fdims = []
fsizs = []


def veryFriendly(yes, total):
    return yes * 10 > total


def findAntichains(line):
    total = 0
    kdtreeFriendly = 0
    locdims = []
    locsizs = []
    for match in acsize.finditer(line):
        d = int(match.group(1))
        s = int(match.group(2))
        locdims.append(d)
        locsizs.append(s)
        if 2 * d < s:
            kdtreeFriendly += 1
        total += 1
    dims.extend(locdims)
    sizs.extend(locsizs)
    if veryFriendly(kdtreeFriendly, total):
        fdims.extend(locdims)
        fsizs.extend(locsizs)
    return (kdtreeFriendly, total)


def parse(fname):
    with open(fname, 'r') as f:
        cnt = 0
        vf = 0
        while True:
            line = f.readline()
            if not line:
                break
            else:
                cnt += 1
                match = bchnam.search(line)
                (kdtree, total) = findAntichains(line)
                if veryFriendly(kdtree, total):
                    print(match.group(1))
                    vf += 1
        print(f"Found {cnt} benchmarks in total and {vf} very friendly")
        print(f"Got {len(sizs)} data points and {len(fsizs)} "
              "kdtree-friendly ones")


def graph(friendly):
    # plot x vs y using blue circle markers
    if friendly:
        mx = max(fsizs)
        plt.plot(fdims, fsizs, "gx")
    else:
        mx = max(sizs)
        plt.plot(dims, sizs, "bo")
    # plt.yscale("log")
    plt.xlabel("Dimension")
    plt.ylabel("Size of antichain")
    # for reference, we add a solid gray exponential line
    # y = np.linspace(1, mx)
    # x = np.log(y)
    # plt.plot(x, y, color="gray", label=r"$\exp(d)$")
    # for other reference, we add a solid line
    y = np.linspace(1, mx)
    x = np.array(list(map(lambda a: a / 2., y)))
    plt.plot(x, y, color="pink", label="$m = 2d$")
    # show plots
    plt.legend(loc="lower right")
    plt.show()


if __name__ == "__main__":
    assert len(sys.argv) > 1
    parse(sys.argv[1])
    graph(False)
    graph(True)
    exit(os.EX_OK)
