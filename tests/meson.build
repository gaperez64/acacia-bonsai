fs = import ('fs')
py = import ('python').find_installation ('python3')

ltl_formula_canonicalize = executable (
  'ltl-formula-canonicalize',
  'ltl_formula_canonicalize.cc',
  include_directories : inc,
  dependencies : [spot_dep],
)
test ('ltl-formula-canonicalize',
      ltl_formula_canonicalize,
      args : ['--self-test'],
      suite : ['unit', 'tlsf'])

#############################
# Suite + backend configuration
#############################

conf_data = configuration_data ()

# ltlsynt is shipped as a Spot binary, so we look for it under the same
# install prefix that pkg-config reported for libspot. Fall back to PATH
# if the pkg-config variable is not available (e.g. uninstalled builds).
spot_prefix = spot_dep.get_variable (pkgconfig : 'prefix', default_value : '')
if spot_prefix != ''
  ltlsynt_exe = find_program (spot_prefix / 'bin' / 'ltlsynt', 'ltlsynt',
                              required : false)
else
  ltlsynt_exe = find_program ('ltlsynt', required : false)
endif
syfco_exe = find_program ('syfco', required : false)

#strix_exe = find_program ('strix', required : false)

if ltlsynt_exe.found ()
  conf_data.set_quoted ('LTLSYNT', ltlsynt_exe.full_path ())
else
  conf_data.set_quoted ('LTLSYNT', '/bin/false')
endif

if syfco_exe.found ()
  conf_data.set_quoted ('SYFCO', syfco_exe.full_path ())
else
  conf_data.set_quoted ('SYFCO', '/bin/false')
endif

#if strix_exe.found ()
#  conf_data.set_quoted ('STRIX', strix_exe.full_path ())
#else
#  conf_data.set_quoted ('STRIX', '/bin/false')
#endif

# NOTE: One can add multiple solvers beyond acacia-bonsai!
# See how to find the executables and send the relevant information to the
# calling script above and the commented lines here below too. You will have
# to implement a calling protocol in the calling script too.

conf_data.set_quoted ('ACABONSAI', ab_exe.full_path ())
#conf_data.set_quoted ('ACAPLUS', acacia_plus_py[0].full_path ())
#conf_data.set_quoted ('PYTHON3', py.full_path ())
#conf_data.set_quoted ('ACACIA_PLUS_LIB', acplus_lib.full_path ())
#conf_data.set_quoted ('LTL2TGBA_BIN', ltl2tgba_exe.full_path ())

check_real_file = configure_file (input : 'check-real-correct.sh.in',
                                  output : 'check-real-correct.sh',
                                  configuration : conf_data)
check_real_exe = find_program (check_real_file)

test ('version-output',
      py,
      args : [files ('check-version.py'), ab_exe],
      suite : 'version',
      depends : [ab_exe])

test ('help-output',
      py,
      args : [files ('check-help.py'), ab_exe],
      suite : ['unit', 'cli'],
      depends : [ab_exe])

# Pure source inspection: no binary, so it runs even when nothing is built yet.
test ('config-frontends-agree',
      py,
      args : [files ('check-config-frontends.py'), meson.project_source_root ()],
      suite : ['unit', 'config-frontends'])

if get_option ('acacia_enable_equivariant_solver') and get_option ('acacia_forward_safety_solver')
  test ('equivariance-budget', py,
        args : [files ('check-equivariance-budget.py'), ab_exe, meson.project_build_root ()],
        suite : ['unit', 'attribution'], depends : [ab_exe], timeout : 90)
  test ('p1-ablation', py,
        args : [files ('check-p1-ablation.py'), ab_exe, meson.project_build_root ()],
        suite : ['unit', 'attribution', 'native-gr1'], depends : [ab_exe], timeout : 90)
endif
worker_record_transport = executable (
  'worker-record-transport', 'worker_record_transport.cc')
test ('record-transport', py,
      args : [files ('check-record-transport.py'), worker_record_transport,
              meson.project_build_root ()],
      suite : ['unit', 'attribution'], depends : [worker_record_transport])
native_attribution_api = executable (
  'native-attribution-api', 'native_attribution_api.cc', dependencies : tlsf_deps)
test ('native-attribution-api', native_attribution_api,
      suite : ['unit', 'attribution', 'native-gr1'])
phase_record_failure_worker = executable (
  'phase-record-failure-worker', 'phase-record-failure-worker.cc')
native_json_binding_test = executable (
  'native-json-binding-test',
  ['native_json_binding_test.cc', '../src/native_proof_binding.cc'],
  include_directories : inc,
  dependencies : tlsf_deps)
test ('native-json-binding', native_json_binding_test,
      suite : ['unit', 'native-gr1', 'native-param-lift'])
native_gr1_cli_args = [files ('check-native-gr1-cli.py'), ab_exe,
                       meson.project_build_root (), phase_record_failure_worker]
native_lift_cli_args = [files ('check-native-param-lift-cli.py'), ab_exe,
                        meson.project_build_root ()]
native_cli_depends = [ab_exe]
if native_test_hooks
  spot_pause_hook = shared_library ('spot-translation-pause',
                                   'spot_translation_pause.cc',
                                   dependencies : [spot_dep], install : false)
  native_gr1_cli_args += ['--hook-binary', native_test_exe.full_path ()]
  native_lift_cli_args += ['--hook-binary', native_test_exe.full_path (),
                           '--spot-hook', spot_pause_hook.full_path ()]
  native_cli_depends += [native_test_exe, spot_pause_hook]
endif
test ('native-gr1-cli', py,
      args : native_gr1_cli_args,
      suite : ['unit', 'native-gr1'], depends : native_cli_depends)
