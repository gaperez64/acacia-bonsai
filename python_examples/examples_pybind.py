import os
import sys


# import acacia_bonsai_pybind
#
#
#
# result = acacia_bonsai_pybind.add(1,2)
# print(result)

import acacia_bonsai_pybind as ac
import spot

data = b"abc"
c = ac.CharContainer(data, len(data))

print("Length:", len(c))
for i, ch in enumerate(c):
    print(i, ch)


s = "((G (F (req))) -> (G (F (grant))))"
# s = "((G (F (req))) <-> (G(!grant) ))"
f = spot.parse_formula(s)
f = spot.formula_Not(f)
inputs = ["req"]
outputs = ["grant"]
ios = ac.get_io_spec(inputs, outputs)
ios2 = ac.create_bdds(ios)

# TODO: not yet compatible with spot::formula
twa = ac.create_twa(f, ios2)
ac.preprocess_aut_standard(twa, ios2, k_max=99)
ac.set_bool_thresh_no_bool_states(twa, k_max=99)