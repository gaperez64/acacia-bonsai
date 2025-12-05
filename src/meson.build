ab_sources = ['acacia-bonsai.cc', 'arg_parser.cc', 'solver/create_automaton.cc']

subdir ('solver')

ab_exe = executable ('acacia-bonsai', ab_sources,
                     include_directories : inc,
                     dependencies : [boost_dep, posets_dep, spot_dep, bddx_dep, stdsimd_dep])

