"""Independent explicit AAG simulation and deterministic safety-monitor product."""

from collections import deque
from dataclasses import dataclass
from itertools import product
from pathlib import Path


@dataclass(frozen=True)
class Aiger:
    max_var: int
    inputs: tuple
    latches: tuple
    outputs: tuple
    gates: tuple
    resets: tuple
    input_names: tuple
    output_names: tuple

    @classmethod
    def read(cls, path):
        lines = Path(path).read_text().splitlines()
        header = lines[0].split()
        if len(header) != 6 or header[0] != "aag":
            raise ValueError("oracle requires basic ASCII AIGER")
        maximum, ni, nl, no, ng = map(int, header[1:])
        cursor = 1
        inputs = tuple(int(v) for v in lines[cursor:cursor + ni])
        cursor += ni
        latches, resets = [], []
        for line in lines[cursor:cursor + nl]:
            row = tuple(map(int, line.split()))
            if len(row) not in (2, 3):
                raise ValueError("invalid latch")
            current, following = row[:2]
            reset = row[2] if len(row) == 3 else 0
            if reset not in (0, 1, current):
                raise ValueError("invalid reset")
            latches.append((current, following))
            resets.append(reset)
        cursor += nl
        outputs = tuple(int(v) for v in lines[cursor:cursor + no])
        cursor += no
        gates = tuple(tuple(map(int, line.split())) for line in lines[cursor:cursor + ng])
        cursor += ng
        ins = [f"i{i}" for i in range(ni)]
        outs = [f"o{i}" for i in range(no)]
        for line in lines[cursor:]:
            if line == "c":
                break
            kind, _, name = line.partition(" ")
            if kind.startswith("i"):
                ins[int(kind[1:])] = name
            elif kind.startswith("o"):
                outs[int(kind[1:])] = name
        defined = {0}
        for literal in (*inputs, *(latch[0] for latch in latches)):
            if literal <= 0 or literal % 2 or literal // 2 in defined:
                raise ValueError("invalid variable definition")
            defined.add(literal // 2)
        for row in gates:
            if len(row) != 3:
                raise ValueError("invalid AND gate")
            lhs, left, right = row
            if (lhs <= 0 or lhs % 2 or lhs // 2 in defined
                    or left // 2 not in defined or right // 2 not in defined):
                raise ValueError("AND gates must be in topological order")
            defined.add(lhs // 2)
        for literal in (*outputs, *(latch[1] for latch in latches)):
            if literal // 2 not in defined:
                raise ValueError("undefined literal")
        if max(defined) > maximum or len(inputs) != ni or len(outputs) != no:
            raise ValueError("invalid header")
        return cls(maximum, inputs, tuple(latches), outputs, gates, tuple(resets),
                   tuple(ins), tuple(outs))

    def initial_states(self):
        choices = [(False, True) if reset == latch[0] else (bool(reset),)
                   for latch, reset in zip(self.latches, self.resets)]
        return product(*choices)

    def step(self, state, inputs):
        values = [False] * (self.max_var + 1)
        for literal, value in zip(self.inputs, inputs):
            values[literal // 2] = value
        for (literal, _), value in zip(self.latches, state):
            values[literal // 2] = value

        def evaluate(literal):
            return values[literal // 2] ^ bool(literal & 1)

        for lhs, left, right in self.gates:
            values[lhs // 2] = evaluate(left) and evaluate(right)
        # Both output and next-state functions use the OLD latch valuation.
        outputs = tuple(evaluate(literal) for literal in self.outputs)
        following = tuple(evaluate(literal) for _, literal in self.latches)
        return outputs, following


@dataclass
class Result:
    valid: bool
    states: int
    transitions: int
    counterexample: list
    input_dependent: bool = False


def check(circuit, initial_monitor, transition, *, moore=True, max_states=200000):
    """Explore ALL inputs and reachable (latch valuation, DFA state) pairs.

    transition(monitor_state, letter) returns the next DFA state or None on
    rejection.  Unknown latch resets are universally enumerated.  A state cap
    raises an error rather than returning a successful check.
    """
    alphabet = tuple(product((False, True), repeat=len(circuit.inputs)))
    parents = {(state, initial_monitor): None for state in circuit.initial_states()}
    queue = deque(parents)
    transitions = 0

    def witness(node, letter):
        trace = [letter]
        while parents[node] is not None:
            node, previous_letter = parents[node]
            trace.append(previous_letter)
        return list(reversed(trace))

    while queue:
        node = queue.popleft()
        state, monitor = node
        first_outputs = None
        for inputs in alphabet:
            outputs, following = circuit.step(state, inputs)
            letter = dict(zip(circuit.input_names, inputs))
            letter.update(zip(circuit.output_names, outputs))
            transitions += 1
            if first_outputs is None:
                first_outputs = outputs
            elif moore and outputs != first_outputs:
                return Result(False, len(parents), transitions, witness(node, letter), True)
            next_monitor = transition(monitor, letter)
            if next_monitor is None:
                return Result(False, len(parents), transitions, witness(node, letter))
            next_node = (following, next_monitor)
            if next_node not in parents:
                if len(parents) >= max_states:
                    raise RuntimeError("oracle state cap reached")
                parents[next_node] = (node, letter)
                queue.append(next_node)
    return Result(True, len(parents), transitions, [])


def delayed_table_monitor(inputs, output, table, delay, preset=None, preset_step=0,
                          start_step=0):
    """DFA for G(X**delay output <-> Boolean-table(inputs)), plus a preset."""
    initial = (0, (None,) * delay)

    def transition(state, letter):
        step, pending = state
        index = sum(int(letter[name]) << bit for bit, name in enumerate(inputs))
        expected = table[index]
        if step == preset_step and preset is not None and letter[output] != preset:
            return None
        if step >= start_step:
            if delay == 0:
                if letter[output] != expected:
                    return None
            else:
                if pending[0] is not None and letter[output] != pending[0]:
                    return None
                pending = (*pending[1:], expected)
        return min(step + 1, max(preset_step, start_step) + 1), pending

    return initial, transition
