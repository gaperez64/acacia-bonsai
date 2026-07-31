fs = import ('fs')
py = import ('python').find_installation ('python3')

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

#strix_exe = find_program ('strix', required : false)

if ltlsynt_exe.found ()
  conf_data.set_quoted ('LTLSYNT', ltlsynt_exe.full_path ())
else
  conf_data.set_quoted ('LTLSYNT', '/bin/false')
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

spot_nba_fastpath_test = executable ('spot-nba-fastpath-test',
                                     'spot_nba_fastpath_test.cc',
                                     include_directories : inc,
                                     dependencies : [spot_dep, bddx_dep])
test ('spot-nba-fastpath-classification',
      spot_nba_fastpath_test,
      suite : ['unit', 'spotfast'])

elevator_preprocessor_test = executable ('elevator-preprocessor-test',
                                         'elevator_preprocessor_test.cc',
                                         include_directories : inc,
                                         dependencies : [spot_dep, bddx_dep])
test ('elevator-preprocessor',
      elevator_preprocessor_test,
      suite : ['unit', 'elevator'])

symmetric_downset_test = executable ('symmetric-downset-test',
                                     'symmetric_downset_test.cc',
                                     include_directories : inc)
test ('symmetric-downset',
      symmetric_downset_test,
      suite : ['unit', 'symmetry'])

symmetric_dense_downset_test = executable ('symmetric-dense-downset-test',
                                           'symmetric_dense_downset_test.cc',
                                           include_directories : inc,
                                           dependencies : [posets_dep])
test ('symmetric-dense-downset',
      symmetric_dense_downset_test,
      suite : ['unit', 'symmetry'])

symmetric_blocks_test = executable ('symmetric-blocks-test',
                                    'symmetric_blocks_test.cc',
                                    include_directories : inc,
                                    dependencies : [spot_dep, bddx_dep])
test ('symmetric-blocks',
      symmetric_blocks_test,
      suite : ['unit', 'symmetry'])

symmetric_conversion_test = executable ('symmetric-conversion-test',
                                        'symmetric_conversion_test.cc',
                                        include_directories : inc,
                                        dependencies : [spot_dep, bddx_dep, posets_dep])
test ('symmetric-conversion',
      symmetric_conversion_test,
      suite : ['unit', 'symmetry'])

symmetric_output_orbits_test = executable ('symmetric-output-orbits-test',
                                           'symmetric_output_orbits_test.cc',
                                           include_directories : inc,
                                           dependencies : [spot_dep, bddx_dep, posets_dep])
test ('symmetric-output-orbits',
      symmetric_output_orbits_test,
      suite : ['unit', 'symmetry'])

equivariant_pre_test = executable ('equivariant-pre-test',
                                   'equivariant_pre_test.cc',
                                   include_directories : inc,
                                   dependencies : [spot_dep, bddx_dep, posets_dep])
test ('equivariant-pre',
      equivariant_pre_test,
      suite : ['unit', 'symmetry'])

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
manifest = expand_res.stdout ().strip ().split ('\n')

# Build the two dictionaries from the manifest.
# Each meaningful line is: KIND \t FOLDER \t SUITE \t FILE
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
  folder_map += { suite : folder_map[suite] + [file] }
  d += { folder : folder_map }

  if kind == 'T'
    test_files = d
  else
    benchmark_files = d
  endif
endforeach

#############################
# Test suite (`meson test`)
#############################

foreach folder, testset : test_files
  foreach suite, files_in_suite : testset
    foreach file : files_in_suite
      filename = 'ltl' / folder / file
      if not fs.exists (filename)
        error ('File ' + filename + ' not found (referenced by '
               + folder + '/' + suite + ').')
      endif
      foreach backend, opt : test_backends
        test (backend + '/' + file,
              check_real_exe,
              args : [ '-p',  ## prefix every line with the name of the test
                       opt,   ## use acacia-bonsai.  -+: acaplus, -l: ltlsync
                       '-F', files (filename) ],
              suite : [
                backend,
                backend + '/' + suite,
                backend + '/' + folder + '/' + suite,
                backend + '/' + folder,
                'all/' + file,
                'all',
                'all' + '/' + suite,
                'all' + '/' + folder + '/' + suite,
                'all' + '/' + folder
              ],
              timeout: 30,
              depends : [ab_exe])
      endforeach
    endforeach
  endforeach
endforeach

#############################
# Benchmark suite (`meson test --benchmark`)
#############################

foreach folder, testsets : benchmark_files
  foreach suite, files_in_suite : testsets
    foreach file : files_in_suite
      filename = 'ltl' / folder / file
      if not fs.exists (filename)
        error ('File ' + filename + ' not found (referenced by '
               + folder + '/' + suite + ').')
      endif
      foreach backend, opt : benchmark_backends
        benchmark (backend + '/' + file,
                   check_real_exe,
                   args : [ '-p',  ## prefix every line with the name of the test
                            opt,   ## use acacia-bonsai.  -+: acaplus, -l: ltlsync
                            '-F', files (filename) ],
                   suite : [
                     backend,
                     backend + '/' + suite,
                     backend + '/' + folder + '/' + suite,
                     backend + '/' + folder,
                     'all/' + file,
                     'all',
                     'all' + '/' + suite,
                     'all' + '/' + folder + '/' + suite,
                     'all' + '/' + folder
                   ],
                   timeout: 10,
                   depends: [ab_exe])
      endforeach
    endforeach
  endforeach
endforeach

#############################
# Synthesis test suite
#############################

# Collect all unique realizable benchmarks — both the `test()` and
# `benchmark()` inputs contribute.
synthesis_benchmarks = {}

if test_files.has_key ('realizable')
  foreach suite, flist : test_files['realizable']
    foreach f : flist
      synthesis_benchmarks += { f : true }
    endforeach
  endforeach
endif

if benchmark_files.has_key ('realizable')
  foreach suite, flist : benchmark_files['realizable']
    foreach f : flist
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
