"""Binding regressions for checked bounds (#383) and saturated successors (#389)."""

import pytest

import acacia_boomslang as ab


# The configured counter type is signed char in every supported preset.
MAX_RANK = 127


def one_state_game():
    game = ab.create_twa("1", [], [])
    assert ab.num_states(game) == 1
    assert ab.state_is_accepting(game, 0)
    ab.set_bool_thresh_no_bool_states(game, MAX_RANK)
    return game


@pytest.mark.parametrize(
    "k_min,k_max,k_inc",
    [
        (0, 0, 1),
        (-1, -1, 1),
        (128, 128, 1),
        (256, 256, 1),  # Previously wrapped to zero and claimed realizability.
        (0, 3, 1),
        (-1, 3, 1),
        (1, 0, 1),
        (1, -1, 1),
        (1, 128, 1),
        (1, 256, 1),
        (3, 2, 1),
        (1, 3, 0),
        (1, 3, -1),
        (1, 3, 128),
        (1, 3, 256),
        (1, 3, 2**31 - 1),
    ],
)
def test_solve_rejects_invalid_bounds(k_min, k_max, k_inc):
    game = one_state_game()
    with pytest.raises(ValueError):
        ab.solve_acacia_safety_game(game, k_max, k_min, k_inc)


@pytest.mark.parametrize("argument", ["k_min", "k_max", "k_inc"])
@pytest.mark.parametrize("value", [-(2**31) - 1, 2**31])
def test_solve_rejects_integers_outside_binding_int_range(argument, value):
    game = one_state_game()
    bounds = dict(k_min=1, k_max=3, k_inc=1)
    bounds[argument] = value
    with pytest.raises(OverflowError):
        ab.solve_acacia_safety_game(game, **bounds)


@pytest.mark.parametrize(
    "k_min,k_max,k_inc",
    [(1, 1, 1), (3, 3, 1), (MAX_RANK, MAX_RANK, MAX_RANK),
     (1, MAX_RANK, 1), (1, MAX_RANK, MAX_RANK)],
)
def test_accepting_self_loop_loses_at_every_valid_bound(k_min, k_max, k_inc):
    game = one_state_game()
    result = ab.solve_acacia_safety_game(game, k_max, k_min, k_inc)
    assert not result.is_real()
    assert result.get_winning_region() is None


@pytest.mark.parametrize("k", [1, 3, MAX_RANK])
@pytest.mark.parametrize("increment", [1, MAX_RANK])
def test_successful_result_contains_initial_state(k, increment):
    game = ab.create_twa("0", [], [])
    ab.set_bool_thresh_no_bool_states(game, k)
    result = ab.solve_acacia_safety_game(game, k, k, increment)
    assert result.is_real()
    region = result.get_winning_region()
    assert region.k() == k
    assert region.contains(ab.get_initial_state(game))


def test_successor_saturates_accepting_self_loop_at_127():
    game = one_state_game()
    vector = ab.make_vector(game, ab.IntVector([127]))
    successor = ab.successor(game, vector, [], [], 127)
    assert list(successor) == [127]


@pytest.mark.parametrize("cap", [0, 1, MAX_RANK - 1, MAX_RANK])
def test_successor_preserves_present_and_absent_ranks(cap):
    game = one_state_game()
    for rank in range(-1, MAX_RANK + 1):
        vector = ab.make_vector(game, ab.IntVector([rank]))
        successor = ab.successor(game, vector, [], [], cap)
        expected = -1 if rank == -1 else min(cap, rank + 1)
        assert list(successor) == [expected], (cap, rank)


@pytest.mark.parametrize("cap", [-1, -128, 128, 256, 2**31 - 1])
def test_successor_rejects_invalid_caps(cap):
    game = one_state_game()
    vector = ab.get_initial_state(game)
    with pytest.raises(ValueError):
        ab.successor(game, vector, [], [], cap)


@pytest.mark.parametrize("rank", [-129, -128, -2, 128, 255, 256, 2**31 - 1])
def test_make_vector_rejects_invalid_ranks(rank):
    game = one_state_game()
    with pytest.raises(ValueError):
        ab.make_vector(game, ab.IntVector([rank]))


@pytest.mark.parametrize("rank", [-1, 0, MAX_RANK])
def test_make_vector_accepts_range_endpoints(rank):
    game = one_state_game()
    assert list(ab.make_vector(game, ab.IntVector([rank]))) == [rank]
