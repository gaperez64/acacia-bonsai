import spot
import acacia_python
s = "((G (F (req))) -> (G (F (grant))))"
# s = "((G (F (req))) <-> (G(!grant) ))"
f = spot.parse_formula(s)
f = spot.formula_Not(f)
inputs = ["req"]
outputs = ["grant"]
ios = acacia_python.get_io_spec(inputs, outputs)
ios2 = acacia_python.create_bdds(ios)
twa = acacia_python.create_twa(f, ios2)
acacia_python.preprocess_aut_standard(twa, ios2, k_max=99)
acacia_python.set_bool_thresh_no_bool_states(twa, k_max=99)

print("Solving safety game...")
result = acacia_python.solve_acacia_safety_game(twa, ios2, k_max=99, k_min=2, k_inc=3)
print("Done.")
print("Solve result:", result)

print("Retrieving winning region...")
region = acacia_python.get_winning_region_of_game(twa, ios2, k_max=99, k_min=2, k_inc=3)
print("Done")

if result:
    assert region is not None
else:
    assert region is None



print("Printing region...")

# it = acacia_python.char_iterator("abc", 3)
# for c in it:
#     print(c)

# container = acacia_python.CharContainer("Hello")
# Clean for-loop usage
# for i, ch in enumerate(container.begin()):
#     print(i, ch)          # prints ASCII values: 0 72, 1 101, ...

# # TODO: compare this to Acacia test suite.
print("Region size", len(region))
for vec in region:
    print("---")
    print("\tVec length:", len(vec))
    for i, elem in enumerate(vec):
        print("\t", i, ord(elem))
print("Done")




