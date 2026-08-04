"""Paper-inspired grid world used by the shielding notebook and MODeM talk.

The map is a compact adaptation of the grid-world example released with
Alshiekh et al., *Safe Reinforcement Learning via Shielding* (AAAI 2018).  It
deliberately keeps the visible cell MDP separate from the stochastic gate and
the ordered-visit monitor.

The module itself depends only on the Python standard library.  PNG rendering
uses the external Graphviz ``dot`` executable (installed in
``Dockerfile.boomslang``).
"""

from __future__ import annotations

import argparse
import random
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Mapping, Sequence


MAP_ROWS = (
    "44#.1",
    "..#..",
    ".3g2.",
    "..#..",
)
ACTIONS = ("north", "east", "south", "west", "wait")
ACTION_DELTAS = {
    "north": (0, -1),
    "east": (1, 0),
    "south": (0, 1),
    "west": (-1, 0),
    "wait": (0, 0),
}
INPUT_APS = ("blocked",)
OUTPUT_APS = (
    "next_cell_b0",
    "next_cell_b1",
    "next_cell_b2",
    "next_cell_b3",
    "next_cell_b4",
    "next_stage_b0",
    "next_stage_b1",
    "tour_complete",
)
INVALID_CELL = 31


@dataclass(frozen=True, slots=True)
class ProductState:
    """State of the physical cell × gate × ordered-monitor product."""

    cell: int
    blocked: bool
    progress: int = 0


@dataclass(frozen=True, slots=True)
class Transition:
    state: ProductState
    probability: float
    collision: bool
    completed: bool

    @property
    def reward(self) -> float:
        """Notebook reward: tour +1, collision -1, and -0.01 per step."""

        return float(self.completed) - float(self.collision) - 0.01


@dataclass(frozen=True, slots=True)
class ActionOutcome:
    """System output induced by an ordinary movement proposal."""

    cell_code: int
    progress: int
    completed: bool
    collision: bool


