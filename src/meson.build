ab_sources = [
  'acacia-bonsai.cc',
  'solver/solver_invoker.cc',
  'solver/solve_game.cc',
  'solver/solve_game_array_bitset.cc',
  'solver/solve_game_vector_bitset.cc',
  'solver/solve_game_vector_boolvec.cc',
]

if get_option('build_python')
  subdir ('python')
endif
#subdir ('python_pybind')

ab_exe = executable ('acacia-bonsai', ab_sources,
                     include_directories : inc,
                     install: true,
                     install_tag: 'cli',
                     dependencies : [posets_dep, spot_dep, bddx_dep, stdsimd_dep])
