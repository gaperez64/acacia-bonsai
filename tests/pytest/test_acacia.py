import pytest
import spot
import acacia_python

from dataclasses import dataclass
from pathlib import Path


@dataclass
class SynthesisProblem:
    formula: str
    inputs: list[str]
    outputs: list[str]


def get_synthesis_problem(test_suite_name: str, test_name: str) -> SynthesisProblem:
    """
        Read the formula, inputs, and outputs of the specified test
        in the /tests/ltl/ folder.
    """
    base = Path("./tests/ltl") / test_suite_name

    ltl_path = base / f"{test_name}.ltl"
    part_path = base / f"{test_name}.part"

    if not base.is_dir():
        print(f"Error: dir not found '{base}'")
        exit(1)

    if not ltl_path.is_file():
        print(f"Error: file not found '{ltl_path}'")
        exit(1)

    if not ltl_path.is_file():
        print(f"Error: file not found '{part_path}'")
        exit(1)

    formula = ltl_path.read_text().strip()

    inputs: list[str] = []
    outputs: list[str] = []

    for line in part_path.read_text().splitlines():
        line = line.strip()
        if line.startswith(".inputs"):
            inputs = line.split()[1:]
        elif line.startswith(".outputs"):
            outputs = line.split()[1:]

    return SynthesisProblem(
        formula=formula,
        inputs=inputs,
        outputs=outputs,
    )


def _check_real_(test_case: SynthesisProblem):

    # acacia_python.create_twa builds the UCB automaton for whatever formula
    # is passed in and the safety algorithm then asks "can the controller
    # keep this formula falsified?". The C++ CLI negates the spec before
    # translation when checking realizability (see solver_invoker.hh's
    # run_one_ltl::operator() — "all that is needed for real is to negate
    # the formula"). To get a realizability check from this API we have to
    # do the same negation here.
    negated = "!(" + test_case.formula + ")"
    twa = acacia_python.create_twa(negated, test_case.inputs, test_case.outputs)

    # print("Automaton (HOA):")
    aut = spot.automaton(acacia_python.get_aut_hoa(twa))
    print(aut.to_str('hoa'))

    acacia_python.preprocess_aut_standard(twa, k_max=99)
    # acacia_python.preprocess_aut_surely_losing(twa, k_max=99)
    acacia_python.set_bool_thresh_no_bool_states(twa, k_max=99)
    # acacia_python.set_bool_thresh_forward_saturation(twa, k_max=99)

    # print("Solving safety game...")
    # type(game_result) = GameResult
    game_result = acacia_python.solve_acacia_safety_game(twa, k_max=99, k_min=2, k_inc=3)

    return game_result.is_real()
    # print("Done.")
    # print("Solve result:", game_result.is_real())

    # type(winning_region) = Optional[WinningRegion]
    winning_region = game_result.get_winning_region()

    # print("Does the game result contain the initial state?")
    contains = winning_region.contains(acacia_python.get_initial_state(twa))
    return contains



def test_check_real_simple():
    # The classic GR(1) request/grant spec — realizable.
    simple_test_case = SynthesisProblem(
        formula = "(G (F (req))) -> (G (F (grant)))",
        inputs = ["req"],
        outputs = ["grant"]
    )

    assert _check_real_(test_case=simple_test_case)



@pytest.mark.parametrize(
    "test_suite_name,test_name",
    [
        ("syntcomp21", "prioritized_arbiter4"),
        ("realizable", "06"),
        ("realizable", "12"),
    ],
)
def test_check_real(test_suite_name: str, test_name: str):
    tc = get_synthesis_problem(test_suite_name=test_suite_name, test_name=test_name)
    assert _check_real_(test_case=tc)
