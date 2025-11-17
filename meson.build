project ('Acacia Bonsai', ['c', 'cpp'],
         default_options : ['cpp_std=c++20', 'b_ndebug=if-release'],
         license : 'GPL-3.0')

extra_args = ['-ftrack-macro-expansion=0',
              '-fno-diagnostics-show-caret',
              '-fconcepts-diagnostics-depth=2']

add_global_arguments (extra_args, language: 'cpp')

spot_dep = dependency ('libspot')
bddx_dep = dependency ('libbddx')  # Spot's copy of BuDDy
boost_dep = dependency ('boost')
posets_dep = dependency ('posets')

cpp = meson.get_compiler('cpp')

if cpp.has_header ('experimental/simd')
  stdsimd_dep = declare_dependency ()
else
  stdsimd_dep = dependency ('stdsimd')
endif

inc = include_directories ('lib', 'src')

subdir ('lib')
subdir ('src')
