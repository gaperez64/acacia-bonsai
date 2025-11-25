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

# get all LTL files
ltl_files = files(
    'a.ltl',
    'b.ltl'
)


# Now, we merge these into triples, always ensuring that the filenames of the three
#   files match. We do this to prevent complicated debugging in case something goes wrong.
# function to extract the basename without extension
get_base = func(f)
  name = f.basename()      # "a.in"
  parts = name.split('.')  # ["a", "in"]
  parts[0]                 # "a"
endfunc


# retrieve the elements in the map. The map is a list of pairs.
lookup = func(map, key)
  result = []
  foreach pair : map
    if pair[0] == key
      result += [pair[1]]
    endif
  endforeach
  result
endfunc


# we now build maps that map the basenames to the paths
in_map   = []
out_map  = []
file_map = []

foreach f : in_files
  in_map += [['@0@'.format(get_base(f)), f]]
endforeach

foreach f : out_files
  out_map += [['@0@'.format(get_base(f)), f]]
endforeach

foreach f : ltl_files
  file_map += [['@0@'.format(get_base(f)), f]]
endforeach


# build matched triples, error on mismatch
triples = []
foreach pair : in_map
  base = pair[0]
  in_file  = pair[1]

  out_files  = lookup(out_map, base)
  ltl_files = lookup(file_map, base)

  if out_files.length() != 1 or ltl_files.length() != 1
    error('Missing matching files for basename "@0@"'.format(base))
  endif

  triples += [[in_file, out_files[0], ltl_files[0]]]
endforeach


# invoke acacia on each file
foreach triple : triples
    inputs = triple[0]
    outputs = triple[1]
    ltl_formula = triple[2]

    # TODO: do a simple call to 'echo' of some sorts, to check that everything is in order
    message('Matched: @0@, @1@, @2@'.format(triple[0].basename(), triple[1].basename(), triple[2].basename()))
    # TODO: make test messages that indicate what we are testing

    # TODO: call acacia
    test(
        tester,
        args : [  # TODO
        ],
        suite : 'check_real_all'
    )
endforeach