class GridWorld:
    """Finite 5×4 grid with a stochastic one-cell gate."""

    def __init__(self, rows: Sequence[str] = MAP_ROWS):
        if tuple(rows) != MAP_ROWS:
            raise ValueError("the talk uses the canonical 5x4 paper grid")
        self.rows = tuple(rows)
        positions: list[tuple[int, int]] = []
        regions: dict[int, int] = {}
        gate: int | None = None
        for y, row in enumerate(self.rows):
            for x, marker in enumerate(row):
                if marker == "#":
                    continue
                cell = len(positions)
                positions.append((x, y))
                if marker in "1234":
                    regions[cell] = int(marker)
                elif marker == "g":
                    gate = cell
        if len(positions) != 17 or gate is None:
            raise ValueError("canonical map must contain 17 cells and one gate")
        self.positions = tuple(positions)
        self.cell_at = {position: cell for cell, position in enumerate(positions)}
        self.regions: Mapping[int, int] = regions
        self.gate = gate
        self.start = self.cell_at[(0, 0)]

    @property
    def physical_states(self) -> tuple[int, ...]:
        return tuple(range(len(self.positions)))

    @property
    def product_state_count(self) -> int:
        return len(self.positions) * 2 * 4

    def initial_state(self, *, blocked: bool = False) -> ProductState:
        return ProductState(self.start, blocked, 0)

    def adjacent_cells(self, cell: int, *, include_wait: bool = True) -> tuple[int, ...]:
        self._check_cell(cell)
        x, y = self.positions[cell]
        cells = {cell} if include_wait else set()
        for action in ACTIONS[:4]:
            dx, dy = ACTION_DELTAS[action]
            neighbour = self.cell_at.get((x + dx, y + dy))
            if neighbour is not None:
                cells.add(neighbour)
        return tuple(sorted(cells))

    def move(self, cell: int, action: str, blocked: bool) -> tuple[int, bool]:
        """Return ``(next_cell, collision)`` before the gate changes state."""

        self._check_cell(cell)
        if action not in ACTION_DELTAS:
            raise ValueError(f"unknown action {action!r}; expected one of {ACTIONS}")
        if action == "wait":
            return cell, False
        x, y = self.positions[cell]
        dx, dy = ACTION_DELTAS[action]
        candidate = self.cell_at.get((x + dx, y + dy))
        if candidate is None or (candidate == self.gate and blocked):
            return cell, True
        return candidate, False

    @staticmethod
    def gate_distribution(blocked: bool) -> tuple[tuple[bool, float], ...]:
        if blocked:
            return ((False, 1.0),)
        return ((False, 0.75), (True, 0.25))

    def successors(self, state: ProductState, action: str) -> tuple[Transition, ...]:
        self._check_state(state)
        next_cell, collision = self.move(state.cell, action, state.blocked)
        next_progress, completed = self.advance_progress(state.progress, next_cell)
        return tuple(
            Transition(
                ProductState(next_cell, next_blocked, next_progress),
                probability,
                collision,
                completed,
            )
            for next_blocked, probability in self.gate_distribution(state.blocked)
        )

    def sample_successor(
        self, state: ProductState, action: str, rng: random.Random
    ) -> Transition:
        threshold = rng.random()
        cumulative = 0.0
        options = self.successors(state, action)
        for option in options:
            cumulative += option.probability
            if threshold <= cumulative:
                return option
        return options[-1]

    def advance_progress(self, progress: int, cell: int) -> tuple[int, bool]:
        if progress not in range(4):
            raise ValueError("progress must be 0, 1, 2, or 3")
        self._check_cell(cell)
        region = self.regions.get(cell)
        if region == progress + 1:
            return (progress + 1) % 4, progress == 3
        return progress, False

    def action_outcome(self, state: ProductState, action: str) -> ActionOutcome:
        """Map a movement proposal to the output valuation checked by Acacia.

        Collisions use an invalid five-bit cell code.  Thus an action into a
        wall or a blocked gate cannot masquerade as a legal wait.
        """

        self._check_state(state)
        next_cell, collision = self.move(state.cell, action, state.blocked)
        progress, completed = self.advance_progress(state.progress, next_cell)
        return ActionOutcome(
            INVALID_CELL if collision else next_cell,
            progress,
            completed,
            collision,
        )

    def valuation(self, state: ProductState, action: str) -> dict[str, bool]:
        outcome = self.action_outcome(state, action)
        bits = _bits(outcome.cell_code, 5) + _bits(outcome.progress, 2)
        values = [state.blocked, *bits, outcome.completed]
        return dict(zip(INPUT_APS + OUTPUT_APS, values, strict=True))

    def _check_cell(self, cell: int) -> None:
        if cell not in self.physical_states:
            raise ValueError(f"cell must be in 0..{len(self.positions) - 1}")

    def _check_state(self, state: ProductState) -> None:
        self._check_cell(state.cell)
        if state.progress not in range(4):
            raise ValueError("progress must be 0, 1, 2, or 3")


def paper_gridworld() -> GridWorld:
    """Return the canonical 17-cell model used throughout the example."""

    return GridWorld()


def successors(
    state: ProductState, action: str, model: GridWorld | None = None
) -> tuple[Transition, ...]:
    return (model or paper_gridworld()).successors(state, action)


def advance_progress(
    progress: int, cell: int, model: GridWorld | None = None
) -> tuple[int, bool]:
    return (model or paper_gridworld()).advance_progress(progress, cell)


def _bits(value: int, width: int) -> list[bool]:
    return [bool((value >> bit) & 1) for bit in range(width)]


def _encoding(names: Sequence[str], value: int) -> str:
    atoms = [name if bit else f"!{name}" for name, bit in zip(names, _bits(value, len(names)))]
    return "(" + " & ".join(atoms) + ")"


def _disjunction(parts: Iterable[str]) -> str:
    return "(" + " | ".join(parts) + ")"


def bare_ltl(model: GridWorld | None = None) -> str:
    """Return wall/gate, motion, monitor, and repeated-tour guarantees."""

    world = model or paper_gridworld()
    cell_bits = OUTPUT_APS[:5]
    stage_bits = OUTPUT_APS[5:7]
    cell = lambda value: _encoding(cell_bits, value)
    stage = lambda value: _encoding(stage_bits, value)
    valid_cell = _disjunction(cell(value) for value in world.physical_states)
    valid_stage = _disjunction(stage(value) for value in range(4))

    clauses = [
        f"G({valid_cell})",
        f"G({valid_stage})",
        _disjunction(cell(value) for value in world.adjacent_cells(world.start)),
        f"G(blocked -> !{cell(world.gate)})",
    ]
    for source in world.physical_states:
        legal = _disjunction(cell(value) for value in world.adjacent_cells(source))
        clauses.append(f"G({cell(source)} -> X {legal})")

    for candidate in world.adjacent_cells(world.start):
        next_stage, completed = world.advance_progress(0, candidate)
        done = "tour_complete" if completed else "!tour_complete"
        clauses.append(f"({cell(candidate)} -> ({stage(next_stage)} & {done}))")

    for progress in range(4):
        updates = []
        for candidate in world.physical_states:
            next_stage, completed = world.advance_progress(progress, candidate)
            done = "tour_complete" if completed else "!tour_complete"
            updates.append(f"({cell(candidate)} -> ({stage(next_stage)} & {done}))")
        clauses.append(f"G({stage(progress)} -> X (" + " & ".join(updates) + "))")

    clauses.append("G F tour_complete")
    return " & ".join(f"({clause})" for clause in clauses)