native_gr1_lift_cli_args = [files ('check-native-gr1-lift-cli.py'), ab_exe,
                            meson.project_build_root ()]
if native_test_hooks
  native_gr1_lift_cli_args += [native_test_exe.full_path ()]
endif
test ('native-gr1-lift-cli', py,
      args : native_gr1_lift_cli_args,
      suite : ['unit', 'native-gr1'], timeout : 180,
      depends : native_cli_depends)
test ('native-gr1-same-thread', py,
      args : [files ('check-native-gr1-same-thread.py'), ab_exe,
              meson.project_build_root ()],
      suite : ['unit', 'native-gr1'], timeout : 1200, depends : [ab_exe])
test ('native-param-lift-cli', py,
      args : native_lift_cli_args,
      suite : ['unit', 'native-param-lift'], timeout : 180,
      depends : native_cli_depends)
test ('native-release-no-hooks', py,
      args : [files ('check-native-release-no-hooks.py'), ab_exe],
      suite : ['unit', 'native-gr1', 'native-param-lift'], depends : [ab_exe])



spot_nba_fastpath_test = executable ('spot-nba-fastpath-test',
                                     'spot_nba_fastpath_test.cc',
                                     include_directories : inc,
                                     dependencies : [spot_dep, bddx_dep])
test ('spot-nba-fastpath-classification',
      spot_nba_fastpath_test,
      suite : ['unit', 'spotfast'])

translator_options_test = executable ('translator-options-test',
                                      'translator_options_test.cc',
                                      include_directories : inc,
                                      dependencies : [spot_dep, bddx_dep])
test ('translator-options',
      translator_options_test,
      suite : ['unit', 'translator-options'])

guarded_event_univ_test = executable ('guarded-event-univ-test',
                                      'guarded_event_univ_test.cc',
                                      include_directories : inc,
                                      dependencies : [posets_dep, spot_dep, bddx_dep, stdsimd_dep])
test ('guarded-event-univ', guarded_event_univ_test,
      suite : ['unit', 'translator-options'], timeout : 180)
test ('guarded-event-univ-identity', py,
      args : [files ('guarded-event-univ-identity.py'), guarded_event_univ_test],
      suite : ['unit', 'translator-options'], timeout : 180)

event_univ_census = executable ('event-univ-census',
                               ['event_univ_census.cc', '../src/tlsf_frontend.cc'],
                               include_directories : inc,
                               dependencies : tlsf_deps + [spot_dep, bddx_dep])
test ('event-univ-census', event_univ_census, args : ['--self-test'],
      suite : ['unit', 'translator-options'])

game_backend_test = executable ('game-backend-test',
                                ['game_backend_test.cc', '../src/portfolio_arm.cc'],
                                link_with : closure_buchi_provider_lib,
                                include_directories : inc,
                                dependencies : [posets_dep, spot_dep, bddx_dep, stdsimd_dep])
test ('game-backend',
      game_backend_test,
      suite : ['unit', 'game-backend'])

spot_rows_test = executable (
  'spot-rows-test',
  'spot_rows_test.cc',
  include_directories : inc,
  dependencies : [posets_dep, spot_dep, bddx_dep, stdsimd_dep],
)
test ('spot-rows', spot_rows_test, suite : ['unit', 'spot-rows'])

closure_buchi_provider_test = executable (
  'closure_buchi_provider_test',
  'closure_buchi_provider_test.cc',
  include_directories : inc,
  link_with : closure_buchi_provider_lib,
  dependencies : [spot_dep, bddx_dep],
)
test ('closure_buchi_provider_test', closure_buchi_provider_test,
      suite : ['unit', 'closure-buchi'])

# The previous sprint's P5 headers and regressions compile in the default build; no backend gate.
spot_lazy_buchi_view_test = executable (
  'spot-lazy-buchi-view-test',
  'spot_lazy_buchi_view_test.cc',
  include_directories : inc,
  dependencies : [posets_dep, spot_dep, bddx_dep, stdsimd_dep],
)
test ('spot-lazy-buchi-view', spot_lazy_buchi_view_test,
      suite : ['unit', 'spot-lazy'])

# The previous sprint's shared P6 engine compiles without either production
# dispatch gate. Variants force each option on; internal callers also exercise
# both states. The #ifndef guards let -D override the generated defaults.
spot_provider_replay_variants = [
  ['spot-provider-replay', []],
]
foreach variant : spot_provider_replay_variants
  spot_provider_replay_test = executable(
    variant[0] + '-test',
    'spot_provider_replay_test.cc',
    link_with: closure_buchi_provider_lib,
    include_directories: inc,
    cpp_args: variant[1],
    dependencies: [posets_dep, spot_dep, bddx_dep, stdsimd_dep],
  )
  test(variant[0], spot_provider_replay_test,
       suite: ['unit', 'spot-provider-replay'])
  foreach layout : ['scan', 'grouped']
    test(variant[0] + '-' + layout, spot_provider_replay_test,
         args: ['--oracle-layout', layout], suite: ['unit', 'spot-provider-replay'])
  endforeach
endforeach

sparse_forward_rank_test = executable (
  'sparse-forward-rank-test',
  'sparse_forward_rank_test.cc',
  include_directories : inc,
  dependencies : [spot_dep, bddx_dep],
)
test ('sparse-forward-rank', sparse_forward_rank_test,
      suite : ['unit', 'sparse-rank'])

spot_letter_oracle_test = executable (
  'spot-letter-oracle-test',
  'spot_letter_oracle_test.cc',
  include_directories : inc,
  dependencies : [posets_dep, spot_dep, bddx_dep, stdsimd_dep],
)
test ('spot-letter-oracle', spot_letter_oracle_test,
      suite : ['unit', 'spot-letter-oracle'])

