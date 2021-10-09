project ('Acacia Bonsai', ['c', 'cpp'],
         default_options : ['cpp_std=c++20', 'b_ndebug=if-release'],
         version : '0.0.1',
         license : 'GPL-3.0')

add_global_arguments (['-ftrack-macro-expansion=0',
                       '-fno-diagnostics-show-caret'], language: 'cpp')

spot_dep = dependency('libspot')
bddx_dep = dependency('libbddx')
boost_dep = dependency('boost')
gnulib_dep = dependency('gnulib')

inc = include_directories ('lib', 'src')

subdir ('lib')
subdir ('src')
subdir ('tests')
