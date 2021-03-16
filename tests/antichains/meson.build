tests_sources = ['tests.cc']



tests_exe = executable ('tests', tests_sources,
                         include_directories : inc,
                         link_with : common_lib)

sets = [ 'standard', 'kdtree_set', 'antichain_set', 'antichain_vector' ]
vectors = [ 'standard', 'simd_vector' ]

foreach set : sets
  foreach vec : vectors
    test(set + '/' + vec, tests_exe, args : [set, vec], suite : 'set_vec_implems')
  endforeach
endforeach
