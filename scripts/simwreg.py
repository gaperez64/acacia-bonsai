import aiger
import math
import subprocess as sp


abscript = "./getwinregion.sh"
safekey = "_ab_single_output"
vbitkey = "_ab_vecstate_bit_"
benchmark = "../tests/ltl/mod-theories/andoni_ex1.tlsf"
wregfile = "wreg.aag"
kval = 11


def interpOutput(outputs, dim, kbits):
    vec = [0] * dim
    if outputs[safekey]:
        return True, tuple(vec)
    for i in range(dim):
        base = i * kbits
        if outputs[vbitkey + str(base)]:
            vec[i] = -1
            assert not any([outputs[vbitkey + str(base + j)]
                           for j in range(1, kbits)])
        for b in range(1, kbits):
            if outputs[vbitkey + str(base + b)]:
                vec[i] += 1 << kbits - (b + 1)
    return False, tuple(vec)


def getwreg(benchfname, wregfname, k, init=None):
    tocall = [abscript, benchfname, wregfname, str(k)]
    if init is not None:
        tocall.append(",".join(map(str, init)))
    res = sp.run(tocall, capture_output=True, encoding="utf-8")
    if res.returncode != 0:
        print("Unable to synthesize controller, abonsai "
              f"retcode={res.returncode}")
        print(res.stdout)
        exit(1)


def sim(benchfname, wregfname, k):
    kbits = math.ceil(math.log(k, 2)) + 1
    while True:
        print("(Re)starting simulation")
        wreg = aiger.load(wregfname)
        dim = (len(wreg.outputs) - 1) // kbits
        print(f"k = {k}, bits per comp. = {kbits}, dim = {dim}")

        # Starting the simulator
        inputs = sorted(list(wreg.inputs))
        engine = wreg.simulator()
        next(engine)

        # Put things in a loop
        while True:
            data = input(f"Values for {inputs} (sep.'d by spaces): ")
            if data == "":
                print("Exiting simulation loop")
                return 0
            print(f"Read {data}")
            outputs, _ = engine.send(dict(zip(inputs,
                                              map(int, data.split()))))
            safe, vec = interpOutput(outputs, dim, kbits)
            print(f"Is safe? {safe}")
            if not safe:
                print(f"Successor vector {vec}")
                getwreg(benchfname, wregfname, k, vec)
                break

    return 0


if __name__ == "__main__":
    getwreg(benchmark, wregfile, kval)
    exit(sim(benchmark, wregfile, kval))
