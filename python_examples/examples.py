import spot
import acacia_python
s = "((G (F (req))) -> (G (F (grant))))"
s = "((G (F (req))) <-> (G(!grant) ))"
f = spot.parse_formula(s)
f = spot.formula_Not(f)
inputs = ["req"]
outputs = ["grant"]
ios = acacia_python.get_io_spec(inputs, outputs)
ios2 = acacia_python.create_bdds(ios)
twa = acacia_python.create_twa(f, ios2)
acacia_python.preprocess_aut_standard(twa, ios2, k_max=99)
acacia_python.set_bool_thresh_no_bool_states(twa, k_max=99)
result = acacia_python.solve_safety_game(twa, ios2, k_max=99, k_min=2, k_inc=3)

region = acacia_python.get_winning_region_of_game(twa, ios2, k_max=99, k_min=2, k_inc=3)

for vec in region:
    print("---")
    for elem in vec:
        print(elem)

