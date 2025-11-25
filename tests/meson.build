# meson test --suite real

# TODO: get executable
tester = find_program()

# generator for input arguments
arggen_input = generator(
    find_program('python3'),
    output: '@BASENAME@.inputs',
    arguments: ['extract_io_args.py', '-ex_i', '--input_file', '@INPUT@', '--output_file', '@OUTPUT@']
)

# run generator
in_files = arggen_input.process(files(
    'a.part',
    'b.part'
))

# generator for output arguments
arggen_output = generator(
    find_program('python3'),
    output: '@BASENAME@.outputs',
    arguments: ['extract_io_args.py', '-ex_o', '--input_file', '@INPUT@', '--output_file', '@OUTPUT@']
)

# run generator
out_files = arggen_output.process(files(
    'a.part',
    'b.part'
))

ltl_files = files(
    'a.ltl',
    'b.ltl'
)

# TODO: call acacia
# TODO: do a simple call to 'echo' of some sorts, to check that everything is in order

# TODO: instead, we should get triples using a map and basename
foreach i : range(in_files.length())
    inputs = in_files[i]
    outputs = out_files[i]
    ltl_formula = ltl_files[i]

    test(
        tester,
        args : [  # TODO
        ],
        suite : 'check_real_all'
    )
endforeach