spot_lazy_oracle_test = executable (
  'spot-lazy-oracle-test',
  'spot_lazy_oracle_test.cc',
  include_directories : inc,
  dependencies : [posets_dep, spot_dep, bddx_dep, stdsimd_dep],
)
test ('spot-lazy-oracle', spot_lazy_oracle_test, suite : ['unit', 'spot-lazy-oracle'])
if get_option ('acacia_spot_guarded_backend')
  test ('oracle-layout-cli', py,
        args : [files ('check-oracle-layout.py'), ab_exe, meson.project_build_root ()],
        suite : ['unit', 'cli'], depends : [ab_exe], timeout : 90)
endif

# Not gated on acacia_spot_guarded_backend: the engine header compiles without
# it, only the backend dispatch is gated, so the test runs in every build --
# including the plain default one, where the backend itself is absent.
spot_guarded_forward_safety_test = executable (
  'spot-guarded-forward-safety-test',
  'spot_guarded_forward_safety_test.cc',
  include_directories : inc,
  dependencies : [posets_dep, spot_dep, bddx_dep, stdsimd_dep],
)
test ('spot-guarded-forward-safety', spot_guarded_forward_safety_test,
      suite : ['unit', 'spot-guarded'])

transition_payload_test = executable (
  'transition-payload-test',
  'transition_payload_test.cc',
  include_directories : inc,
  dependencies : [posets_dep, spot_dep, bddx_dep, stdsimd_dep],
)
test ('transition-payload',
      transition_payload_test,
      suite : ['unit', 'transition-payload'])

acceptance_core_test = executable ('acceptance-core-test',
                                   'acceptance_core_test.cc',
                                   include_directories : inc,
                                   dependencies : [spot_dep, bddx_dep])
test ('acceptance-core',
      acceptance_core_test,
      suite : ['unit', 'acceptance-core'])

direct_simulation_test = executable ('direct-simulation-test',
                                   'direct_simulation_test.cc',
                                   include_directories : inc,
                                   dependencies : [spot_dep, bddx_dep])
test ('direct-simulation',
      direct_simulation_test,
      suite : ['unit', 'direct-simulation'])

mp_nba_test = executable ('mp-nba-test',
                          'mp_nba_test.cc',
                          include_directories : inc,
                          dependencies : [spot_dep, bddx_dep])
test ('mp-nba',
      mp_nba_test,
      suite : ['unit', 'mp-nba'])

unreal_safety_core_witnesses_test = executable (
  'unreal-safety-core-witnesses-test',
  'unreal_safety_core_witnesses_test.cc',
  include_directories : inc,
  dependencies : [spot_dep],
)
test ('unreal-safety-core-witnesses',
      unreal_safety_core_witnesses_test,
      suite : ['unit', 'translation'])
weakening_record_args = [files ('check-weakening-records.py'), unreal_safety_core_witnesses_test,
                         meson.current_build_dir (), ab_exe]
if host_machine.system () == 'linux'
  record_transport_ack = shared_library (
    'record-transport-ack', 'record_transport_ack.cc',
    include_directories : inc, dependencies : cpp.find_library ('dl'), install : false)
  weakening_record_args += ['--record-ack-library', record_transport_ack]
endif
test ('unreal-weakening-records', py,
      args : weakening_record_args,
      suite : ['unit', 'attribution'], timeout : 60)

diagnostics_attempt_test = executable (
  'diagnostics-attempt-test',
  'diagnostics_attempt_test.cc',
  include_directories : inc,
  dependencies : [spot_dep],
)
test ('diagnostics-attempt',
      diagnostics_attempt_test,
      suite : ['unit', 'diagnostics'])

cpre_snapshot_test = executable ('cpre-snapshot-test', 'cpre_snapshot_test.cc',
  cpp_args : ['-DACACIA_ENABLE_DIAGNOSTICS=1'],
  include_directories : inc,
  dependencies : [spot_dep, bddx_dep, posets_dep, stdsimd_dep])
test ('cpre-snapshot', py,
      args : [files ('check-cpre-snapshot.py'), cpre_snapshot_test],
      depends : cpre_snapshot_test, suite : ['unit', 'diagnostics'], timeout : 60)

support_demand_test = executable (
  'support-demand-test',
  'support_demand_test.cc',
  include_directories : inc,
  cpp_args : ['-DACACIA_ENABLE_DIAGNOSTICS=1'],
  dependencies : [spot_dep, bddx_dep, posets_dep, stdsimd_dep],
)
test ('support-demand', support_demand_test, suite : ['unit', 'diagnostics'])

syntactic_bypass_test = executable ('syntactic-bypass-test',
                                    'syntactic_bypass_test.cc',
                                    include_directories : inc,
                                    dependencies : [spot_dep, bddx_dep])
test ('syntactic-bypass',
      syntactic_bypass_test,
      suite : ['unit', 'syntactic-bypass'])

k_schedule_test = executable ('k-schedule-test',
                              'k_schedule_test.cc',
                              include_directories : inc,
                              dependencies : [spot_dep, bddx_dep])
test ('k-schedule',
      k_schedule_test,
      suite : ['unit', 'k-schedule'])

forced_output_contradiction_test = executable ('forced-output-contradiction-test',
                                               'forced_output_contradiction_test.cc',
                                               include_directories : inc,
                                               dependencies : [spot_dep, bddx_dep])
test ('forced-output-contradiction',
      forced_output_contradiction_test,
      args : [meson.current_source_dir () / 'fixtures/forced_contradiction'],
      suite : ['unit', 'forced-output-contradiction'])

degenerate_io_test = executable ('degenerate-io-test',
                                 'degenerate_io_test.cc',
                                 include_directories : inc,
                                 dependencies : [spot_dep, bddx_dep])
