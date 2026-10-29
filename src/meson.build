parity_lib = static_library ('parity', 'solver/parity/game.cc',
  include_directories : inc, dependencies : [spot_dep, bddx_dep], install : false)
tlsf_dpa_lib = static_library ('tlsf-dpa', 'solver/parity/tlsf_dpa.cc',
  include_directories : inc, dependencies : [spot_dep, bddx_dep] + tlsf_deps, install : false)
parity_route_lib = static_library ('parity-route', 'solver/parity/route.cc',
  include_directories : inc, dependencies : [spot_dep, bddx_dep], install : false)

closure_buchi_provider_lib = static_library (
  'closure-buchi-provider',
  'solver/closure_buchi_provider.cc',
  include_directories : inc,
  dependencies : [spot_dep, bddx_dep],
)

ab_sources = [
  'acacia-bonsai.cc',
  'arg_parser.cc',
  'portfolio_arm.cc',
  'version.cc',
  'solver/mealy_to_moore.cc',
  'solver/solver_invoker.cc',
  'solver/obligation_helper.cc',
  'parity_arm.cc',
  'solver/solve_game_vector.cc',
]
ab_sources += [
  'native_support.cc',
  'native_proof_binding.cc',
  'native_gr1_arm.cc',
  'native_gr1_lift_arm.cc',
  'native_param_lift_arm.cc',
]
ab_sources += 'tlsf_frontend.cc'

if get_option('build_python')
  subdir ('python')
endif
#subdir ('python_pybind')

ab_exe = executable ('acacia-bonsai', ab_sources, version_h,
                     link_with : [closure_buchi_provider_lib, parity_route_lib, parity_lib, tlsf_dpa_lib],
                     include_directories : inc,
                     install: true,
                     install_tag: 'cli',
                     dependencies : [posets_dep, spot_dep, bddx_dep, stdsimd_dep] + tlsf_deps)

if native_test_hooks
  native_test_exe = executable ('acacia-bonsai-native-test', ab_sources,
    'native_test_hooks.cc', '../tests/native_attribution_hooks.cc', version_h,
    tlsf_native.get_variable ('gr1_reduction_source'),
    tlsf_native.get_variable ('reduction_contract_hooks'),
    link_with : [closure_buchi_provider_lib, parity_route_lib, parity_lib, tlsf_dpa_lib],
    include_directories : inc,
    cpp_args : ['-DACACIA_NATIVE_TEST_HOOKS=1', '-DTLSF_GR1_REDUCTION_TEST_FAULT'],
    install : false,
    dependencies : [posets_dep, spot_dep, bddx_dep, stdsimd_dep,
                    tlsf_native.get_variable ('tlsf_internal_dep')] + tlsf_deps)
endif