def assumed_ltl(model: GridWorld | None = None) -> str:
    """Return the operative specification with recurring gate availability."""

    return f"(G F !blocked) -> ({bare_ltl(model)})"


def _node_line(world: GridWorld, cell: int, *, shield: bool = False) -> str:
    x, y = world.positions[cell]
    marker = world.rows[y][x]
    fill = {
        "1": "#dbeafe",
        "2": "#ffedd5",
        "3": "#ede9fe",
        "4": "#dcfce7",
        "g": "#fee2e2",
        ".": "#f8fafc",
    }[marker]
    shape = "diamond" if marker == "g" else "box"
    label = f"s{cell}"
    if marker in "1234g":
        label += f"\\n{marker}"
    attributes = [
        f'label="{label}"',
        f'pos="{x * 1.15:.2f},{(3 - y) * 1.15:.2f}!"',
        f"shape={shape}",
        "style=filled",
        f'fillcolor="{fill}"',
    ]
    if cell == world.start:
        attributes.extend(["peripheries=2", 'color="#f59e0b"', "penwidth=2.2"])
    if shield and cell == world.start:
        attributes.extend(['xlabel="after one wait"', 'fontcolor="#9a3412"'])
    return f"  cell_{cell} [{', '.join(attributes)}];"


def _map_dot_lines(world: GridWorld, *, shield: bool = False) -> list[str]:
    lines = [_node_line(world, cell, shield=shield) for cell in world.physical_states]
    for wall_index, (x, y) in enumerate(
        (position for position in ((x, y) for y in range(4) for x in range(5)) if world.rows[position[1]][position[0]] == "#")
    ):
        lines.append(
            f'  wall_{wall_index} [label="#", pos="{x * 1.15:.2f},{(3 - y) * 1.15:.2f}!", '
            'shape=box, style=filled, fillcolor="#334155", fontcolor="white"];'
        )
    seen: set[tuple[int, int]] = set()
    for source in world.physical_states:
        for target in world.adjacent_cells(source, include_wait=False):
            edge = tuple(sorted((source, target)))
            if edge not in seen:
                seen.add(edge)
                lines.append(f'  cell_{edge[0]} -- cell_{edge[1]} [color="#94a3b8"];')
    return lines