test ('degenerate-io',
      degenerate_io_test,
      suite : ['unit', 'degenerate-io'])

degenerate_io_cli = find_program ('check-degenerate-io-cli.sh')
test ('degenerate-io-cli',
      degenerate_io_cli,
      args : [ab_exe],
      suite : ['unit', 'degenerate-io'],
      depends : [ab_exe])

game_backend_cli = find_program ('check-game-backend-cli.sh')
# Exercise the real worker boundary even in docker_default CI. This test-only
# binary uses the same sources; no installed binary, preset or arm is changed.
game_backend_integration_sources = [
  '../src/acacia-bonsai.cc',
  '../src/arg_parser.cc',
  '../src/portfolio_arm.cc',
  '../src/version.cc',
  '../src/solver/mealy_to_moore.cc',
  '../src/solver/solver_invoker.cc',
  '../src/solver/obligation_helper.cc',
  '../src/parity_arm.cc',
  '../src/solver/solve_game_vector.cc',
]
game_backend_integration_sources += [
  '../src/native_support.cc',
  '../src/native_proof_binding.cc',
  '../src/native_gr1_arm.cc',
  '../src/native_gr1_lift_arm.cc',
  '../src/native_param_lift_arm.cc',
]
game_backend_integration_sources += '../src/tlsf_frontend.cc'
preparation_fault_link_args = []
preparation_fault_cpp_args = []
if host_machine.system () == 'linux' and (get_option ('b_ndebug') == 'false' or (get_option ('b_ndebug') == 'if-release' and get_option ('buildtype') != 'release'))
  preparation_fault_link_args = ['-Wl,--wrap=tlsf_convert']
  preparation_fault_cpp_args = ['-DACACIA_TLSF_PREPARATION_TEST_HOOKS=1']
endif
game_backend_integration = executable (
  'game-backend-integration', game_backend_integration_sources,
  'synthesis_rendezvous_hooks.cc', version_h,
  link_with : [closure_buchi_provider_lib, parity_route_lib, parity_lib],
  include_directories : inc,
  cpp_args : ['-DACACIA_PORTFOLIO_TEST_HOOKS=1',
              '-DACACIA_FORWARD_SAFETY_SOLVER=1',
              '-DACACIA_SPOT_GUARDED_BACKEND=1', '-DACACIA_SPOT_LAZY_PROVIDER=1'] + preparation_fault_cpp_args,
  link_args : preparation_fault_link_args,
  dependencies : [posets_dep, spot_dep, bddx_dep, stdsimd_dep] + tlsf_deps,
  install : false,
)
if get_option ('b_ndebug') == 'false' or (get_option ('b_ndebug') == 'if-release' and get_option ('buildtype') != 'release')
  portfolio_deadline_args = [files ('check-portfolio-deadline.py'), game_backend_integration,
                             meson.project_build_root ()]
  portfolio_deadline_args += ['--native']
  if host_machine.system () == 'linux'
    test ('synthesis-rendezvous', py,
          args : [files ('check-synthesis-rendezvous.py'), game_backend_integration,
                  meson.current_build_dir (), record_transport_ack],
          suite : ['unit', 'portfolio', 'synthesis'], timeout : 90,
          depends : [game_backend_integration])
    test ('synthesis-worker-cleanup', py,
          args : [files ('check-synthesis-worker-cleanup.py'), game_backend_integration,
                  meson.current_build_dir ()],
          suite : ['unit', 'portfolio', 'synthesis'], timeout : 90,
          depends : [game_backend_integration])
  endif
  test ('portfolio-deadline', py,
        args : portfolio_deadline_args,
        suite : ['unit', 'portfolio'], depends : [game_backend_integration])
endif
if native_test_hooks
  test ('attribution-contracts', py,
        args : [files ('check-attribution-contracts.py'), native_test_exe,
                tlsf_native.get_variable ('native_contract_api_exe'),
                meson.current_build_dir ()],
        suite : ['unit', 'attribution'], timeout : 180)
  test ('attribution', py,
        args : [files ('check-attribution.py'), ab_exe, native_test_exe,
                game_backend_integration, meson.project_build_root ()],
        suite : ['unit', 'attribution', 'portfolio'], timeout : 180,
        depends : [ab_exe, native_test_exe, game_backend_integration])
endif
test ('game-backend-integration', game_backend_cli,
      args : [game_backend_integration,
              'true', 'true', 'true',
              (compiler_profile == 'debug').to_string (),
              'only',
              '200000',
              'false'],
      suite : ['unit', 'game-backend'], depends : [game_backend_integration])
test ('spot-worker-boundary', py,
      args : [files ('check-spot-worker.py'), game_backend_integration],
      suite : ['unit', 'tlsf', 'game-backend'], depends : [game_backend_integration])
test ('game-backend-cli',
      game_backend_cli,
      args : [ab_exe, get_option ('acacia_forward_safety_solver').to_string (),
              get_option ('acacia_spot_guarded_backend').to_string (),
              get_option ('acacia_spot_lazy_provider').to_string (),
              (compiler_profile == 'debug').to_string (),
              'only',
              '200000'],
      suite : ['unit', 'game-backend'],
      depends : [ab_exe])

pipe_verdict = find_program ('check-pipe-verdict.sh')
test ('pipe-verdict',
      pipe_verdict,
      args : [ab_exe],
      suite : ['unit', 'cli'],
      depends : [ab_exe])

mealy_to_moore_test = executable ('mealy-to-moore-test',
                                  '../src/solver/mealy_to_moore.cc',
                                  'mealy_to_moore_test.cc',
                                  include_directories : inc,
                                  dependencies : [spot_dep, bddx_dep])
