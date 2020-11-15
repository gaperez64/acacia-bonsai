project ('Acacia Bonsai', ['c', 'cpp'],
         default_options : ['cpp_std=c++2a'],
         version : '0.0.1',
         licence : 'GPL3')

## c++20 is not accepted above, and g++ behaves differently with c++2a.
add_global_arguments ('-std=c++20', language: 'cpp')
spot_dep = dependency ('libspot')

inc = include_directories ('gnulib', 'lib')

subdir ('gnulib')
subdir ('lib')
subdir ('src')
subdir ('tests')
