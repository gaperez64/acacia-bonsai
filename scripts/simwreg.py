import aiger
import math


safekey = "_ab_single_output"
vbitkey = "_ab_vecstate_bit_"


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


def sim(fname, k):
    wreg = aiger.load(fname)
    kbits = math.ceil(math.log(k, 2)) + 1
    dim = (len(wreg.outputs) - 1) // kbits
    print(f"k = {k}, bits per comp. = {kbits}, dim = {dim}")

    # Starting the simulator
    inputs = list(wreg.inputs)
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

    return 0


if __name__ == "__main__":
    exit(sim("wreg.aag", 11))