test ('mealy-to-moore',
      mealy_to_moore_test,
      suite : ['unit', 'tlsf', 'synthesis'])

bash = find_program ('bash')
default_real_children = 1
default_unreal_children = 2
default_check_children = default_real_children + default_unreal_children
# Compiling spot-guarded leaves this default unchanged, as in arg_parser.hh.
default_game_backend = 'backward'
if get_option ('acacia_forward_safety_solver')
  default_game_backend = 'forward'
endif
# A build that ships its own portfolio forks that instead of the historical
# default, so the expected child count is the number of arms it declares.
# default_game_backend is deliberately NOT touched: it is only asserted for
# cases that pass -r or -u explicitly, and those bypass the shipped portfolio
# and still take the compile-time backend.
default_arms_opt = get_option ('acacia_default_arms').strip ()
if default_arms_opt != ''
  default_check_children = 0
  needs_real = false
  needs_unreal = false
  has_legacy_real = false
  has_legacy_unreal = false
  foreach arm : default_arms_opt.split (',')
    spec = arm.strip ()
    real_arm = spec.startswith ('real:') or spec.startswith ('both:')
    unreal_arm = spec.startswith ('unreal:') or spec.startswith ('both:')
    needs_real = needs_real or real_arm
    needs_unreal = needs_unreal or unreal_arm
    native_arm = spec.contains (':gr1:oxidd') or spec.contains (':gr1-lift:oxidd') or spec.contains (':gr1-real-lift:oxidd') or spec.contains (':param-lift:oxidd')
    if not native_arm and spec != 'both:obligation:spot'
      default_check_children += 1
      has_legacy_real = has_legacy_real or real_arm
      has_legacy_unreal = has_legacy_unreal or unreal_arm
    endif
  endforeach
  if needs_real and not has_legacy_real
    default_check_children += default_real_children
  endif
  if needs_unreal and not has_legacy_unreal
    default_check_children += default_unreal_children
  endif
endif
if (default_arms_opt.contains (':gr1:oxidd') or
    default_arms_opt.contains (':gr1-lift:oxidd') or
    default_arms_opt.contains (':gr1-real-lift:oxidd') or
    default_arms_opt.contains (':param-lift:oxidd'))
  test ('default-native-arms', py,
        args : [files ('check-default-native-arms.py'), ab_exe,
                meson.project_build_root (), default_arms_opt,
                default_check_children.to_string (),
                (compiler_profile == 'debug').to_string ()],
        suite : ['unit', 'cli', 'native-gr1', 'native-param-lift'],
        timeout : 60, depends : [ab_exe])
endif
test ('check-selection',
      bash,
      args : [files ('check-selection.sh'), ab_exe,
              default_check_children.to_string (),
              (compiler_profile == 'debug').to_string (),
              default_game_backend],
      suite : ['unit', 'check-selection'],
      depends : [ab_exe])

cap_census_test = executable ('cap-census-test',
                              'cap_census_test.cc',
                              include_directories : inc,
                              dependencies : [spot_dep, bddx_dep])
test ('cap-census',
      cap_census_test,
      suite : ['unit', 'preprocessing'])

push_aps_test = executable ('push-aps-test',
                            'push_aps_test.cc',
                            include_directories : inc,
                            dependencies : [spot_dep, bddx_dep])
test ('push-aps', push_aps_test, suite : ['unit', 'push-aps'])

if host_machine.system () == 'linux'
  phase_observation_off = executable (
    'phase-observation-off', 'phase_observation_off.cc',
    include_directories : inc,
    dependencies : [posets_dep, spot_dep, bddx_dep, stdsimd_dep],
    link_args : ['-Wl,--wrap=getpid', '-Wl,--wrap=clock_gettime', '-Wl,--wrap=getrusage',
                 '-Wl,--wrap=_ZSt19uncaught_exceptionsv'])
  test ('phase-observation-off', phase_observation_off, suite : ['unit', 'attribution'])
  phase_query_progress = executable (
    'phase-query-progress', 'phase_query_progress.cc',
    include_directories : inc, dependencies : [spot_dep, bddx_dep])
  test ('phase-query-progress', phase_query_progress, suite : ['unit', 'attribution'])
endif

tlsf_conversion_guard_sources = []
tlsf_conversion_guard_args = []
if host_machine.system () == 'linux'
  tlsf_conversion_guard_sources += files ('tlsf_no_manager.cc')
  tlsf_conversion_guard_args += '-Wl,--wrap=oxidd_bdd_manager_new'
  tlsf_conversion_guard_args += '-Wl,--wrap=cover_build'
endif

tlsf_frontend_test = executable (
  'tlsf-frontend-test',
  '../src/tlsf_frontend.cc',
  'tlsf_frontend_test.cc', tlsf_conversion_guard_sources,
  link_args : tlsf_conversion_guard_args,
  include_directories : inc,
  dependencies : tlsf_deps + [spot_dep, bddx_dep],
)
test ('tlsf-frontend', tlsf_frontend_test, suite : ['unit', 'tlsf'])

test ('moore-synthesis-semantics',
      py,
      args : [files ('check-moore-synthesis.py'), ab_exe],
      suite : ['unit', 'tlsf', 'synthesis'],
      timeout : 60,
      depends : [ab_exe])

tlsf_frontend_inspect = executable (
  'tlsf-frontend-inspect',
  '../src/tlsf_frontend.cc',
  'tlsf_frontend_inspect.cc',
  include_directories : inc,
  dependencies : tlsf_deps + [spot_dep, bddx_dep],
)
tlsf_conversion_compare = executable (
  'tlsf-conversion-compare',
  ['tlsf_conversion_compare.cc', '../src/tlsf_frontend.cc'],
  include_directories : inc, dependencies : tlsf_deps + [spot_dep, bddx_dep])
