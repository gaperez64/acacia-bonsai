project ('Acacia Bonsai', ['c', 'cpp'],
         version : run_command(
             'bash', '-c',
             'v=$(git describe --tags --abbrev=0 2>/dev/null) && printf "%s" "${v#[vV]}" || printf "2.0"',
             check: false
         ).stdout().strip(),
         default_options : ['cpp_std=c++23', 'b_ndebug=if-release'],
         license : 'GPL-3.0')

extra_args = ['-ftrack-macro-expansion=0',
              '-fno-diagnostics-show-caret',
              '-fconcepts-diagnostics-depth=2',
              '-Wunused-function']

add_global_arguments (extra_args, language: 'cpp')

spot_dep = dependency ('libspot')
bddx_dep = dependency ('libbddx')  # Spot's copy of BuDDy
posets_dep = dependency ('posets')

cpp = meson.get_compiler('cpp')

if cpp.has_header ('experimental/simd')
  stdsimd_dep = declare_dependency ()
else
  stdsimd_dep = dependency ('stdsimd')
endif

build_conf = configuration_data ()
build_conf.set ('DEFAULT_K', get_option ('acacia_default_k'))
build_conf.set ('DEFAULT_KMIN', get_option ('acacia_default_kmin'))
build_conf.set ('DEFAULT_KINC', get_option ('acacia_default_kinc'))
build_conf.set ('STATIC_ARRAY_MAX', get_option ('acacia_static_array_max'))
build_conf.set ('STATIC_MAX_BITSETS', get_option ('acacia_static_max_bitsets'))

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

if get_option ('acacia_no_simd')
  build_conf.set ('NO_SIMD_DEFINE', '#ifndef NO_SIMD\n# define NO_SIMD 1\n#endif')
else
  build_conf.set ('NO_SIMD_DEFINE', '')
endif
if get_option ('acacia_no_array_cap_max')
  build_conf.set ('NO_ARRAY_CAP_MAX_DEFINE', '#ifndef NO_ARRAY_CAP_MAX\n# define NO_ARRAY_CAP_MAX 1\n#endif')
else
  build_conf.set ('NO_ARRAY_CAP_MAX_DEFINE', '')
endif
if get_option ('acacia_use_boolvec_over_bitset')
  build_conf.set ('USE_BOOLVEC_OVER_BITSET_DEFINE', '#ifndef USE_BOOLVEC_OVER_BITSET\n# define USE_BOOLVEC_OVER_BITSET 1\n#endif')
else
  build_conf.set ('USE_BOOLVEC_OVER_BITSET_DEFINE', '')
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

configure_file (input : 'src/config/acacia_build_config.hh.in',
                output : 'acacia_build_config.hh',
                configuration : build_conf)

inc = include_directories ('src', '.')
subdir ('src')
if get_option('build_tests')
  subdir ('tests')
endif
