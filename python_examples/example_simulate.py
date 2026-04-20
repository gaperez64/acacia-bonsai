"""
Showcase for issue #51: simulate a non-deterministic controller from the
winning region in a loop, and bump k whenever the user picks an output that
drives the system out of the currently-known winning region.

Flow
----
1. Build the UCB for the negated spec and solve the safety game with an
   initial bound k = INITIAL_K.
2. Start from the initial state vector (which is in the winning region iff
   the spec is realizable at this k).
3. At each step:
   * The environment is asked for an input valuation.
   * We enumerate all output valuations; those that keep the state vector
     inside the winning region form the "winning outputs" (the
     non-deterministic controller).  The "losing outputs" are shown too.
   * The user picks *any* output.  We compute the forward successor.
   * If the successor is still winning, the loop continues.
     Otherwise we call acacia again with a bigger k; if the larger winning
     region now contains the successor, we keep going with the new solver
     result.  If not, the play really has left every winning region we can
     afford and we stop.

This mirrors the old `scripts/simwreg.py` design but does not depend on a
synthesised AIGER circuit: it works directly on the Python-exposed UCB and
winning region.

Run:
    PYTHONPATH=<build>/src/python python3 python_examples/example_simulate.py

The script accepts one optional CLI arg: "auto" to drive the simulation
automatically (useful as a smoke test); without it, the user is prompted.
"""
import itertools
import sys

import acacia_boomslang as ab


# -------------------------- configuration --------------------------

INITIAL_K = 2
K_STEP    = 3        # how much to grow k when the play escapes the winreg
MAX_K     = 30       # give up at this point
MAX_STEPS = 8        # auto-mode step budget

SPEC = "!((G (F (req))) -> (G (F (grant))))"   # negated: UCB for bad plays
INPUTS  = ["req"]
OUTPUTS = ["grant"]


# -------------------------- helpers --------------------------

def vec_to_list(v):
    """Copy a vector_wrapper into a plain Python list of ints."""
    return [v[i] for i in range(len(v))]


def all_assignments(aps):
    """Yield every boolean assignment of `aps` as (true_list, false_list)."""
    for bits in itertools.product([False, True], repeat=len(aps)):
        t = [a for a, b in zip(aps, bits) if b]
        f = [a for a, b in zip(aps, bits) if not b]
        yield t, f


def label(true_aps, false_aps):
    if not true_aps and not false_aps:
        return "tt"
    ts = [f"{a}" for a in true_aps]
    fs = [f"!{a}" for a in false_aps]
    return " & ".join(ts + fs) if ts or fs else "tt"


def solve(game, k_max):
    """Solve (or re-solve) with a given bound.  Returns GameResult."""
    ab.preprocess_aut_standard(game, k_max=k_max)
    ab.set_bool_thresh_no_bool_states(game, k_max=k_max)
    return ab.solve_acacia_safety_game(
        game, k_max=k_max, k_min=min(2, k_max), k_inc=K_STEP)


def classify_outputs(game, v, true_inputs, false_inputs, winreg, k_cap):
    """Split every output assignment into (winning, losing, successor)."""
    winning, losing = [], []
    for t_out, f_out in all_assignments(OUTPUTS):
        s = ab.successor(
            game, v,
            ab.StringVector(true_inputs + t_out),
            ab.StringVector(false_inputs + f_out),
            k_cap)
        if winreg.contains(s):
            winning.append((t_out, f_out, s))
        else:
            losing.append((t_out, f_out, s))
    return winning, losing