test ('tlsf-conversion-focused', tlsf_conversion_compare, args : ['--focused'],
      suite : ['unit', 'tlsf'])
if ltlsynt_exe.found () and get_option ('acacia_spot_guarded_backend')
  test ('extended-weakening-games', py,
        args : [files ('check-extended-weakening.py'), ab_exe,
                unreal_safety_core_witnesses_test, tlsf_frontend_inspect, ltlsynt_exe,
                meson.current_build_dir ()],
        suite : ['unit', 'tlsf', 'translation'], timeout : 60)
endif

symmetry_search_test = executable ('symmetry-search-test',
                                    'symmetry_search_test.cc',
                                    include_directories : inc,
                                    dependencies : [spot_dep, bddx_dep])
test ('symmetry-search',
      symmetry_search_test,
      suite : ['unit', 'symmetry'])

symmetry_blocks_test = executable ('symmetry-blocks-test',
                                    'symmetry_blocks_test.cc',
                                    include_directories : inc,
                                    dependencies : [spot_dep, bddx_dep])
test ('symmetry-blocks',
      symmetry_blocks_test,
      suite : ['unit', 'symmetry'])

test ('equivariance-backends', py,
      args : [files ('check-equivariance-backends.py'), meson.project_build_root ()],
      suite : ['unit', 'symmetry'], timeout : 120)

# Only the debug predicate's test helper keeps assertions in the release profile.
equivariant_invariant_test_helper = static_library (
  'equivariant-invariant-test-helper',
  'equivariant_invariant_test.cc',
  cpp_args : ['-UNDEBUG'],
  include_directories : inc,
  dependencies : [spot_dep, bddx_dep, posets_dep],
)
equivariant_pre_test = executable ('equivariant-pre-test',
                                   'equivariant_pre_test.cc',
                                   link_with : equivariant_invariant_test_helper,
                                   include_directories : inc,
                                   dependencies : [spot_dep, bddx_dep, posets_dep])
test ('equivariant-pre',
      equivariant_pre_test,
      suite : ['unit', 'symmetry'])

small_invariant_test = executable ('small-invariant-test',
                                  'small_invariant_test.cc',
                                  include_directories : inc,
                                  dependencies : [spot_dep, bddx_dep, posets_dep, stdsimd_dep])
test ('small-invariant',
      small_invariant_test,
      suite : ['unit', 'small-invariant'])

local_certificate_test = executable ('local-certificate-test',
                                     'local_certificate_test.cc',
                                     include_directories : inc,
                                     dependencies : [spot_dep, bddx_dep, posets_dep, stdsimd_dep])
test ('local-certificate',
      local_certificate_test,
      suite : ['unit', 'local-certificate'])

forward_safety_game_test = executable ('forward-safety-game-test',
                                       'forward_safety_game_test.cc',
                                       include_directories : inc,
                                       dependencies : [spot_dep, bddx_dep, posets_dep, stdsimd_dep])
test ('forward-safety-game',
      forward_safety_game_test,
      suite : ['unit', 'forward-safety-game'])

semantic_mona_test = executable ('semantic-mona-test',
                                 'semantic_mona_test.cc',
                                 include_directories : inc,
                                 dependencies : [spot_dep, bddx_dep, posets_dep, stdsimd_dep])
test ('semantic-mona',
      semantic_mona_test,
      suite : ['unit', 'ios-precomputer'])

component_concepts_test = executable ('component-concepts-test',
                                      'component_concepts_test.cc',
                                      include_directories : inc,
                                      dependencies : [posets_dep, spot_dep, bddx_dep])
test ('component-concepts',
      component_concepts_test,
      suite : ['unit', 'component-concepts'])

# Tests (quick; `meson test`) are run with acacia-bonsai only.
test_backends = { 'ab' : '-a' }

# Benchmarks (heavier; `meson test --benchmark`) are also run against
# ltlsynt when available so that we can compare on the same inputs.
benchmark_backends = { 'ab' : '-a' }
if ltlsynt_exe.found ()
  benchmark_backends += { 'ltlsynt' : '-l' }
endif

#############################
# Suite manifest
#############################
#
# Instead of maintaining two huge inline dicts in this file, the test and
# benchmark suites are listed as plain-text files under:
#
#   tests/suites/tests/<folder>/<suite>.list        (test() entries)
#   tests/suites/benchmarks/<folder>/<suite>.list   (benchmark() entries)
#
# Each `.list` file is one filename per line; `#` introduces a comment
# and `@include other` splices in the contents of another `.list` from
# the same folder (used for aggregates like `syntcomp21/all`).
#
# `tests/suites/expand-lists.py` flattens all `.list` files into a
# line-oriented manifest that we iterate below. To add or change a
# suite, edit (or create) a `.list` file — no meson changes required.
#
# A suite whose name starts with `!` is loaded but skipped by the
# iteration below (historically used to disable `large` suites).
expand_script = files ('suites' / 'expand-lists.py')
expand_res = run_command (py, expand_script, check : true)
manifest = expand_res.stdout ().split ('\n')

# Build the two dictionaries from the manifest.
# Each meaningful line is:
# KIND \t FOLDER \t SUITE \t FILE \t SOURCE \t TLSF_SOURCE
# where KIND is one of { T, B, ST, SB } — the `S` prefix marks a skipped
# suite (name starts with `!`); those lines are ignored here, matching
# the old behaviour of skipping suites whose key started with `!`.
test_files      = {}
benchmark_files = {}

_append = { 'T' : 'test', 'B' : 'benchmark' }

