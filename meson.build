project ('Acacia Bonsai', ['c', 'cpp'],
         meson_version : '>=1.7.0',
         version : run_command ('scripts/git-version.py', '--pep440',
                                check : true).stdout ().strip (),
         default_options : ['cpp_std=c++23', 'b_ndebug=if-release',
                            'yyjson:default_library=static'],
         license : 'GPL-3.0')

extra_args = ['-ftrack-macro-expansion=0',
              '-fno-diagnostics-show-caret',
              '-fconcepts-diagnostics-depth=2',
              '-Wunused-function']

cpp = meson.get_compiler ('cpp')
add_global_arguments (cpp.get_supported_arguments (extra_args), language: 'cpp')

spot_dep = dependency ('libspot')
bddx_dep = dependency ('libbddx')  # Spot's copy of BuDDy
posets_dep = dependency ('posets')
tlsf_native = subproject ('tlsf-tools', default_options : [
  'cpp_std=c++23', 'research_tools=false',
])
tlsf_deps = [tlsf_native.get_variable ('tlsf_dep')]
native_test_hooks = get_option ('buildtype') in ['debug', 'debugoptimized'] and \
  get_option ('acacia_compiler_profile') == 'debug'

compiler_profile = get_option ('acacia_compiler_profile')
compiler_profile_args = []
compiler_profile_link_args = []
if compiler_profile == 'release'
  compiler_profile_args = [
    '-march=native',
    '-Ofast',
    '-pipe',
    '-DNO_VERBOSE',
    '-DNDEBUG',
  ]
  if host_machine.system () != 'darwin'
    compiler_profile_args += ['-fuse-linker-plugin']
    compiler_profile_link_args += ['-fuse-linker-plugin']
  endif
elif compiler_profile == 'lowmem'
  compiler_profile_args = [
    '-pipe',
    '-DNO_VERBOSE',
    '-DNDEBUG',
  ]
endif
if compiler_profile_args.length () > 0
  add_project_arguments (cpp.get_supported_arguments (compiler_profile_args),
                         language: 'cpp')
endif
if compiler_profile_link_args.length () > 0
  add_project_link_arguments (
      cpp.get_supported_link_arguments (compiler_profile_link_args),
      language: 'cpp')
endif

if cpp.has_header ('experimental/simd')
  stdsimd_dep = declare_dependency ()
else
  stdsimd_dep = dependency ('stdsimd')
endif

build_conf = configuration_data ()
if get_option ('acacia_preset') != ''
  build_conf.set_quoted ('ACACIA_PRESET', get_option ('acacia_preset'))
endif
translation_preferences = {
  'small' : 'spot::postprocessor::Small',
  'any' : 'spot::postprocessor::Any',
}
translation_preference = translation_preferences[get_option ('acacia_translation_pref')]
build_conf.set ('ACACIA_TRANSLATION_PREF', translation_preference)
build_conf.set ('ACACIA_TRANSLATION_PREFS', translation_preference)
build_conf.set ('ACACIA_BDD_MAX_INCREASE', get_option ('acacia_bdd_max_increase'))
build_conf.set ('ACACIA_FORCED_OUTPUT_CONTRADICTION',
                get_option ('acacia_forced_output_contradiction') ? 1 : 0)
build_conf.set ('SIMD_IS_MAX', get_option ('acacia_simd_is_max') ? 'true' : 'false')
build_conf.set ('ACACIA_ENABLE_DIAGNOSTICS', get_option ('acacia_enable_diagnostics') ? 1 : 0)
build_conf.set_quoted ('ACACIA_DEFAULT_ARMS', get_option ('acacia_default_arms'))
build_conf.set ('ACACIA_SPOT_LAZY_PROVIDER', get_option ('acacia_spot_lazy_provider') ? 1 : 0)
build_conf.set ('ACACIA_SPOT_GUARDED_BACKEND', get_option ('acacia_spot_guarded_backend') ? 1 : 0)
build_conf.set ('ACACIA_FORWARD_SAFETY_SOLVER',
                get_option ('acacia_forward_safety_solver') ? 1 : 0)
build_conf.set ('ACACIA_ENABLE_EQUIVARIANT_SOLVER',
                get_option ('acacia_enable_equivariant_solver') ? 1 : 0)
aut_preprocessor = get_option ('acacia_aut_preprocessor')
aut_preprocessor_types = {
  'surely_losing' : 'aut_preprocessors::surely_losing',
  'standard' : 'aut_preprocessors::standard',
}
build_conf.set ('AUT_PREPROCESSOR', aut_preprocessor_types[aut_preprocessor])
build_conf.set ('ACACIA_ENABLE_AUT_PREPROCESSOR_SURELY_LOSING',
                aut_preprocessor == 'surely_losing' ? 1 : 0)
