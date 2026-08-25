project ('Acacia Bonsai', ['c', 'cpp'],
         version : run_command ('scripts/git-version.py', '--pep440',
                                check : true).stdout ().strip (),
         default_options : ['cpp_std=c++23', 'b_ndebug=if-release'],
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
tlsf_deps = []
if get_option ('acacia_enable_tlsf_frontend')
  tlsf_deps += dependency (
    'tlsf',
    fallback : ['tlsf-tools', 'tlsf_dep'],
    default_options : [
      'cpp_std=c++23',
      'oxidd=disabled',
      'research_tools=false',
    ],
  )
endif

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
build_conf.set ('DEFAULT_K', get_option ('acacia_default_k'))
build_conf.set ('DEFAULT_KMIN', get_option ('acacia_default_kmin'))
build_conf.set ('DEFAULT_KINC', get_option ('acacia_default_kinc'))
build_conf.set ('ACACIA_EQUIVARIANT_MAX_STATES',
                get_option ('acacia_equivariant_max_states'))
build_conf.set ('ACACIA_EQUIVARIANT_MIN_CLIENTS',
                get_option ('acacia_equivariant_min_clients'))
build_conf.set ('ACACIA_EQUIVARIANT_MIN_BLOCKS',
                get_option ('acacia_equivariant_min_blocks'))

if get_option ('acacia_default_unreal_x') == 'automaton'
  build_conf.set ('DEFAULT_UNREAL_X', 'UNREAL_X_AUTOMATON')
elif get_option ('acacia_default_unreal_x') == 'formula'
  build_conf.set ('DEFAULT_UNREAL_X', 'UNREAL_X_FORMULA')
else
  build_conf.set ('DEFAULT_UNREAL_X', 'UNREAL_X_BOTH')
endif

if get_option ('acacia_default_spot_fast') == 'off'
  build_conf.set ('DEFAULT_SPOT_FAST', 'SPOT_FAST_OFF')
elif get_option ('acacia_default_spot_fast') == 'det_and_gfg'
  build_conf.set ('DEFAULT_SPOT_FAST', 'SPOT_FAST_DET_AND_GFG')
else
  build_conf.set ('DEFAULT_SPOT_FAST', 'SPOT_FAST_DET')
endif

if get_option ('acacia_translation_pref') == 'small+any'
  build_conf.set ('ACACIA_TRANSLATION_PREF', 'spot::postprocessor::Small')
  build_conf.set ('ACACIA_TRANSLATION_PREFS',
                  'spot::postprocessor::Small, spot::postprocessor::Any')
elif get_option ('acacia_translation_pref') == 'any'
  build_conf.set ('ACACIA_TRANSLATION_PREF', 'spot::postprocessor::Any')
  build_conf.set ('ACACIA_TRANSLATION_PREFS', 'spot::postprocessor::Any')
elif get_option ('acacia_translation_pref') == 'deterministic'
  build_conf.set ('ACACIA_TRANSLATION_PREF', 'spot::postprocessor::Deterministic')
  build_conf.set ('ACACIA_TRANSLATION_PREFS', 'spot::postprocessor::Deterministic')
else
  build_conf.set ('ACACIA_TRANSLATION_PREF', 'spot::postprocessor::Small')
  build_conf.set ('ACACIA_TRANSLATION_PREFS', 'spot::postprocessor::Small')
endif

if get_option ('acacia_enable_realizability_simplifier')
  build_conf.set ('ACACIA_ENABLE_REALIZABILITY_SIMPLIFIER', 1)
else
  build_conf.set ('ACACIA_ENABLE_REALIZABILITY_SIMPLIFIER', 0)
endif
if get_option ('acacia_enable_syntactic_bypass')
  build_conf.set ('ACACIA_ENABLE_SYNTACTIC_BYPASS', 1)
else
  build_conf.set ('ACACIA_ENABLE_SYNTACTIC_BYPASS', 0)
endif
if get_option ('acacia_enable_tlsf_frontend')
  build_conf.set ('ACACIA_ENABLE_TLSF_FRONTEND', 1)
else
  build_conf.set ('ACACIA_ENABLE_TLSF_FRONTEND', 0)
endif

if get_option ('acacia_simd_is_max')
  build_conf.set ('SIMD_IS_MAX', 'true')
else
  build_conf.set ('SIMD_IS_MAX', 'false')
endif
if get_option ('acacia_decompose_spec')
  build_conf.set ('DECOMPOSE_SPEC', 1)
else
  build_conf.set ('DECOMPOSE_SPEC', 0)
endif
if get_option ('acacia_cpre_avoid_unions')
  build_conf.set ('CPRE_AVOID_UNIONS', 1)
else
  build_conf.set ('CPRE_AVOID_UNIONS', 0)
endif
if get_option ('acacia_compile_all_components')
  build_conf.set ('ACACIA_COMPILE_ALL_COMPONENTS', 1)
else
  build_conf.set ('ACACIA_COMPILE_ALL_COMPONENTS', 0)
endif
if get_option ('acacia_enable_diagnostics')
  build_conf.set ('ACACIA_ENABLE_DIAGNOSTICS', 1)
else
  build_conf.set ('ACACIA_ENABLE_DIAGNOSTICS', 0)
endif
if get_option ('acacia_enable_equivariant_solver')
  build_conf.set ('ACACIA_ENABLE_EQUIVARIANT_SOLVER', 1)
else
  build_conf.set ('ACACIA_ENABLE_EQUIVARIANT_SOLVER', 0)
endif
if get_option ('acacia_enable_symmetric_solver')
  build_conf.set ('ACACIA_ENABLE_SYMMETRIC_SOLVER', 1)
else
  build_conf.set ('ACACIA_ENABLE_SYMMETRIC_SOLVER', 0)
endif

if get_option ('acacia_no_simd')
  build_conf.set ('NO_SIMD_DEFINE', '#ifndef NO_SIMD\n# define NO_SIMD 1\n#endif')
else
  build_conf.set ('NO_SIMD_DEFINE', '')
endif
aut_preprocessor = get_option ('acacia_aut_preprocessor')
aut_preprocessor_types = {
  'surely_losing' : 'aut_preprocessors::surely_losing',
  'no_preprocessing' : 'aut_preprocessors::no_preprocessing',
  'standard' : 'aut_preprocessors::standard',
  'elevator' : 'aut_preprocessors::elevator',
}
aut_preprocessor_gates = {
  'surely_losing' : 'ACACIA_ENABLE_AUT_PREPROCESSOR_SURELY_LOSING',
  'no_preprocessing' : 'ACACIA_ENABLE_AUT_PREPROCESSOR_NO_PREPROCESSING',
  'standard' : 'ACACIA_ENABLE_AUT_PREPROCESSOR_STANDARD',
  'elevator' : 'ACACIA_ENABLE_AUT_PREPROCESSOR_ELEVATOR',
}
build_conf.set ('AUT_PREPROCESSOR', aut_preprocessor_types[aut_preprocessor])
foreach choice, gate : aut_preprocessor_gates
  enabled = 0
  if choice == aut_preprocessor
    enabled = 1
  endif
  build_conf.set (gate, enabled)
endforeach

boolean_states = get_option ('acacia_boolean_states')
boolean_states_types = {
  'forward_saturation' : 'boolean_states::forward_saturation',
  'no_boolean_states' : 'boolean_states::no_boolean_states',
}
boolean_states_gates = {
  'forward_saturation' : 'ACACIA_ENABLE_BOOLEAN_STATES_FORWARD_SATURATION',
  'no_boolean_states' : 'ACACIA_ENABLE_BOOLEAN_STATES_NO_BOOLEAN_STATES',
}
build_conf.set ('BOOLEAN_STATES', boolean_states_types[boolean_states])
foreach choice, gate : boolean_states_gates
  enabled = 0
  if choice == boolean_states
    enabled = 1
  endif
  build_conf.set (gate, enabled)
endforeach

ios_precomputer = get_option ('acacia_ios_precomputer')
ios_precomputer_types = {
  'standard' : 'ios_precomputers::standard',
  'delegate' : 'ios_precomputers::delegate',
  'fake_vars' : 'ios_precomputers::fake_vars',
  'powset' : 'ios_precomputers::powset',
  'mona' : 'ios_precomputers::mona',
}
ios_precomputer_gates = {
  'standard' : 'ACACIA_ENABLE_IOS_PRECOMPUTER_STANDARD',
  'delegate' : 'ACACIA_ENABLE_IOS_PRECOMPUTER_DELEGATE',
  'fake_vars' : 'ACACIA_ENABLE_IOS_PRECOMPUTER_FAKE_VARS',
  'powset' : 'ACACIA_ENABLE_IOS_PRECOMPUTER_POWSET',
  'mona' : 'ACACIA_ENABLE_IOS_PRECOMPUTER_MONA',
}
build_conf.set ('IOS_PRECOMPUTER', ios_precomputer_types[ios_precomputer])
foreach choice, gate : ios_precomputer_gates
  enabled = 0
  if choice == ios_precomputer
    enabled = 1
  endif
  build_conf.set (gate, enabled)
endforeach

actioner = get_option ('acacia_actioner')
if actioner == 'no_ios_precomputation' and ios_precomputer != 'delegate'
  error ('acacia_actioner=no_ios_precomputation requires acacia_ios_precomputer=delegate')
endif
actioner_types = {
  'standard' : 'actioners::standard',
  'no_ios_precomputation' : 'actioners::no_ios_precomputation',
}
actioner_gates = {
  'standard' : 'ACACIA_ENABLE_ACTIONER_STANDARD',
  'no_ios_precomputation' : 'ACACIA_ENABLE_ACTIONER_NO_IOS_PRECOMPUTATION',
}
build_conf.set ('ACTIONER', actioner_types[actioner])
foreach choice, gate : actioner_gates
  enabled = 0
  if choice == actioner
    enabled = 1
  endif
  build_conf.set (gate, enabled)
endforeach

input_picker = get_option ('acacia_input_picker')
input_picker_types = {
  'critical_pq' : 'input_pickers::critical_pq',
  'critical' : 'input_pickers::critical',
  'critical_rnd' : 'input_pickers::critical_rnd',
  'critical_fullrnd' : 'input_pickers::critical_fullrnd',
}
input_picker_gates = {
  'critical_pq' : 'ACACIA_ENABLE_INPUT_PICKER_CRITICAL_PQ',
  'critical' : 'ACACIA_ENABLE_INPUT_PICKER_CRITICAL',
  'critical_rnd' : 'ACACIA_ENABLE_INPUT_PICKER_CRITICAL_RND',
  'critical_fullrnd' : 'ACACIA_ENABLE_INPUT_PICKER_CRITICAL_FULLRND',
}
build_conf.set ('INPUT_PICKER', input_picker_types[input_picker])
foreach choice, gate : input_picker_gates
  enabled = 0
  if choice == input_picker
    enabled = 1
  endif
  build_conf.set (gate, enabled)
endforeach

build_conf.set ('ARRAY_AND_BITSET_DOWNSET_IMPL', get_option ('acacia_array_downset'))
build_conf.set ('VECTOR_AND_BITSET_DOWNSET_IMPL', get_option ('acacia_vector_downset'))

if get_option ('acacia_array_impl') == 'auto'
  build_conf.set ('ARRAY_IMPL_DEFINE', '')
else
  build_conf.set ('ARRAY_IMPL_DEFINE', '#ifndef ARRAY_IMPL\n# define ARRAY_IMPL ' + get_option ('acacia_array_impl') + '\n#endif')
endif
if get_option ('acacia_vector_impl') == 'auto'
  build_conf.set ('VECTOR_IMPL_DEFINE', '')
else
  build_conf.set ('VECTOR_IMPL_DEFINE', '#ifndef VECTOR_IMPL\n# define VECTOR_IMPL ' + get_option ('acacia_vector_impl') + '\n#endif')
endif

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

if get_option ('acacia_compile_all_components')
  build_conf.set ('POSETS_COMPILE_ALL_COMPONENTS', 1)
  foreach gate : posets_gates
    build_conf.set (gate, 1)
  endforeach
else
  build_conf.set ('POSETS_COMPILE_ALL_COMPONENTS', 0)
endif

posets_array_impl = get_option ('acacia_array_impl')
if posets_array_impl == 'auto'
  if get_option ('acacia_no_simd')
    posets_array_impl = 'array_backed_sum'
  else
    posets_array_impl = 'simd_array_backed_sum'
  endif
endif

posets_vector_impl = get_option ('acacia_vector_impl')
if posets_vector_impl == 'auto'
  if get_option ('acacia_no_simd')
    posets_vector_impl = 'vector_backed'
  else
    posets_vector_impl = 'simd_vector_backed'
  endif
endif

posets_vector_impl_gates = {
  'simd_array_backed_sum' : 'POSETS_ENABLE_VECTOR_SIMD_ARRAY_BACKED_SUM',
  'array_backed_sum' : 'POSETS_ENABLE_VECTOR_ARRAY_BACKED_SUM',
  'simd_vector_backed' : 'POSETS_ENABLE_VECTOR_SIMD_VECTOR_BACKED',
  'simd_vector_backed_sum' : 'POSETS_ENABLE_VECTOR_SIMD_VECTOR_BACKED_SUM',
  'vector_backed' : 'POSETS_ENABLE_VECTOR_VECTOR_BACKED',
}
build_conf.set (posets_vector_impl_gates[posets_array_impl], 1)
build_conf.set (posets_vector_impl_gates[posets_vector_impl], 1)
posets_downset_gates = {
  'vector_backed' : 'POSETS_ENABLE_DOWNSET_VECTOR_BACKED',
  'vector_or_kdtree_backed' : 'POSETS_ENABLE_DOWNSET_VECTOR_OR_KDTREE_BACKED',
  'kdtree_backed' : 'POSETS_ENABLE_DOWNSET_KDTREE_BACKED',
  'bboxtree_backed' : 'POSETS_ENABLE_DOWNSET_BBOXTREE_BACKED',
  'comptrie_backed' : 'POSETS_ENABLE_DOWNSET_COMPTRIE_BACKED',
  'sharingtree_backed' : 'POSETS_ENABLE_DOWNSET_SHARINGTREE_BACKED',
  'simple_sharingtree_backed' : 'POSETS_ENABLE_DOWNSET_SIMPLE_SHARINGTREE_BACKED',
  'sharingtrie_backed' : 'POSETS_ENABLE_DOWNSET_SHARINGTRIE_BACKED',
  'skiplist_backed' : 'POSETS_ENABLE_DOWNSET_SKIPLIST_BACKED',
  'cst_backed' : 'POSETS_ENABLE_DOWNSET_CST_BACKED',
  'rank_bucketed_vector_backed' : 'POSETS_ENABLE_DOWNSET_RANK_BUCKETED_VECTOR_BACKED',
  'filtered_vector_backed' : 'POSETS_ENABLE_DOWNSET_FILTERED_VECTOR_BACKED',
  'vector_backed_bin' : 'POSETS_ENABLE_DOWNSET_VECTOR_BACKED_BIN',
}
foreach choice, gate : posets_downset_gates
  build_conf.set (gate, 0)
endforeach
build_conf.set (posets_downset_gates[get_option ('acacia_array_downset')], 1)
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