foreach line : manifest
  if line == ''
    continue
  endif
  parts = line.split ('\t')
  kind   = parts[0]
  folder = parts[1]
  suite  = parts[2]
  file   = parts[3]
  source = parts[4]
  tlsf_source = parts[5]

  if not _append.has_key (kind)
    # 'ST' / 'SB' — skipped, don't populate.
    continue
  endif

  if kind == 'T'
    d = test_files
  else
    d = benchmark_files
  endif

  if not d.has_key (folder)
    d += { folder : {} }
  endif
  folder_map = d[folder]
  if not folder_map.has_key (suite)
    folder_map += { suite : [] }
  endif
  folder_map += {
    suite : folder_map[suite] + [file + '\t' + source + '\t' + tlsf_source]
  }
  d += { folder : folder_map }

  if kind == 'T'
    test_files = d
  else
    benchmark_files = d
  endif
endforeach

tlsf_corpus_dir = get_option ('acacia_tlsf_corpus_dir')
skipped_tlsf_entries = 0
expanded = { 'test' : [], 'benchmark' : [] }

foreach kind, kind_files : { 'test' : test_files, 'benchmark' : benchmark_files }
  entries = []
  foreach folder, testset : kind_files
    foreach suite, files_in_suite : testset
      foreach record : files_in_suite
        record_parts = record.split ('\t')
        file = record_parts[0]
        source = record_parts[1]
        tlsf_source = record_parts[2]
        if tlsf_source != ''
          if tlsf_corpus_dir == ''
            skipped_tlsf_entries += 1
            continue
          endif
          source_args = ['-T', tlsf_corpus_dir / tlsf_source]
        else
          filename = 'ltl' / source
          if not fs.exists (filename)
            error ('File ' + filename + ' not found (referenced by '
                   + folder + '/' + suite + ').')
          endif
          source_args = ['-F', files (filename)]
        endif
        entries += [{
          'file' : file,
          'args' : source_args,
          'suffixes' : ['', '/' + suite, '/' + folder + '/' + suite,
                        '/' + folder],
          'all_suites' : ['all/' + file, 'all', 'all/' + suite,
                          'all/' + folder + '/' + suite,
                          'all/' + folder],
        }]
      endforeach
    endforeach
  endforeach
  expanded += { kind : entries }
endforeach

#############################
# Test suite (`meson test`)
#############################

foreach entry : expanded['test']
  foreach backend, opt : test_backends
    backend_suites = []
    foreach suffix : entry['suffixes']
      backend_suites += [backend + suffix]
    endforeach
    test (backend + '/' + entry['file'],
          check_real_exe,
          # opt selects acacia-bonsai (-a) or another configured backend.
          args : ['-p', opt] + entry['args'],
          suite : backend_suites + entry['all_suites'],
          timeout: 30,
          depends : [ab_exe])
  endforeach
endforeach

#############################
# Benchmark suite (`meson test --benchmark`)
#############################

foreach entry : expanded['benchmark']
  foreach backend, opt : benchmark_backends
    backend_suites = []
    foreach suffix : entry['suffixes']
      backend_suites += [backend + suffix]
    endforeach
    benchmark (backend + '/' + entry['file'],
               check_real_exe,
               # opt selects acacia-bonsai (-a) or another configured backend.
               args : ['-p', opt] + entry['args'],
               suite : backend_suites + entry['all_suites'],
               timeout: 10,
               depends: [ab_exe])
  endforeach
endforeach

if skipped_tlsf_entries > 0
  message (
    skipped_tlsf_entries.to_string ()
    + ' TLSF suite entries were skipped because acacia_tlsf_corpus_dir is empty. '
    + 'Materialize them with `python3 benchmarking/syntcomp-corpus.py '
    + 'materialize --out DIR` and configure with -Dacacia_tlsf_corpus_dir=DIR.'
  )
endif

#############################
# Synthesis test suite
#############################

# Collect all unique realizable benchmarks — both the `test()` and
# `benchmark()` inputs contribute.
synthesis_benchmarks = {}

if test_files.has_key ('realizable')
  foreach suite, flist : test_files['realizable']
    foreach record : flist
      f = record.split ('\t')[0]
      synthesis_benchmarks += { f : true }
    endforeach
  endforeach
endif

if benchmark_files.has_key ('realizable')
  foreach suite, flist : benchmark_files['realizable']
    foreach record : flist
      f = record.split ('\t')[0]
      synthesis_benchmarks += { f : true }
    endforeach
  endforeach
endif

# Instances no Acacia version is known to answer. The suite asserts a verdict,
# so leaving one here asserts something the solver cannot produce, and the test
# reports a failure that is not a regression. Each entry states its evidence;
# do not add one without it.
#
#   SPIPureNext.ltl -- exhausts memory during action construction and returns
#     UNKNOWN. Every probe from v1.9 to master ends in std::bad_alloc. The
#     syntcomp24 crossover records Acacia 1.x solving it in 9.437s, but the 1.x
#     binary reports REALIZABLE when a worker is killed by a signal, and 9.437s
#     matches time-to-exhaust-memory; see the section on that in
#     LTLSYNT-GAP.md (archived in benchmarking-root-legacy,
#     evidence-2026-09-27). CI did not see this before because at -O0 the
#     solver reached the 51s timeout, which the harness tolerates, before it
#     reached its memory limit; at -O2 it gets there in ~11s and answers.
#     It fails the ab/large1 suite for the same reason, which CI does not run.
synthesis_known_unsolved = ['SPIPureNext.ltl']

foreach f, _ : synthesis_benchmarks
  filename = 'ltl/realizable' / f
  if fs.exists (filename) and not synthesis_known_unsolved.contains (f)
    test ('synthesis/' + f,
          check_real_exe,
          args : [ '-p', '-a', '-F', files (filename), '--', '-s', f + '.out.aag' ],
          suite : 'synthesis',
          timeout : 30,
          depends : [ab_exe])
  endif
