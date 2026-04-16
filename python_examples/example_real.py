import spot
import acacia_python

s = "!((G (F (req))) -> (G (F (grant))))"
inputs = ["req"]
outputs = ["grant"]
twa = acacia_python.create_twa(s, inputs, outputs)

print("Automaton (HOA):")
aut = spot.automaton(acacia_python.get_aut_hoa(twa))
print(aut.to_str('hoa'))

acacia_python.preprocess_aut_standard(twa, k_max=99)
# acacia_python.preprocess_aut_surely_losing(twa, k_max=99)
acacia_python.set_bool_thresh_no_bool_states(twa, k_max=99)
# acacia_python.set_bool_thresh_forward_saturation(twa, k_max=99)

print("Solving safety game...")
# type(game_result) = GameResult
game_result = acacia_python.solve_acacia_safety_game(twa, k_max=99, k_min=2, k_inc=3)
print("Done.")
print("Solve result:", game_result.is_real())


# type(winning_region) = Optional[WinningRegion]
winning_region = game_result.get_winning_region()

print("Does the game result contain the initial state?")
assert winning_region.contains(acacia_python.get_initial_state(twa))

print("Region size", len(winning_region))
for vec in winning_region:
    print("---")
    print("\tVec length:", len(vec))
    for i, elem in enumerate(vec):
        # type(i) = int
        print("\t", i, elem)
print("Done")
