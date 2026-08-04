"""Tests for the paper-inspired grid world used by the shielding example."""

from __future__ import annotations

import sys
from pathlib import Path

import pytest


REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "python_examples"))
try:
    import finite_mdp
finally:
    sys.path.pop(0)


def test_canonical_map_has_seventeen_physical_states():
    world = finite_mdp.paper_gridworld()
    assert world.rows == ("44#.1", "..#..", ".3g2.", "..#..")
    assert len(world.physical_states) == 17
    assert world.start == world.cell_at[(0, 0)]
    assert world.gate == world.cell_at[(2, 2)]
    assert world.product_state_count == 17 * 2 * 4


def test_every_transition_distribution_is_normalized():
    world = finite_mdp.paper_gridworld()
    for cell in world.physical_states:
        for blocked in (False, True):
            for progress in range(4):
                state = finite_mdp.ProductState(cell, blocked, progress)
                for action in finite_mdp.ACTIONS:
                    options = finite_mdp.successors(state, action, world)
                    assert sum(option.probability for option in options) == pytest.approx(1.0)
                    assert all(option.probability > 0.0 for option in options)


def test_walls_and_blocked_gate_are_collisions_but_wait_is_not():
    world = finite_mdp.paper_gridworld()
    start = world.initial_state()
    assert world.move(start.cell, "north", start.blocked) == (start.cell, True)
    assert world.move(start.cell, "west", start.blocked) == (start.cell, True)
    assert world.move(start.cell, "wait", start.blocked) == (start.cell, False)

    left_of_gate = world.cell_at[(1, 2)]
    assert world.move(left_of_gate, "east", False) == (world.gate, False)
    assert world.move(left_of_gate, "east", True) == (left_of_gate, True)
    collision = world.action_outcome(
        finite_mdp.ProductState(left_of_gate, True, 2), "east"
    )
    assert collision.collision
    assert collision.cell_code == finite_mdp.INVALID_CELL


def test_gate_markov_chain_matches_documented_probabilities():
    assert finite_mdp.GridWorld.gate_distribution(False) == (
        (False, 0.75),
        (True, 0.25),
    )
    assert finite_mdp.GridWorld.gate_distribution(True) == ((False, 1.0),)


def test_ordered_progress_ignores_out_of_order_regions_and_completes_tour():
    world = finite_mdp.paper_gridworld()
    region = {
        number: next(cell for cell, label in world.regions.items() if label == number)
        for number in range(1, 5)
    }
    assert finite_mdp.advance_progress(0, region[3], world) == (0, False)
    progress = 0
    for number in range(1, 4):
        progress, completed = finite_mdp.advance_progress(progress, region[number], world)
        assert (progress, completed) == (number, False)
    assert finite_mdp.advance_progress(progress, region[4], world) == (0, True)


def test_dot_output_is_deterministic_and_names_all_three_factors():
    first = finite_mdp.to_dot("mdp")
    second = finite_mdp.to_dot("mdp")
    assert first == second
    assert first.count("  cell_") >= 17
    assert "visible cell MDP" in first
    assert "gate factor" in first
    assert "monitor factor" in first
    assert "full state = 17 x 2 x 4" in first


def test_png_generation_and_artifact_writer(tmp_path):
    png = finite_mdp.render_png("shield")
    assert isinstance(png, bytes)
    assert png.startswith(b"\x89PNG\r\n\x1a\n")

    artifacts = finite_mdp.write_artifacts(tmp_path)
    assert set(artifacts) == {"mdp_dot", "mdp_png", "shield_dot", "shield_png"}
    assert artifacts["mdp_dot"].read_text(encoding="utf-8") == finite_mdp.to_dot("mdp")
    assert artifacts["shield_dot"].read_text(encoding="utf-8") == finite_mdp.to_dot("shield")
    assert artifacts["mdp_png"].read_bytes().startswith(b"\x89PNG")
    assert artifacts["shield_png"].read_bytes().startswith(b"\x89PNG")


def test_png_generation_reports_missing_graphviz_clearly(monkeypatch):
    monkeypatch.setattr(finite_mdp.shutil, "which", lambda _name: None)
    with pytest.raises(RuntimeError, match="Graphviz.*dot"):
        finite_mdp.render_png("mdp")


def test_ltl_variants_make_the_gate_assumption_explicit():
    bare = finite_mdp.bare_ltl()
    assumed = finite_mdp.assumed_ltl()
    assert "G(blocked ->" in bare
    assert "G F tour_complete" in bare
    assert assumed == f"(G F !blocked) -> ({bare})"