build_conf.set ('ACACIA_ENABLE_AUT_PREPROCESSOR_STANDARD', aut_preprocessor == 'standard' ? 1 : 0)
ios_precomputer = get_option ('acacia_ios_precomputer')
ios_precomputer_types = {
  'standard' : 'ios_precomputers::standard',
  'mona' : 'ios_precomputers::mona',
  'semantic_mona' : 'ios_precomputers::semantic_mona',
}
build_conf.set ('IOS_PRECOMPUTER', ios_precomputer_types[ios_precomputer])
build_conf.set ('ACACIA_ENABLE_IOS_PRECOMPUTER_STANDARD', ios_precomputer == 'standard' ? 1 : 0)
build_conf.set ('ACACIA_ENABLE_IOS_PRECOMPUTER_MONA', ios_precomputer == 'mona' ? 1 : 0)
build_conf.set ('ACACIA_ENABLE_IOS_PRECOMPUTER_SEMANTIC_MONA',
                ios_precomputer == 'semantic_mona' ? 1 : 0)
input_picker = get_option ('acacia_input_picker')
input_picker_types = {
  'critical_pq' : 'input_pickers::critical_pq',
  'critical' : 'input_pickers::critical',
}
build_conf.set ('INPUT_PICKER', input_picker_types[input_picker])
build_conf.set ('ACACIA_ENABLE_INPUT_PICKER_CRITICAL_PQ', input_picker == 'critical_pq' ? 1 : 0)
build_conf.set ('ACACIA_ENABLE_INPUT_PICKER_CRITICAL', input_picker == 'critical' ? 1 : 0)
build_conf.set ('VECTOR_AND_BITSET_DOWNSET_IMPL', get_option ('acacia_vector_downset'))

posets_gates = [
  'POSETS_ENABLE_VECTOR_SIMD_ARRAY_BACKED',
  'POSETS_ENABLE_VECTOR_SIMD_ARRAY_BACKED_SUM',
  'POSETS_ENABLE_VECTOR_SIMD_ARRAY_PTR_BACKED',
  'POSETS_ENABLE_VECTOR_SIMD_ARRAY_PTR_BACKED_SUM',
  'POSETS_ENABLE_VECTOR_SIMD_VECTOR_BACKED',
  'POSETS_ENABLE_VECTOR_SIMD_VECTOR_BACKED_SUM',
  'POSETS_ENABLE_VECTOR_ARRAY_BACKED',
  'POSETS_ENABLE_VECTOR_ARRAY_BACKED_SUM',
  'POSETS_ENABLE_VECTOR_ARRAY_PTR_BACKED',
  'POSETS_ENABLE_VECTOR_ARRAY_PTR_BACKED_SUM',
  'POSETS_ENABLE_VECTOR_VECTOR_BACKED',
  'POSETS_ENABLE_VECTOR_VECTOR_BACKED_SUM',
  'POSETS_ENABLE_VECTOR_X_AND_BITSET',
  'POSETS_ENABLE_DOWNSET_FULL_SET',
  'POSETS_ENABLE_DOWNSET_KDTREE_BACKED',
  'POSETS_ENABLE_DOWNSET_BBOXTREE_BACKED',
  'POSETS_ENABLE_DOWNSET_COMPTRIE_BACKED',
  'POSETS_ENABLE_DOWNSET_SET_BACKED',
  'POSETS_ENABLE_DOWNSET_SHARINGTREE_BACKED',
  'POSETS_ENABLE_DOWNSET_SHARINGTRIE_BACKED',
  'POSETS_ENABLE_DOWNSET_SIMPLE_SHARINGTREE_BACKED',
  'POSETS_ENABLE_DOWNSET_SKIPLIST_BACKED',
  'POSETS_ENABLE_DOWNSET_VECTOR_BACKED',
  'POSETS_ENABLE_DOWNSET_VECTOR_BACKED_BIN',
  'POSETS_ENABLE_DOWNSET_VECTOR_BACKED_ONE_DIM_SPLIT_INTERSECTION_ONLY',
  'POSETS_ENABLE_DOWNSET_VECTOR_OR_KDTREE_BACKED',
  'POSETS_ENABLE_DOWNSET_CST_BACKED',
  'POSETS_ENABLE_DOWNSET_RANK_BUCKETED_VECTOR_BACKED',
  'POSETS_ENABLE_DOWNSET_FILTERED_VECTOR_BACKED',
]

build_conf.set ('POSETS_CONFIGURED', 1)
foreach gate : posets_gates
  build_conf.set (gate, 0)
endforeach

build_conf.set ('POSETS_COMPILE_ALL_COMPONENTS', 0)
build_conf.set ('POSETS_ENABLE_VECTOR_SIMD_VECTOR_BACKED', 1)

posets_downset_gates = {
  'vector_backed' : 'POSETS_ENABLE_DOWNSET_VECTOR_BACKED',
  'rank_bucketed_vector_backed' : 'POSETS_ENABLE_DOWNSET_RANK_BUCKETED_VECTOR_BACKED',
}
foreach choice, gate : posets_downset_gates
  build_conf.set (gate, 0)
endforeach
build_conf.set (posets_downset_gates[get_option ('acacia_vector_downset')], 1)

configure_file (input : 'src/config/acacia_build_config.hh.in',
                output : 'acacia_build_config.hh',
                configuration : build_conf)

version_h = vcs_tag (
  command : ['scripts/git-version.py', '--dirty'],
  input : 'src/version.hh.in',
  output : 'acacia_version.hh',
  fallback : meson.project_version (),
  replace_string : '@VCS_TAG@',
)

inc = include_directories ('src', '.')
subdir ('src')
if get_option('build_tests')
  subdir ('tests')
endif
