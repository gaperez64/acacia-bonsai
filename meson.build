project ('Acacia Bonsai', ['c', 'cpp'],
         default_options : ['cpp_std=c++17', 'b_ndebug=if-release'],
         version : '0.0.1',
         license : 'GPL-3.0')

spot_dep = dependency ('libspot')

inc = include_directories ('gnulib', 'lib', 'std-simd', 'src')

subdir ('gnulib')
subdir ('lib')
subdir ('src')
subdir ('tests')
