import acacia_boomslang as ab
try:
    import spot
except ImportError:
    print("Note: Spot is not available, some prints will be skipped")
    spot = None

# s = "!((G (F (req))) -> (G (F (grant))))"  # realizable — use example_real.py
s = "!((G (F (req))) <-> (G(!grant)))"
inputs = ["req"]
outputs = ["grant"]
twa = ab.create_twa(s, inputs, outputs)

if spot is not None:
    print("Automaton (HOA):")
    aut = spot.automaton(ab.get_aut_hoa(twa))
    print(aut.to_str('hoa'))


ab.preprocess_aut_standard(twa, k_max=99)
# ab.preprocess_aut_surely_losing(twa, k_max=99)
ab.set_bool_thresh_no_bool_states(twa, k_max=99)
# ab.set_bool_thresh_forward_saturation(twa, k_max=99)

print("Solving safety game...")
# type(game_result) = GameResult
game_result = ab.solve_acacia_safety_game(twa, k_max=99, k_min=2, k_inc=3)
print("Done.")
print("Solve result:", game_result.is_real())

assert not game_result.is_real()
