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

# Pure source inspection: no binary, so it runs even when nothing is built yet.
test ('config-frontends-agree',
      py,
      args : [files ('check-config-frontends.py'), meson.project_source_root ()],
      suite : ['unit', 'config-frontends'])

if get_option ('build_research_tools') and get_option ('acacia_enable_diagnostics')
  cpre_replay_smoke = find_program ('check-cpre-replay.sh')
  test ('cpre-replay-smoke',
        cpre_replay_smoke,
        args : [ab_exe, acacia_cpre_replay_exe,
                files ('ltl/realizable/06.ltl')],
        suite : ['unit', 'research'],
        depends : [ab_exe, acacia_cpre_replay_exe])
endif

if get_option ('build_research_tools')
  research_tools_smoke = find_program ('check-research-tools.sh')
  test ('research-tools-smoke',
        research_tools_smoke,
        args : [acacia_automata_study_exe, acacia_hoa_replay_exe],
        suite : ['unit', 'research'],
        depends : [acacia_automata_study_exe, acacia_hoa_replay_exe])
endif

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

game_backend_test = executable ('game-backend-test',
                                'game_backend_test.cc',
                                include_directories : inc,
                                dependencies : [spot_dep])
test ('game-backend',
      game_backend_test,
      suite : ['unit', 'game-backend'])

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

transition_core_test = executable ('transition-core-test',
                                   'transition_core_test.cc',
                                   include_directories : inc,
                                   dependencies : [spot_dep, bddx_dep])
test ('transition-core',
      transition_core_test,
      suite : ['unit', 'transition-core'])

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

diagnostics_attempt_test = executable (
  'diagnostics-attempt-test',
  'diagnostics_attempt_test.cc',
  include_directories : inc,
  dependencies : [spot_dep],
)
test ('diagnostics-attempt',
      diagnostics_attempt_test,
      suite : ['unit', 'diagnostics'])

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

profile_dominance_test = executable (
  'profile-dominance-test',
  'profile_dominance_test.cc',
  include_directories : inc,
  dependencies : [spot_dep, bddx_dep, posets_dep],
)
test ('profile-dominance',
      profile_dominance_test,
      suite : ['unit', 'profile-dominance'])

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
test ('game-backend-cli',
      game_backend_cli,
      args : [ab_exe, get_option ('acacia_forward_safety_solver').to_string ()],
      suite : ['unit', 'game-backend'],
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
if get_option ('acacia_translation_pref') == 'small+any'
  default_real_children = 2
endif
default_unreal_children = 1
if get_option ('acacia_default_unreal_x') == 'both'
  default_unreal_children = 2
endif
default_check_children = default_real_children + default_unreal_children
default_game_backend = 'backward'
if get_option ('acacia_forward_safety_solver')
  default_game_backend = 'forward'
endif
test ('check-selection',
      bash,
      args : [files ('check-selection.sh'), ab_exe,
              default_check_children.to_string (),
              (compiler_profile == 'debug').to_string (),
              default_game_backend],
      suite : ['unit', 'check-selection'],
      depends : [ab_exe])

elevator_preprocessor_test = executable ('elevator-preprocessor-test',
                                         'elevator_preprocessor_test.cc',
                                         include_directories : inc,
                                         dependencies : [spot_dep, bddx_dep])
test ('elevator-preprocessor',
      elevator_preprocessor_test,
      suite : ['unit', 'elevator'])

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

if get_option ('acacia_enable_tlsf_frontend')
  tlsf_frontend_test = executable (
    'tlsf-frontend-test',
    '../src/tlsf_frontend.cc',
    'tlsf_frontend_test.cc',
    include_directories : inc,
    dependencies : tlsf_deps + [spot_dep, bddx_dep],
  )
  test ('tlsf-frontend', tlsf_frontend_test, suite : ['unit', 'tlsf'])

  executable (
    'tlsf-frontend-inspect',
    '../src/tlsf_frontend.cc',
    'tlsf_frontend_inspect.cc',
    include_directories : inc,
    dependencies : tlsf_deps + [spot_dep, bddx_dep],
  )
endif

symmetry_blocks_test = executable ('symmetry-blocks-test',
                                    'symmetry_blocks_test.cc',
                                    include_directories : inc,
                                    dependencies : [spot_dep, bddx_dep])
test ('symmetry-blocks',
      symmetry_blocks_test,
      suite : ['unit', 'symmetry'])

equivariant_pre_test = executable ('equivariant-pre-test',
                                   'equivariant_pre_test.cc',
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

foreach f, _ : synthesis_benchmarks
  filename = 'ltl/realizable' / f
  if fs.exists (filename)
    test ('synthesis/' + f,
          check_real_exe,
          args : [ '-p', '-a', '-F', files (filename), '--', '-s', f + '.out.aag' ],
          suite : 'synthesis',
          timeout : 30,
          depends : [ab_exe])
  endif
endforeach
