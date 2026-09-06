ab_sources = [
  'acacia-bonsai.cc',
  'version.cc',
  'solver/mealy_to_moore.cc',
  'solver/solver_invoker.cc',
  'solver/solve_game.cc',
  'solver/solve_game_vector.cc',
]
if get_option ('acacia_enable_tlsf_frontend')
  ab_sources += 'tlsf_frontend.cc'
endif

if get_option('build_python')
  subdir ('python')
endif
#subdir ('python_pybind')

ab_exe = executable ('acacia-bonsai', ab_sources, version_h,
                     include_directories : inc,
                     install: true,
                     install_tag: 'cli',
                     dependencies : [posets_dep, spot_dep, bddx_dep, stdsimd_dep] + tlsf_deps)

if get_option('build_research_tools')
  acacia_hoa_replay_exe = executable(
    'acacia-hoa-replay',
    [
      'research/hoa_replay.cc',
      'version.cc',
      'solver/solve_game.cc',
      'solver/solve_game_vector.cc',
    ],
    version_h,
    include_directories: inc,
    install: false,
    dependencies: [posets_dep, spot_dep, bddx_dep, stdsimd_dep],
  )
  if get_option ('acacia_enable_tlsf_frontend')
    acacia_forced_contradiction_scan_exe = executable(
      'acacia-forced-contradiction-scan',
      [
        'research/forced_contradiction_scan.cc',
        'tlsf_frontend.cc',
        'version.cc',
      ],
      version_h,
      include_directories: inc,
      install: false,
      dependencies: [spot_dep, bddx_dep] + tlsf_deps,
    )
  endif
  if get_option ('acacia_enable_diagnostics')
    acacia_antichain_replay_exe = executable(
      'acacia-antichain-replay',
      'research/antichain_replay.cc',
      include_directories: inc,
      install: false,
      dependencies: [spot_dep, bddx_dep],
    )
    # Searches a recorded region for a small inductive subregion.
    acacia_small_invariant_exe = executable(
      'acacia-small-invariant',
      'research/small_invariant_replay.cc',
      include_directories: inc,
      install: false,
      dependencies: [posets_dep, spot_dep, bddx_dep, stdsimd_dep],
    )
    # Builds and solves the full reachable bounded safety game from the same
    # recorded all-input action table.
    acacia_forward_game_replay_exe = executable(
      'acacia-forward-game-replay',
      'research/forward_game_replay.cc',
      include_directories: inc,
      install: false,
      dependencies: [posets_dep, spot_dep, bddx_dep, stdsimd_dep],
    )
    # Recomputes the same recorded update symbolically over a unary threshold
    # encoding, to measure whether the representation survives the operation.
    acacia_bdd_cpre_replay_exe = executable(
      'acacia-bdd-cpre-replay',
      'research/bdd_cpre_replay.cc',
      include_directories: inc,
      install: false,
      dependencies: [posets_dep, spot_dep, bddx_dep, stdsimd_dep],
    )
    # Replays a recorded CPre update against the real downset and vector types,
    # so unlike the antichain replay beside it this one needs posets and stdsimd.
    acacia_cpre_replay_exe = executable(
      'acacia-cpre-replay',
      'research/cpre_replay.cc',
      include_directories: inc,
      install: false,
      dependencies: [posets_dep, spot_dep, bddx_dep, stdsimd_dep],
    )
  endif
  acacia_automata_study_exe = executable(
    'acacia-automata-study',
    ['research/automata_study.cc', 'version.cc'],
    version_h,
    include_directories: inc,
    install: false,
    dependencies: [spot_dep, bddx_dep],
  )
  acacia_mp_census_exe = executable(
    'acacia-mp-census',
    'research/mp_census.cc',
    include_directories: inc,
    install: false,
    dependencies: [spot_dep, bddx_dep],
  )
endif