def to_dot(view: str = "mdp", model: GridWorld | None = None) -> str:
    """Return a deterministic Graphviz representation of the requested view."""

    world = model or paper_gridworld()
    if view not in {"mdp", "shield"}:
        raise ValueError("view must be 'mdp' or 'shield'")
    lines = [
        "graph paper_gridworld {",
        '  graph [layout=neato, bgcolor="white", margin=0.08, overlap=false, outputorder=edgesfirst];',
        '  node [fontname="DejaVu Sans", fontsize=10, width=0.55, height=0.55, fixedsize=true, color="#64748b"];',
        '  edge [penwidth=1.25];',
    ]
    lines.extend(_map_dot_lines(world, shield=view == "shield"))
    if view == "mdp":
        annotations = (
            ("physical", "visible cell MDP\\n17 positions\\nN / E / S / W / wait", 6.5, 3.45, "#dbeafe"),
            ("gate_factor", "gate factor\\nopen -> blocked  .25\\nopen -> open  .75\\nblocked -> open  1", 6.5, 1.7, "#fee2e2"),
            ("monitor", "monitor factor\\nexpect 1 -> 2 -> 3 -> 4\\n4 progress stages", 6.5, -0.05, "#dcfce7"),
            ("product", "full state = 17 x 2 x 4", 9.35, 1.7, "#ffedd5"),
        )
        for name, label, x, y, fill in annotations:
            lines.append(
                f'  {name} [label="{label}", pos="{x},{y}!", shape=box, fixedsize=false, '
                f'width=2.2, height=0.82, style="rounded,filled", fillcolor="{fill}", penwidth=1.5];'
            )
        lines.extend(
            [
                '  physical -- gate_factor [label=" x ", color="#64748b", fontcolor="#475569"];',
                '  gate_factor -- monitor [label=" x ", color="#64748b", fontcolor="#475569"];',
                '  gate_factor -- product [label=" = ", color="#64748b", fontcolor="#475569"];',
            ]
        )
    else:
        action_data = (
            ("north", "N: wall", 6.35, 3.7, "#fecaca", "#b91c1c"),
            ("east", "E: allowed", 7.8, 2.75, "#bbf7d0", "#15803d"),
            ("south", "S: allowed", 7.8, 1.8, "#bbf7d0", "#15803d"),
            ("west", "W: boundary", 6.35, 0.85, "#fecaca", "#b91c1c"),
            ("wait16", "wait: excluded at k=16", 9.75, 3.15, "#fecaca", "#b91c1c"),
            ("wait20", "wait: allowed at k=20", 9.75, 1.4, "#bbf7d0", "#15803d"),
        )
        lines.append(
            '  learner [label="learner proposal", pos="6.1,2.25!", shape=ellipse, fixedsize=false, '
            'width=1.45, height=0.65, style=filled, fillcolor="#ffedd5", color="#ea580c", penwidth=1.8];'
        )
        for name, label, x, y, fill, colour in action_data:
            lines.append(
                f'  {name} [label="{label}", pos="{x},{y}!", shape=box, fixedsize=false, width=1.72, '
                f'height=0.5, style="rounded,filled", fillcolor="{fill}", color="{colour}", fontcolor="{colour}"];'
            )
        for name in ("north", "east", "south", "west"):
            lines.append(f'  learner -- {name} [color="#94a3b8"];')
        lines.extend(
            [
                '  envelope [label="same safety boundary\\nmore co-Buchi slack = more waiting freedom", pos="9.6,0.15!", '
                'shape=note, fixedsize=false, width=2.7, height=0.72, style=filled, fillcolor="#f8fafc"];',
            ]
        )
    lines.append("}")
    return "\n".join(lines) + "\n"


def render_png(
    view: str = "mdp",
    path: str | Path | None = None,
    model: GridWorld | None = None,
) -> bytes | Path:
    """Render one view with Graphviz, returning bytes or the written path."""

    executable = shutil.which("dot")
    if executable is None:
        raise RuntimeError(
            "Graphviz is required to render the grid world; install the 'dot' executable"
        )
    binary = subprocess.run(
        [executable, "-Kneato", "-Tpng"],
        input=to_dot(view, model).encode(),
        capture_output=True,
        check=False,
    )
    if binary.returncode:
        detail = binary.stderr.decode(errors="replace").strip() or "unknown Graphviz error"
        raise RuntimeError(f"Graphviz could not render the {view!r} view: {detail}")
    data = binary.stdout
    if path is None:
        return data
    destination = Path(path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(data)
    return destination


def write_artifacts(
    directory: str | Path, model: GridWorld | None = None
) -> dict[str, Path]:
    """Write canonical DOT and PNG assets for both talk views."""

    output = Path(directory)
    output.mkdir(parents=True, exist_ok=True)
    artifacts: dict[str, Path] = {}
    for view, stem in (("mdp", "paper-gridworld-mdp"), ("shield", "paper-gridworld-shield")):
        dot_path = output / f"{stem}.dot"
        png_path = output / f"{stem}.png"
        dot_path.write_text(to_dot(view, model), encoding="utf-8")
        render_png(view, png_path, model)
        artifacts[f"{view}_dot"] = dot_path
        artifacts[f"{view}_png"] = png_path
    return artifacts


def _main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--write-artifacts",
        metavar="DIRECTORY",
        type=Path,
        help="write canonical DOT and PNG views into DIRECTORY",
    )
    parser.add_argument("--view", choices=("mdp", "shield"), default="mdp")
    args = parser.parse_args()
    if args.write_artifacts:
        for name, path in write_artifacts(args.write_artifacts).items():
            print(f"{name}: {path}")
    else:
        print(to_dot(args.view), end="")


if __name__ == "__main__":
    _main()