endforeach

action_payload_test = executable ('action-payload-test', 'action_payload_test.cc',
                                  include_directories : inc,
                                  dependencies : [spot_dep, bddx_dep, posets_dep, stdsimd_dep],
                                  cpp_args : ['-UNO_VERBOSE'])
test ('action-payload', action_payload_test, suite : ['unit', 'action-payload'], timeout : 120)

obligation_helper_test = executable (
  'obligation-helper-test',
  ['obligation_helper_test.cc', '../src/solver/obligation_helper.cc',
   '../src/solver/mealy_to_moore.cc'],
  include_directories : inc, dependencies : [spot_dep, bddx_dep] + tlsf_deps)
test ('obligation-helper', obligation_helper_test, suite : ['unit', 'obligation'])
if host_machine.system () == 'linux'
  obligation_helper_faults = shared_library (
    'obligation-helper-faults', 'obligation_helper_faults.cc',
    dependencies : [spot_dep, bddx_dep, cpp.find_library ('dl')], install : false)
  obligation_cli_args = [files ('check-obligation-helper.py'), ab_exe,
                         obligation_helper_test, obligation_helper_faults,
                         meson.current_build_dir (),
                         '--default-arms', get_option ('acacia_default_arms')]
  if ltlsynt_exe.found ()
    obligation_cli_args += ['--ltlsynt', ltlsynt_exe.full_path ()]
  endif
  obligation_cli_args += ['--tlsf']
  obligation_cli_args += ['--native']
  test ('obligation-helper-cli', py, args : obligation_cli_args,
        suite : ['unit', 'obligation', 'portfolio'], timeout : 90,
        depends : [ab_exe, obligation_helper_test, obligation_helper_faults])
  test ('obligation-helper-synthesis', py,
        args : [files ('check-moore-synthesis.py'), ab_exe,
                '--ab-option=--arms', '--ab-option=both:obligation:spot',
                '--require-obligation'],
        env : ['LD_PRELOAD=' + obligation_helper_faults.full_path ()],
        suite : ['unit', 'obligation', 'synthesis'], timeout : 120,
        depends : [ab_exe, obligation_helper_faults])
endif

parity_route_test = executable ('parity-route-test',
  ['parity_route_test.cc', 'parity_route_test_support.cc',
   '../src/solver/mealy_to_moore.cc'],
  link_with : [parity_route_lib, parity_lib], include_directories : inc,
  dependencies : [spot_dep, bddx_dep])
test ('parity-route', parity_route_test, suite : ['unit', 'parity'], timeout : 120)

parity_arm_test = executable ('parity-arm-test',
  ['parity_arm_test.cc', '../src/solver/mealy_to_moore.cc'],
  link_with : [parity_route_lib, parity_lib], include_directories : inc,
  dependencies : [spot_dep, bddx_dep])
test ('parity-arm', parity_arm_test, suite : ['unit', 'parity'], timeout : 120)
parity_arm_cli_depends = [ab_exe, parity_arm_test]
parity_arm_cli_args = [files ('check-parity-arm.py'), ab_exe, parity_arm_test,
                       meson.current_build_dir ()]
parity_arm_cli_args += ['--tlsf']
if preparation_fault_link_args.length () != 0
  parity_arm_cli_args += ['--integration-binary', game_backend_integration.full_path ()]
  parity_arm_cli_depends += [game_backend_integration]
endif
test ('parity-arm-cli', py, args : parity_arm_cli_args,
      suite : ['unit', 'parity', 'portfolio', 'synthesis'], timeout : 120,
      depends : parity_arm_cli_depends)
if host_machine.system () == 'linux' and (get_option ('b_ndebug') == 'false' or (get_option ('b_ndebug') == 'if-release' and get_option ('buildtype') != 'release'))
  test ('parity-rendezvous', py,
        args : [files ('check-parity-rendezvous.py'), game_backend_integration,
                meson.current_build_dir ()],
        suite : ['unit', 'parity', 'portfolio', 'synthesis'], timeout : 120,
        depends : [game_backend_integration])
endif

first_shipping_preset = run_command (
  py, files ('../scripts/acacia-config.py'), 'list-group', 'docker_default',
  check : true).stdout ().strip ().split ('\n')[0]
if host_machine.system () == 'linux' and get_option ('acacia_preset') == first_shipping_preset
  test ('tlsf-address-space', py,
        args : [files ('check-tlsf-address-space.py'), ab_exe,
                meson.project_build_root ()],
        suite : ['unit', 'tlsf'], depends : [ab_exe], timeout : 330,
        is_parallel : false)
endif

if get_option ('buildtype') == 'debug' and host_machine.system () == 'linux'
  record_setup_faults = shared_library ('record-setup-faults', 'phase_record_setup_faults.c',
                                        dependencies : cpp.find_library ('dl'))
  test ('worker-accounting', py,
    args : [files ('check-worker-accounting.py'), game_backend_integration,
            meson.project_build_root (), ab_exe, record_setup_faults],
    suite : ['unit', 'attribution'],
    depends : [game_backend_integration, ab_exe, record_setup_faults], timeout : 60)
endif
test ('accounting-analyzers', py, args : [files ('test_accounting_analyzers.py')],
      suite : ['unit', 'attribution'])
if host_machine.system () == 'linux'
  test ('obligation-recorded', py,
        args : [files ('check-obligation-recorded.py'), obligation_helper_test,
                meson.project_build_root (), record_transport_ack],
        suite : ['unit', 'attribution', 'obligation'],
        depends : [obligation_helper_test, record_transport_ack], timeout : 90)
endif
