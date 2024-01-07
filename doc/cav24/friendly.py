#!/usr/bin/env python3

import json
import matplotlib.pyplot as plt
import os
import sys


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
    for k in results:
        (bench, rtime) = results[k]
        plt.scatter(bench, rtime, label=k)
    # plt.yscale("log")
    plt.ylim(0, 22)
    plt.xlabel("Benchmarks")
    plt.ylabel("Running time (s)")
    plt.xticks(rotation=30)
    # show plots
    plt.legend(loc="upper right")
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
