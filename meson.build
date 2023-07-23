project ('Acacia Bonsai', ['c', 'cpp'],
         default_options : ['cpp_std=c++20', 'b_ndebug=if-release'],
         license : 'GPL-3.0')

if meson.get_compiler('cpp').get_id() == 'clang'
  extra_args = []
else
  extra_args = ['-ftrack-macro-expansion=0',
                '-fno-diagnostics-show-caret']
endif

add_global_arguments (extra_args, language: 'cpp')

spot_dep = dependency ('libspot')
bddx_dep = dependency ('libbddx')
boost_dep = dependency ('boost')
gnulib_dep = dependency ('gnulib')

cpp = meson.get_compiler('cpp')

if cpp.has_header ('experimental/simd')
  stdsimd_dep = declare_dependency ()
else
  stdsimd_dep = dependency ('stdsimd')
endif

inc = include_directories ('lib', 'src')

subdir ('lib')
subdir ('src')
subdir ('tests')
subdir ('tests-synth')
