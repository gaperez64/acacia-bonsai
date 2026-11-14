"""Mealy game and IO ordering regressions for the binding soundness bug #393."""

import itertools
import subprocess
from pathlib import Path

import acacia_boomslang as ab
import pytest


ROOT = Path(__file__).resolve().parents[2]


def solve(formula, inputs, outputs, k=3):
    game = ab.create_twa(f"!({formula})", inputs, outputs)
    ab.preprocess_aut_standard(game, k)
    ab.set_bool_thresh_no_bool_states(game, k)
    return game, ab.solve_acacia_safety_game(game, k, k, 1)


@pytest.mark.parametrize("inputs,outputs", [(["z"], ["a"]), (["a"], ["z"])])
def test_environment_cannot_be_chosen_by_controller(inputs, outputs):
    # Translation of !(G(a & z)) registers a before z. In the first case,
    # translating before registering IO made MONA treat z as another output.
    _, result = solve("G(a & z)", inputs, outputs)
    assert not result.is_real()
    assert result.get_winning_region() is None


@pytest.mark.parametrize("formula,expected", [("G(a <-> z)", True),
                                              ("G(a <-> X z)", False)])
def test_mealy_controller_observes_current_input_only(formula, expected):
    game, result = solve(formula, ["z"], ["a"])
    assert result.is_real() == expected
    if expected:
        region = result.get_winning_region()
        assert region.contains(ab.get_initial_state(game))
        # Check true Mealy closure, using full IO assignments independently of
        # the solver's action precomputation. Every maximum needs a response
        # for EACH environment input.
        for maximum in region:
            for z in (False, True):
                successors = []
                for a in (False, True):
                    valuation = {"z": z, "a": a}
                    successors.append(ab.successor(
                        game, maximum,
                        [p for p, value in valuation.items() if value],
                        [p for p, value in valuation.items() if not value],
                        region.k(),
                    ))
                assert any(region.contains(v) for v in successors)


@pytest.fixture
def gridworld(monkeypatch):
    monkeypatch.syspath_prepend(str(ROOT / "python_examples"))
    import finite_mdp
    import shielding

    return finite_mdp, shielding


def test_bare_gridworld_has_no_bounded_winning_region(gridworld):
    model, shielding = gridworld
    _, result = shielding.build_game(model.bare_ltl(model.paper_gridworld()), 16)
    assert not result.is_real()
    assert result.get_winning_region() is None


def test_assumed_gridworld_controller_satisfies_specification(gridworld):
    import buddy
    import spot

    model, shielding = gridworld
    world = model.paper_gridworld()
    formula = model.assumed_ltl(world)
    game, result = shielding.build_game(formula, 16)
    assert result.is_real()
    region = result.get_winning_region()
    initial = ab.get_initial_state(game)
    assert region.contains(initial)
    shield = shielding.WinningRegionShield(world, game, result)
    assert shield.allowed(initial, world.initial_state()) == ["east", "south", "wait"]

    # Extract a controller using actual IO valuations, independently of MONA.
    # Represent its memory by dominating antichain maxima, as CLI synthesis does.
    maxima = list(region)

    def dominating(vector):
        return next(i for i, maximum in enumerate(maxima)
                    if all(x <= y for x, y in zip(vector, maximum, strict=True)))

    controller = spot.make_twa_graph()
    aps = {p: buddy.bdd_ithvar(controller.register_ap(p))
           for p in model.INPUT_APS + model.OUTPUT_APS}
    controller.new_states(len(maxima))
    controller.set_init_state(dominating(initial))
    controller.set_acceptance(0, "t")
    pending = [controller.get_init_state_number()]
    visited = set()
    while pending:
        source = pending.pop()
        if source in visited:
            continue
        visited.add(source)
        for blocked in (False, True):
            for outputs in itertools.product((False, True), repeat=len(model.OUTPUT_APS)):
                valuation = dict(zip(model.INPUT_APS + model.OUTPUT_APS,
                                     (blocked, *outputs), strict=True))
                successor = ab.successor(
                    game, maxima[source],
                    [p for p, value in valuation.items() if value],
                    [p for p, value in valuation.items() if not value],
                    region.k(),
                )
                if region.contains(successor):
                    target = dominating(successor)
                    guard = buddy.bddtrue
                    for p, value in valuation.items():
                        guard &= aps[p] if value else buddy.bdd_not(aps[p])
                    controller.new_edge(source, target, guard)
                    pending.append(target)
                    break
            else:
                pytest.fail(f"no response for input blocked={blocked} at maximum {source}")

    # Nonempty intersection with the bad language supplies a counterexample.
    bad = spot.translate(f"!({formula})", dict=controller.get_dict())
    assert controller.intersecting_word(bad) is None


def test_gridworld_matches_cli_antichain_arms(gridworld):
    # Python CI builds the CLI alongside the extension. Installed wheels may
    # supply only the extension; the binding regression above still runs there.
    cli = Path(ab.__file__).resolve().parents[1] / "acacia-bonsai"
    if not cli.is_file():
        pytest.skip("requires the CLI built alongside the Python bindings")
    model, shielding = gridworld
    formula = model.bare_ltl(model.paper_gridworld())
    _, result = shielding.build_game(formula, 16)
    common = [str(cli), "--spot-fast", "off", "--weakening", "off",
              "-K", "16", "-M", "16", "-i", ",".join(model.INPUT_APS),
              "-o", ",".join(model.OUTPUT_APS), "-f", formula]
    real = subprocess.run([*common, "--arms", "real:small:backward"],
                          capture_output=True, text=True, timeout=30)
    # A real-only bounded arm cannot prove LTL unrealizability; it returns
    # UNKNOWN when no bounded winning region is found.
    assert real.returncode == 2, real.stdout + real.stderr
    assert (real.stdout + real.stderr).strip() == "UNKNOWN"
    assert not result.is_real()
    unreal = subprocess.run([*common, "--arms", "unreal:formula:backward"],
                            capture_output=True, text=True, timeout=30)
    assert unreal.returncode == 1, unreal.stdout + unreal.stderr
    assert unreal.stdout.strip() == "UNREALIZABLE"
