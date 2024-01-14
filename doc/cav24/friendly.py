#!/usr/bin/env python3


import itertools
import json
import matplotlib.pyplot as plt
import os
import sys


mrk = itertools.cycle(['o', '^', 'x'])
clr = itertools.cycle(['g', 'r', 'b'])


def loadTests(fname):
    print(fname)
    with open(fname, 'r') as f:
        stats = json.load(f)["stats"]
    bench = []
    rtime = []
    del stats["result"]
    for exp in stats:
        bench.append(exp[exp.find("/") + 1:exp.rfind(".ltl")])
        rtime.append(float(stats[exp]["rtime"]))
    return (bench, rtime)


def graph(results):
    for k in ["best_downset_vector.json2",
              "best_downset_vector_or_kdtree.json2",
              "best_downset_kdtree.json2"]:
        (bench, rtime) = results[k]
        plt.scatter(bench, rtime, label=k, marker=next(mrk),
                    c=next(clr))
    # plt.yscale("log")
    plt.ylim(0, 21)
    # plt.xlabel("Benchmarks")
    # plt.ylabel("CPU time (s)")
    plt.xticks(rotation=20)
    # show plots
    # plt.legend(loc="upper right")
    plt.show()


if __name__ == "__main__":
    assert len(sys.argv) > 1
    results = dict()
    for file in os.listdir(sys.argv[1]):
        if file.endswith(".json2"):
            (bench, rtime) = loadTests(os.path.join(sys.argv[1], file))
            results[file] = (bench, rtime)
    graph(results)
    exit(os.EX_OK)