def pick_output(winning, losing, interactive):
    """Ask the user (or autopick) for an output choice.

    Auto-mode picks a winning output to smoke-test the happy path. The
    escape-and-grow-k branch is best exercised interactively (the winning
    region for a finite-memory spec stabilises quickly, so "grow k to
    recover" is visible mostly when the spec is truly unrealisable and the
    re-solve attempts surface that fact — not a great demo value).
    """
    if not interactive:
        if winning:
            t, f, s = winning[0]
            print(f"[auto] picking winning output {label(t, f)}")
            return t, f, s
        # No winning output means the current state is already losing.
        t, f, s = losing[0]
        print(f"[auto] only losing outputs available, picking {label(t, f)}")
        return t, f, s

    print("  winning outputs:")
    for i, (t, f, _) in enumerate(winning):
        print(f"    [W{i}] {label(t, f)}")
    print("  losing outputs:")
    for i, (t, f, _) in enumerate(losing):
        print(f"    [L{i}] {label(t, f)}")
    while True:
        raw = input("  pick (e.g. W0 or L1, q to quit)> ").strip()
        if raw.lower() == "q":
            return None
        tag, idx = raw[0].upper(), int(raw[1:])
        src = winning if tag == "W" else losing
        if 0 <= idx < len(src):
            return src[idx]


# -------------------------- main loop --------------------------

def main(interactive):
    # Build the UCB once and reuse it across k bumps: the TWA/dict/APs are
    # independent of k, only the solver call itself changes.
    game = ab.create_twa(SPEC, INPUTS, OUTPUTS)
    n = ab.num_states(game)
    print(f"Built UCB with {n} states "
          f"(initial state = {ab.initial_state_number(game)}).")

    k = INITIAL_K
    result = solve(game, k)
    if not result.is_real():
        print(f"Unrealizable at k={k}.  Increasing won't help for a safety game "
              f"at this engine, aborting.")
        return 1

    winreg = result.get_winning_region()
    v = ab.get_initial_state(game)
    assert winreg.contains(v), "initial state is not in winning region — bug?"
    print(f"Solved at k={k}; winning region has {len(winreg)} maximal vectors.")

    steps_taken = 0
    while True:
        steps_taken += 1
        if not interactive and steps_taken > MAX_STEPS:
            print(f"[auto] reached step budget ({MAX_STEPS}), stopping.")
            break

        print(f"\n-- step {steps_taken} --")
        print(f"  state vector: {vec_to_list(v)} (in winreg? {winreg.contains(v)})")

        # Environment input: pick the first assignment (auto) or ask the user.
        if interactive:
            raw = input(f"  environment input (true APs, subset of {INPUTS})> ").strip()
            true_inputs = [a for a in raw.split() if a in INPUTS]
        else:
            # Alternate req / !req each step to get some variation.
            true_inputs = INPUTS if steps_taken % 2 == 0 else []
        false_inputs = [a for a in INPUTS if a not in true_inputs]
        print(f"  input: {label(true_inputs, false_inputs)}")

        winning, losing = classify_outputs(
            game, v, true_inputs, false_inputs, winreg, k)

        pick = pick_output(winning, losing, interactive)
        if pick is None:
            break
        t_out, f_out, s = pick

        if winreg.contains(s):
            print(f"  -> successor {vec_to_list(s)} is winning, continuing.")
            v = s
            continue

        # Escape: try to grow the winning region.
        print(f"  -> successor {vec_to_list(s)} is NOT in winreg(k={k}).")
        new_k = k
        while new_k + K_STEP <= MAX_K:
            new_k += K_STEP
            print(f"     re-solving with k={new_k} ...")
            # Re-create the game because preprocess_aut_standard is
            # idempotent but set_bool_thresh_* mutates the global threshold
            # and rebuilding keeps things hygienic.
            game2 = ab.create_twa(SPEC, INPUTS, OUTPUTS)
            result2 = solve(game2, new_k)
            if not result2.is_real():
                continue
            winreg2 = result2.get_winning_region()
            # Re-cast the successor vector against the fresh game: since the
            # TWA is built deterministically from the same formula, the
            # state numbering matches.
            s_new = ab.make_vector(game2, ab.IntVector(vec_to_list(s)))
            if winreg2.contains(s_new):
                print(f"     now in winreg(k={new_k}) (size {len(winreg2)}); "
                      f"switching.")
                game, winreg, v, k = game2, winreg2, s_new, new_k
                break
        else:
            print(f"  escape permanent up to k={MAX_K}; stopping.")
            return 2

    print("\nSimulation finished.")
    return 0


if __name__ == "__main__":
    interactive = not (len(sys.argv) > 1 and sys.argv[1] == "auto")
    sys.exit(main(interactive))
